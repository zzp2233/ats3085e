"""Prepare no-SD P3 PUT commands and reconstruct board responses. No network I/O."""
import argparse
import hashlib
import json
from pathlib import Path
import re
from urllib.parse import urlsplit
import zlib

META_MAX = 3072
BODY_MAX = 4_800_000
RESPONSE_MAX = 16384
# Board UART RX ring is 64 bytes. Keep a whole command plus CRLF below it;
# 64 metadata bytes encoded as hex made ~150-character paste bursts.
COMMAND_CHUNK_BYTES = 16
RESERVED = {"host", "content-length", "transfer-encoding", "connection", "expect"}


def generated_chunks(length):
    for offset in range(0, length, 480):
        index = offset // 480
        yield bytes((index ^ (index >> 8) ^ (i * 29)) & 255
                    for i in range(min(480, length - offset)))


def prepare(request):
    """Input is one backend-approved first part, not the full init response."""
    length = request.get("length")
    if type(length) is not int or not 0 < length <= BODY_MAX:
        raise ValueError("length must be 1..4800000")
    if type(request.get("offset", 0)) is not int or request.get("offset", 0) != 0:
        raise ValueError("generated probe supports only a first part at offset 0")
    url = request.get("url")
    if not isinstance(url, str) or any(ord(c) <= 32 or ord(c) >= 127 for c in url):
        raise ValueError("url must be an ASCII HTTPS URL without whitespace")
    try:
        parsed = urlsplit(url)
        valid = (url.startswith("https://") and parsed.hostname and parsed.path.startswith("/")
                 and not parsed.username and not parsed.password and "#" not in url
                 and re.fullmatch(r"[A-Za-z0-9.-]+", parsed.hostname)
                 and (parsed.port is None or 1 <= parsed.port <= 65535))
    except ValueError:
        valid = False
    if not valid:
        raise ValueError("url must match the board HTTPS host/port/path format")
    headers = request.get("headers", {"Content-Type": "application/octet-stream"})
    if not isinstance(headers, dict):
        raise ValueError("headers must be an object")
    lines, seen = [], set()
    for name, value in headers.items():
        if not isinstance(name, str) or not re.fullmatch(r"[A-Za-z0-9-]+", name):
            raise ValueError("invalid header name")
        lower = name.lower()
        if lower in RESERVED or lower in seen:
            raise ValueError("reserved or duplicate header")
        if not isinstance(value, str) or any(ord(c) < 32 or ord(c) >= 127 for c in value):
            raise ValueError("header values must be printable ASCII")
        seen.add(lower)
        lines.append(name + ": " + value + "\r\n")
    # Preserve the URL, including all escaped signature parameters, verbatim.
    spec = ("PUT\n" + url + "\n" + "".join(lines)).encode("ascii")
    if len(spec) > META_MAX:
        raise ValueError("URL plus headers exceeds the 3072-byte metadata limit")
    commands = [f"app spi2putcfg begin {len(spec)} {zlib.crc32(spec):08x}"]
    for offset in range(0, len(spec), COMMAND_CHUNK_BYTES):
        commands.append(f"app spi2putcfg data {offset} {spec[offset:offset+COMMAND_CHUNK_BYTES].hex()}")
    commands.append(f"app spi2putgen {length}")
    digest = hashlib.sha256()
    for chunk in generated_chunks(length):
        digest.update(chunk)
    return "\n".join(commands) + "\n", digest.hexdigest()


def decode(log, tid):
    data = bytearray()
    result = None
    for line in log.splitlines():
        page = re.search(r"SPI HTTP DATA tid=(\d+) offset=(\d+) hex=([0-9a-fA-F]+)(?:\s|$)", line)
        if page and int(page[1]) == tid:
            if result is not None or int(page[2]) != len(data):
                raise ValueError("response has missing, repeated or reordered pages")
            block = bytes.fromhex(page[3])
            if not 0 < len(block) <= 32 or len(data) + len(block) > RESPONSE_MAX:
                raise ValueError("invalid response page length")
            data.extend(block)
        summary = re.search(r"SPI HTTP tid=(\d+) http=(\d+) bytes=(\d+)", line)
        if summary and int(summary[1]) == tid:
            if result is not None or int(summary[3]) != len(data):
                raise ValueError("response length mismatch or repeated transaction ID")
            result = int(summary[2])
    if result is None:
        raise ValueError("complete response summary not found for transaction ID")
    return result, bytes(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    prep = commands.add_parser("prepare")
    prep.add_argument("--request", required=True, type=Path, help="local JSON: url, length, offset=0, headers")
    prep.add_argument("--out", required=True, type=Path, help="new local command file (contains signed URL/headers)")
    read = commands.add_parser("decode")
    read.add_argument("--log", required=True, type=Path)
    read.add_argument("--tid", required=True, type=int)
    read.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    try:
        if args.command == "prepare":
            request = json.loads(args.request.read_text(encoding="utf-8-sig"))
            if not isinstance(request, dict):
                raise ValueError("request must be a JSON object")
            text, digest = prepare(request)
            with args.out.open("x", encoding="ascii", newline="\n") as output:
                output.write(text)
            print(f"Prepared bytes={request['length']} sha256={digest}")
            print("Commands contain credentials; keep locally and send one line at a time after each ACK.")
        else:
            code, data = decode(args.log.read_text(encoding="utf-8", errors="replace"), args.tid)
            with args.out.open("xb") as output:
                output.write(data)
            print(f"Decoded http={code} bytes={len(data)}; this is not a backend acceptance verdict.")
    except (OSError, ValueError) as error:
        # JSON exceptions and paths may contain user-provided credentials.
        parser.exit(1, f"Probe failed ({type(error).__name__}); check input, limits and output path.\n")


if __name__ == "__main__":
    main()
