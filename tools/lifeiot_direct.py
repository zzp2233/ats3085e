"""P3 lifeiot session preparation, board PUT orchestration, and completion.

PC signs init; 3085E generates the body and 503 performs the actual PUT.
Never sends file bytes from the PC, rewrites a signed URL, or disables TLS checks.
upload runs the ACK-paced serial sender and completes only after board evidence
and server reconciliation. complete reconciles an existing session without PUT.
"""
import argparse
from datetime import datetime, timezone, timedelta
import hashlib
import hmac
import http.client
import json
from pathlib import Path
import re
import secrets
import ssl
import struct
import subprocess
import sys
import time

import p3_put_probe

ORIGIN = "https://lifeiot.cmaiot.cn:8081"
HOST, PORT = "lifeiot.cmaiot.cn", 8081
INIT_PATH = "/voice-card/device/v1/oss/audio-sync/multipart/init"
COMPLETE_PATH = "/voice-card/device/v1/oss/audio-sync/multipart/complete"
PART_SIZE = 4 * 1024 * 1024
MAX_RESPONSE = 16384


def signing_key(secret_file=None, reference_project=None):
    if secret_file:
        key = secret_file.read_text(encoding="utf-8-sig").strip()
    else:
        source = reference_project / "application/bt_earphone/src/onenet/cloud_storage.h"
        matches = re.findall(r'^\s*#define\s+CLOUD_SIGN_SECRET\s+"([^"\r\n]+)"',
                             source.read_text(encoding="utf-8"), re.M)
        if len(matches) != 1:
            raise ValueError("reference signing key definition is missing or ambiguous")
        key = matches[0]
    if not key or "\\" in key:
        raise ValueError("invalid signing key")
    return key.encode("utf-8")


