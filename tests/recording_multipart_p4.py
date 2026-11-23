"""PC reference for the Nov 19 recording upload contract.

The HTTP transport is deliberately loopback-only. Credentials are injected by
the caller. The session engine uses a transport callback so the firmware bridge
can use the same request/response cases without using this PC transport.
"""
from dataclasses import dataclass
import hashlib
import hmac
import http.client
import json
import re
import secrets
import struct
import time
from typing import Callable, Dict, Iterable, Tuple
from urllib.parse import urlsplit


BASE = "/voice-card/device/v1/oss/audio-sync/multipart"
INIT, COMPLETE = BASE + "/init", BASE + "/complete"
PART_SIZE = 4 * 1024 * 1024
READ_SIZE = 16 * 1024
RESPONSE_MAX = 64 * 1024


class UploadError(Exception):
    pass


@dataclass(frozen=True)
class UploadConfig:
    origin: str
    imei: str
    secret: bytes


def device_signature(method, path, imei, timestamp, nonce, body, secret):
    digest = hashlib.sha256(body).hexdigest()
    canonical = "\n".join((method, path, imei, timestamp, nonce, digest))
    return hmac.new(secret, canonical.encode("utf-8"), hashlib.sha256).hexdigest()


class Recording:
    def __init__(self, file):
        self.file = file
        self.size = file.seek(0, 2)
        if not 0 < self.size <= 0x7fffffff:
            raise UploadError("invalid recording length")
        head = min(self.size, 100)
        tail = min(self.size - head, 100)
        sample = b"".join(self.chunks(0, head))
        if tail:
            sample += b"".join(self.chunks(self.size - tail, tail))
        self.md5 = hashlib.md5(sample + struct.pack("<I", self.size)).hexdigest()

    def verify_size(self):
        if self.file.seek(0, 2) != self.size:
            raise UploadError("recording length changed")

    def chunks(self, offset, length):
        if offset < 0 or length < 0 or offset + length > self.size:
            raise UploadError("part is outside the recording")
        self.file.seek(offset)
        while length:
            block = self.file.read(min(length, READ_SIZE))
            if not block:
                raise UploadError("recording read was short")
            length -= len(block)
            yield block


Transport = Callable[[str, str, Dict[str, str], Iterable[bytes], int], Tuple[int, bytes]]


def post_session(config, transport, path, payload):
    body = json.dumps(payload, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    timestamp = str(time.time_ns() // 1_000_000)
    nonce = secrets.token_hex(12)
    headers = {
        "Content-Type": "application/json",
        "X-Device-Imei": config.imei,
        "X-Device-Timestamp": timestamp,
        "X-Device-Nonce": nonce,
        "X-Device-Signature": device_signature(
            "POST", path, config.imei, timestamp, nonce, body, config.secret),
    }
    status, raw = transport("POST", config.origin + path, headers, (body,), len(body))
    if not 200 <= status < 300:
        raise UploadError("session HTTP {}".format(status))
    try:
        obj = json.loads(raw)
    except (ValueError, UnicodeError) as error:
        raise UploadError("invalid session JSON") from error
    if not isinstance(obj, dict) or obj.get("status") != "00000":
        raise UploadError("server rejected session")
    result = obj.get("result")
    if not isinstance(result, dict):
        raise UploadError("missing session result")
    return result


def upload(config: UploadConfig, transport: Transport, recording: Recording,
           local_id, filename, duration_ms, recorded_at=None, part_size=PART_SIZE,
           max_requests=128, max_retries=3):
    """Reconcile with init after each PUT or an ambiguous complete.

    A new process can start with the same file and local ID and recover from
    the server's pendingParts. The caller keeps ownership of the source.
    """
    if not re.fullmatch(r"\d{15}", config.imei, flags=re.ASCII) or not config.secret:
        raise UploadError("test device identity and signing key are required")
    if not local_id or not filename or not 0 < part_size <= PART_SIZE or duration_ms < 0:
        raise UploadError("invalid recording metadata")
    init = {
        "localFileId": local_id, "fileMd5": recording.md5, "fileSize": recording.size,
        "durationMs": duration_ms, "audioFormat": "msbc", "contentType": "audio/msbc",
        "originalFileName": filename,
    }
    if recorded_at:
        init["recordedAt"] = recorded_at
    init.update(partSize=part_size, uploadSignVersion="V2")
    previous_part, repeats, failures = None, 0, 0
    for _ in range(max_requests):
        recording.verify_size()
        result = post_session(config, transport, INIT, init)
        state = result.get("syncStatus")
        if state == "SYNC_SUCCESS":
            return result
        if state != "SYNCING":
            raise UploadError("unexpected upload state")
        ids = {name: result.get(name) for name in ("syncSessionId", "multipartUploadId")}
        if not all(isinstance(value, str) and value for value in ids.values()):
            raise UploadError("missing session IDs")
        pending = result.get("pendingParts")
        if not isinstance(pending, list):
            raise UploadError("missing pendingParts")
        if not pending:
            try:
                complete = post_session(config, transport, COMPLETE, ids)
            except (OSError, http.client.HTTPException):
                failures += 1
                if failures > max_retries:
                    raise UploadError("complete response unavailable")
                continue
            if complete.get("syncStatus") != "SYNC_SUCCESS":
                raise UploadError("server did not confirm completion")
            return complete

        part = pending[0]
        if not isinstance(part, dict):
            raise UploadError("invalid pending part")
        number, offset, length, url = (part.get(k) for k in ("partNumber", "offset", "length", "url"))
        if (any(type(n) is not int for n in (number, offset, length)) or number < 1 or
                offset < 0 or length < 1 or offset + length > recording.size or
                not isinstance(url, str) or not url.startswith(("http://", "https://"))):
            raise UploadError("invalid pending part")
        identity = (ids["multipartUploadId"], number, offset, length)
        repeats = repeats + 1 if previous_part == identity else 0
        if repeats > max_retries:
            raise UploadError("server did not advance part {}".format(number))
        previous_part = identity
        try:
            http_status, _ = transport(
                "PUT", url, {"Content-Type": "application/octet-stream"},
                recording.chunks(offset, length), length)
        except (OSError, http.client.HTTPException):
            continue
        if not 200 <= http_status < 300 and http_status not in (403, 408, 429) and http_status < 500:
            raise UploadError("PUT failed with HTTP {}".format(http_status))
        # Even a 2xx only confirms this request; init owns the durable progress.
    raise UploadError("upload did not finish within the request budget")


def loopback_transport(method, url, headers, chunks, length):
    parsed = urlsplit(url)
    if (parsed.scheme != "http" or parsed.hostname != "127.0.0.1" or
            parsed.username or parsed.password or parsed.fragment):
        raise UploadError("offline transport accepts only HTTP on 127.0.0.1")
    connection = http.client.HTTPConnection(parsed.hostname, parsed.port, timeout=5)
    target = parsed.path or "/"
    if parsed.query:
        target += "?" + parsed.query
    try:
        connection.putrequest(method, target)
        connection.putheader("Content-Length", str(length))
        for name, value in headers.items():
            connection.putheader(name, value)
        connection.endheaders()
        sent = 0
        for block in chunks:
            sent += len(block)
            if sent > length:
                raise UploadError("request body exceeds declared length")
            connection.send(block)
        if sent != length:
            raise UploadError("request body shorter than declared length")
        response = connection.getresponse()
        body = response.read(RESPONSE_MAX + 1)
        if len(body) > RESPONSE_MAX:
            raise UploadError("response exceeds offline limit")
        return response.status, body
    finally:
        connection.close()