def signed_headers(imei, key, body, timestamp=None, nonce=None, path=INIT_PATH):
    if not re.fullmatch(r"[0-9]{15}", imei) or not key:
        raise ValueError("invalid device identity or signing key")
    timestamp = str(timestamp if timestamp is not None else time.time_ns() // 1_000_000)
    nonce = nonce or secrets.token_hex(12)
    if path not in (INIT_PATH, COMPLETE_PATH):
        raise ValueError("unsupported signing path")
    canonical = "\n".join(("POST", path, imei, timestamp, nonce, hashlib.sha256(body).hexdigest()))
    return {
        "Content-Type": "application/json",
        "X-Device-Imei": imei,
        "X-Device-Timestamp": timestamp,
        "X-Device-Nonce": nonce,
        "X-Device-Signature": hmac.new(key, canonical.encode("utf-8"), hashlib.sha256).hexdigest(),
    }


def generated_metadata(length, local_id, recorded_at):
    if type(length) is not int or not 0 < length <= PART_SIZE:
        raise ValueError("single-part generated test length must be 1..4194304")
    if not isinstance(local_id, str) or not re.fullmatch(r"[0-9]{14}", local_id):
        raise ValueError("localFileId must be a stable 14-digit timestamp")
    # Exact reference firmware algorithm: head100 + tail100 (no overlap) + LE32.
    head = b""
    tail = b""
    digest = hashlib.sha256()
    for chunk in p3_put_probe.generated_chunks(length):
        digest.update(chunk)
        head = (head + chunk)[:100]
        tail = (tail + chunk)[-100:]
    if length <= 200:
        sample = b"".join(p3_put_probe.generated_chunks(length))
    else:
        sample = head + tail
    payload = {
        "localFileId": local_id,
        "fileMd5": hashlib.md5(sample + struct.pack("<I", length)).hexdigest(),
        "fileSize": length,
        "durationMs": 0,
        # Be explicit: this probe is generated binary, not a real recording.
        "audioFormat": "bin",
        "contentType": "application/octet-stream",
        "originalFileName": "P3-generated-not-audio-" + local_id + ".bin",
        "recordedAt": recorded_at,
        "partSize": PART_SIZE,
        "uploadSignVersion": "V2",
    }
    return payload, digest.hexdigest()


def tls_context(ca_file=None):
    context = ssl.create_default_context(cafile=str(ca_file) if ca_file else None)
    # Match the board's protocol and cipher list, not the PC's TLS 1.3 defaults.
    context.minimum_version = context.maximum_version = ssl.TLSVersion.TLSv1_2
    context.set_ciphers("ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES128-GCM-SHA256")
    return context


def request_init(imei, key, payload, ca_file=None):
    return request_post(INIT_PATH, imei, key, payload, ca_file)


def request_post(path, imei, key, payload, ca_file=None):
    body = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    connection = http.client.HTTPSConnection(HOST, PORT, timeout=20, context=tls_context(ca_file))
    try:
        connection.request("POST", path, body, signed_headers(imei, key, body, path=path))
        response = connection.getresponse()
        raw = response.read(MAX_RESPONSE + 1)
        if len(raw) > MAX_RESPONSE:
            raise ValueError("response exceeds board response limit")
        return response.status, raw
    finally:
        connection.close()


def probe_backend(ca_file=None):
    """Read-only TLS/HTTP check against the exact reference backend."""
    connection = http.client.HTTPSConnection(HOST, PORT, timeout=20, context=tls_context(ca_file))
    try:
        connection.connect()
        certificate = connection.sock.getpeercert()
        result = dict(origin=ORIGIN, tls=connection.sock.version(),
                      cipher=connection.sock.cipher()[0], verified=True,
                      subject=certificate.get("subject"), issuer=certificate.get("issuer"),
                      not_after=certificate.get("notAfter"))
        connection.request("HEAD", "/")
        response = connection.getresponse()
        result.update(http=response.status, server_date=response.getheader("Date"))
        return result
    finally:
        connection.close()


def prepare_part(status, raw, total):
    if not 200 <= status < 300:
        raise ValueError("init HTTP failure; inspect the local response")
    response = json.loads(raw)
    if not isinstance(response, dict) or response.get("status") != "00000":
        raise ValueError("init business failure; inspect the local response")
    result = response.get("result")
    if not isinstance(result, dict):
        raise ValueError("missing init result")
    if result.get("syncStatus") == "SYNC_SUCCESS":
        return None, "SYNC_SUCCESS"
    if result.get("syncStatus") != "SYNCING":
        raise ValueError("unexpected session state")
    for name in ("syncSessionId", "multipartUploadId"):
        if not isinstance(result.get(name), str) or not result[name]:
            raise ValueError("missing session identity")
    pending = result.get("pendingParts")
    if not isinstance(pending, list):
        raise ValueError("missing pendingParts")
    if not pending:
        return None, "NO_PENDING_PARTS"
    if len(pending) != 1 or not isinstance(pending[0], dict):
        raise ValueError("this probe requires one whole first part")
    part = pending[0]
    for name, expected in (("partNumber", 1), ("offset", 0), ("length", total)):
        if type(part.get(name)) is not int or part[name] != expected:
            raise ValueError("backend range differs from the generated source")
    # Reference firmware signs init, not PUT. Server-issued headers take precedence.
    headers = part.get("headers", part.get("uploadHeaders", result.get("uploadHeaders")))
    if headers is None:
        headers = {"Content-Type": "application/octet-stream"}
    request = dict(url=part.get("url"), offset=0, length=total, headers=headers)
    p3_put_probe.prepare(request)  # Refuse unsupported metadata BEFORE producing commands.
    return request, "PENDING_FIRST_PART"


def write_json(path, value):
    with path.open("x", encoding="utf-8") as output:
        json.dump(value, output, ensure_ascii=False, indent=2)
        output.write("\n")


def load_state(directory):
    state = json.loads((directory / "state.json").read_text(encoding="utf-8"))
    if state.get("origin") != ORIGIN or state.get("source") != "generated-not-audio":
        raise ValueError("state is not from this direct test")
    payload = state["payload"]
    expected, digest = generated_metadata(payload["fileSize"], payload["localFileId"], payload["recordedAt"])
    if payload != expected or state.get("sha256") != digest:
        raise ValueError("source metadata was changed; refusing to resume")
    return state


def session_result(status, raw):
    obj = json.loads(raw)
    if not 200 <= status < 300 or not isinstance(obj, dict) or obj.get("status") != "00000":
        raise ValueError("backend rejected session request; inspect private response")
    result = obj.get("result")
    if not isinstance(result, dict):
        raise ValueError("missing session result")
    return result


def complete_preflight(result, identity, size):
    if not identity.get("syncSessionId") or result.get("syncSessionId") != identity["syncSessionId"]:
        raise ValueError("session identity changed; refusing complete")
    for field in ("uploadedBytes", "contiguousUploadedBytes"):
        if type(result.get(field)) is not int or result[field] != size:
            raise ValueError("server has not confirmed every byte")
    if result.get("syncStatus") == "SYNC_SUCCESS":
        return None
    if (result.get("syncStatus") != "SYNCING" or result.get("pendingParts") != [] or
            not identity.get("multipartUploadId") or
            result.get("multipartUploadId") != identity["multipartUploadId"]):
        raise ValueError("incomplete or changed multipart session")
    return {k: identity[k] for k in ("syncSessionId", "multipartUploadId")}


def complete_upload(directory, key, identity, ca_file=None):
    """Reconcile once, then submit complete once. Never retry an ambiguous POST."""
    state = load_state(directory)
    evidence = directory / ("complete-" + str(time.time_ns()))
    evidence.mkdir()
    status, raw = request_init(state["imei"], key, state["payload"], ca_file)
    (evidence / "preflight-init.json").write_bytes(raw)
    write_json(evidence / "preflight-http.json", {"http": status})
    body = complete_preflight(session_result(status, raw), identity, state["payload"]["fileSize"])
    if body is None:
        print("SYNC_SUCCESS already confirmed; no PUT or complete sent.")
        return
    write_json(evidence / "request.json", body)
    status, raw = request_post(COMPLETE_PATH, state["imei"], key, body, ca_file)
    (evidence / "complete-response.json").write_bytes(raw)
    write_json(evidence / "http.json", {"http": status})
    result = session_result(status, raw)
    if result.get("syncSessionId") != identity["syncSessionId"] or result.get("syncStatus") != "SYNC_SUCCESS":
        raise ValueError("complete did not confirm this session; reconcile before retry")
    for field in ("uploadedBytes", "contiguousUploadedBytes"):
        if type(result.get(field)) is not int or result[field] != state["payload"]["fileSize"]:
            raise ValueError("complete size differs; inspect private response")
    print("Complete HTTP={} business=00000 syncStatus=SYNC_SUCCESS bytes={}".format(
        status, state["payload"]["fileSize"]))
    print("Private completion evidence: " + str(evidence))


def saved_identity(directory):
    attempts = sorted(directory.glob("attempt-*/init-response.json"),
                      key=lambda p: int(p.parent.name.split("-")[1]), reverse=True)
    for path in attempts:
        http = json.loads((path.parent / "http.json").read_text(encoding="utf-8"))
        if not 200 <= http["http"] < 300:
            continue
        obj = json.loads(path.read_text(encoding="utf-8"))
        if obj.get("status") != "00000":
            continue
        result = obj.get("result", {})
        if result.get("syncSessionId"):
            return result
    raise ValueError("no saved session identity; refresh first")


def run(args):
    key = signing_key(args.secret_file, args.reference_project)
    if args.command == "complete":
        complete_upload(args.directory, key, saved_identity(args.directory), args.ca_file)
        return
    if args.command == "init" or (args.command == "upload" and not args.directory.exists()):
        if not args.imei:
            raise ValueError("a new test needs --imei")
        now = datetime.now(timezone.utc)
        local_id = args.local_id or now.astimezone(timezone(timedelta(hours=8))).strftime("%Y%m%d%H%M%S")
        payload, digest = generated_metadata(args.bytes, local_id, now.strftime("%Y-%m-%dT%H:%M:%SZ"))
        state = dict(origin=ORIGIN, imei=args.imei, source="generated-not-audio", payload=payload, sha256=digest)
        args.directory.mkdir(parents=True, exist_ok=False)
        write_json(args.directory / "state.json", state)
    else:
        state = load_state(args.directory)
        payload = state["payload"]
        if args.command == "upload" and ((args.imei and args.imei != state["imei"]) or
                (args.bytes is not None and args.bytes != payload["fileSize"]) or
                (args.local_id and args.local_id != payload["localFileId"])):
            raise ValueError("resume arguments differ from saved file identity")
    attempt = args.directory / ("attempt-" + str(time.time_ns()))
    attempt.mkdir()
    status, raw = request_init(state["imei"], key, payload, args.ca_file)
    (attempt / "init-response.json").write_bytes(raw)
    write_json(attempt / "http.json", {"http": status})
    print(f"Backend={ORIGIN} HTTP={status}; response saved locally")
    request, progress = prepare_part(status, raw, payload["fileSize"])
    print(f"State={progress} bytes={payload['fileSize']} expected_sha256={state['sha256']}")
    if request is not None:
        write_json(attempt / "put-request.json", request)
        commands, _ = p3_put_probe.prepare(request)
        (attempt / "board-commands.txt").write_text(commands, encoding="ascii")
        host = p3_put_probe.urlsplit(request["url"]).hostname
        print(f"PUT host={host} URL_bytes={len(request['url'])}; signed URL omitted")
        print("Board commands: " + str(attempt / "board-commands.txt"))
        if args.command == "upload":
            report = attempt / "board-result.json"
            command = [sys.executable, str(Path(__file__).with_name("p3_serial_send.py")),
                       "--commands", str(attempt / "board-commands.txt"),
                       "--port", args.port, "--baud", str(args.baud), "--result-json", str(report)]
            subprocess.run(command, check=True)
            proof = json.loads(report.read_text(encoding="utf-8"))
            if (proof.get("bytes") != payload["fileSize"] or proof.get("sha256") != state["sha256"] or
                    proof.get("ret") != 0 or not 200 <= proof.get("http", 0) < 300):
                raise ValueError("board did not confirm this upload; complete not sent")
    else:
        print("No pending PUT; no serial port opened.")
    if args.command == "upload":
        complete_upload(args.directory, key, session_result(status, raw), args.ca_file)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    probe = commands.add_parser("probe", help="read-only TLS 1.2/HEAD check of the reference backend")
    probe.add_argument("--ca-file", type=Path, help="PEM CA bundle")
    for name in ("init", "refresh", "complete", "upload"):
        command = commands.add_parser(name)
        command.add_argument("--directory", required=True, type=Path, help="private local session directory")
        command.add_argument("--ca-file", type=Path, help="PEM CA bundle (verification remains required)")
        keys = command.add_mutually_exclusive_group(required=True)
        keys.add_argument("--secret-file", type=Path, help="local business HTTP key, not OneNET device key")
        keys.add_argument("--reference-project", type=Path, help="read CLOUD_SIGN_SECRET from the specified ZS302A source")
        if name in ("init", "upload"):
            command.add_argument("--imei", required=name == "init")
            command.add_argument("--bytes", type=int, default=48001 if name == "init" else None)
            command.add_argument("--local-id", help="stable 14-digit localFileId; default current China time")
        if name == "upload":
            command.add_argument("--port", default="COM4")
            command.add_argument("--baud", type=int, required=True)
    args = parser.parse_args()
    try:
        if args.command == "probe":
            print(json.dumps(probe_backend(args.ca_file), ensure_ascii=True, indent=2))
        else:
            if args.command == "upload":
                if args.baud <= 0:
                    raise ValueError("invalid baud")
                if not args.directory.exists() and args.bytes is None:
                    args.bytes = 48001
            run(args)
    except (OSError, ValueError, KeyError, TypeError, http.client.HTTPException,
            subprocess.SubprocessError, KeyboardInterrupt) as error:
        # Never print raw exceptions: they may contain signed URLs or request data.
        parser.exit(1, f"Direct test failed ({type(error).__name__}); inspect local response/configuration.\n")


if __name__ == "__main__":
    main()
