"""Local OneNET device credentials and MQTT 3.1.1 bring-up (standard library only).

Never prints the device key, token, or received payload. Run --help for commands.
"""
import argparse
import base64
import getpass
import hashlib
import hmac
import json
import os
from pathlib import Path
import socket
import ssl
import struct
import sys
import time
from urllib.parse import quote, parse_qs

PRODUCT = "S1U4S78Iya"
DEVICE = "868504079888857"
HOSTS = ("mqtts.heclouds.com", PRODUCT + ".mqtts.acc.cmcconenet.cn")
DEFAULT_CONFIG = Path(__file__).resolve().parents[1] / ".local/onenet/onenet.json"


def token(secret, expires):
    key = base64.b64decode(secret.strip(), validate=True)
    if not key or len(key) > 256:
        raise ValueError("Invalid device key length")
    resource = "products/{}/devices/{}".format(PRODUCT, DEVICE)
    signed = "{}\nsha1\n{}\n2018-10-31".format(expires, resource)
    signature = base64.b64encode(hmac.new(key, signed.encode(), hashlib.sha1).digest()).decode()
    return "version=2018-10-31&res={}&et={}&method=sha1&sign={}".format(
        quote(resource, safe=""), expires, quote(signature, safe=""))


def configuration(secret, expires, host=HOSTS[0], tls=False):
    return dict(schema=1, product=PRODUCT, device=DEVICE, host=host,
                port=8883 if tls else 1883, tls=tls, expires=expires,
                password=token(secret, expires))


def validate(config):
    if (config.get("schema") != 1 or config.get("product") != PRODUCT or
            config.get("device") != DEVICE or config.get("host") not in HOSTS or
            type(config.get("tls")) is not bool or
            config.get("port") != (8883 if config["tls"] else 1883)):
        raise ValueError("Invalid device configuration")
    expires = config.get("expires")
    if type(expires) is not int or not int(time.time()) < expires <= 0xffffffff:
        raise ValueError("Expired/invalid token; generate a new configuration")
    password = config.get("password", "")
    if not isinstance(password, str) or not 1 <= len(password) < 512:
        raise ValueError("Invalid token")
    fields = parse_qs(password, strict_parsing=True)
    expected = dict(version=["2018-10-31"],
                    res=["products/{}/devices/{}".format(PRODUCT, DEVICE)],
                    et=[str(expires)], method=["sha1"])
    if set(fields) != set(expected) | {"sign"} or any(fields[k] != v for k, v in expected.items()):
        raise ValueError("Token identity/expiry does not match configuration")
    if len(fields["sign"]) != 1 or len(base64.b64decode(fields["sign"][0], validate=True)) != 20:
        raise ValueError("Invalid token signature encoding")
    return config


def write_private(path, config):
    path.parent.mkdir(parents=True, exist_ok=True)
    # Refuse overwrites; retain earlier credentials until the user replaces them.
    fd = os.open(str(path), os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as stream:
        json.dump(config, stream, separators=(",", ":"))
        stream.write("\n")


def mqtt_string(value):
    value = value.encode() if isinstance(value, str) else value
    return struct.pack("!H", len(value)) + value


def packet(kind, body=b""):
    remaining = len(body)
    encoded = bytearray([kind])
    while True:
        digit = remaining % 128
        remaining //= 128
        encoded.append(digit | (128 if remaining else 0))
        if not remaining:
            return bytes(encoded) + body


def receive(sock, deadline):
    def exact(length):
        data = bytearray()
        while len(data) < length:
            left = deadline - time.monotonic()
            if left <= 0:
                raise TimeoutError("MQTT response timeout")
            sock.settimeout(left)
            piece = sock.recv(length - len(data))
            if not piece:
                raise ConnectionError("MQTT connection closed")
            data.extend(piece)
        return bytes(data)
    kind = exact(1)[0]
    size = 0
    for shift in range(0, 28, 7):
        digit = exact(1)[0]
        size += (digit & 127) << shift
        if size > 8192:
            raise ValueError("MQTT packet exceeds bring-up buffer")
        if not digit & 128:
            return kind, exact(size)
    raise ValueError("Malformed MQTT remaining length")


def reply_code(payload, message_id):
    value = json.loads(payload)
    if not isinstance(value, dict) or value.get("id") != message_id:
        return None
    code = value.get("code")
    if type(code) is not int:
        raise ValueError("Invalid property reply code")
    return code


def probe(config, timeout=30, connector=socket.create_connection):
    validate(config)
    root = "$sys/{}/{}/thing/property/post".format(PRODUCT, DEVICE)
    message_id = str(time.time_ns() // 1000000)
    # Omit time so the platform uses receipt time; never reuse the old log timestamp.
    payload = json.dumps(dict(id=message_id, version="1.0",
                              params=dict(record_state=dict(value=0))), separators=(",", ":")).encode()
    sock = connector((config["host"], config["port"]), timeout=timeout)
    try:
        if config["tls"]:
            sock = ssl.create_default_context().wrap_socket(sock, server_hostname=config["host"])
        deadline = time.monotonic() + timeout
        connect = mqtt_string("MQTT") + b"\x04\xc2\x00\x3c"
        connect += mqtt_string(DEVICE) + mqtt_string(PRODUCT) + mqtt_string(config["password"])
        sock.sendall(packet(0x10, connect))
        kind, body = receive(sock, deadline)
        if kind != 0x20 or len(body) != 2 or body[0] != 0:
            raise ValueError("Invalid MQTT CONNACK")
        if body[1]:
            raise ConnectionError("MQTT login rejected: CONNACK={}".format(body[1]))
        print("MQTT CONNECT accepted")
        sock.sendall(packet(0x82, b"\x00\x01" + mqtt_string(root + "/reply") + b"\x00"))
        kind, body = receive(sock, deadline)
        if kind != 0x90 or body != b"\x00\x01\x00":
            raise ConnectionError("MQTT subscription rejected or unexpected SUBACK")
        sock.sendall(packet(0x30, mqtt_string(root) + payload))
        while True:
            kind, body = receive(sock, deadline)
            if kind >> 4 != 3:
                raise ValueError("Unexpected MQTT packet")
            if len(body) < 2:
                raise ValueError("Truncated MQTT PUBLISH")
            length = int.from_bytes(body[:2], "big")
            if len(body) < length + 2:
                raise ValueError("Truncated MQTT topic")
            topic, content = body[2:2 + length], body[2 + length:]
            qos = (kind >> 1) & 3
            if qos == 1 and len(content) >= 2:
                sock.sendall(packet(0x40, content[:2]))
                content = content[2:]
            elif qos != 0:
                raise ValueError("Unsupported/truncated reply QoS")
            if kind & 1 or topic != (root + "/reply").encode():
                continue
            code = reply_code(content, message_id)
            if code is None:
                continue
            if code != 200:
                raise RuntimeError("OneNET property rejected: code={}".format(code))
            print("PASS: matching property reply id={} code=200".format(message_id))
            sock.sendall(packet(0xe0))
            return code
    finally:
        sock.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    create = commands.add_parser("create", help="Generate local token; hidden key prompt by default")
    create.add_argument("--key-file", type=Path)
    create.add_argument("--out", type=Path, default=DEFAULT_CONFIG)
    create.add_argument("--hours", type=int, default=24)
    create.add_argument("--host", choices=HOSTS, default=HOSTS[0])
    create.add_argument("--tls", action="store_true", help="PC TLS 8883; current 503 SDK MQTT cannot use this config")
    check = commands.add_parser("probe", help="Log in and post record_state=0; closes connection afterward")
    check.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    args = parser.parse_args()
    try:
        if args.command == "create":
            if not 1 <= args.hours <= 168:
                raise ValueError("Token lifetime must be 1..168 hours")
            secret = args.key_file.read_text(encoding="utf-8-sig").strip() if args.key_file else getpass.getpass("Device key (hidden): ")
            config = configuration(secret, int(time.time()) + args.hours * 3600, args.host, args.tls)
            validate(config)
            write_private(args.out, config)
            print("Saved local token configuration: {}".format(args.out))
            print("Expires (UTC): {}".format(time.strftime("%Y-%m-%d %H:%M:%S", time.gmtime(config["expires"]))))
        else:
            probe(json.loads(args.config.read_text(encoding="utf-8")))
        return 0
    except (OSError, ValueError, RuntimeError) as error:
        # Parsing/OS exceptions can contain source text. Do not echo their arguments.
        if type(error) in (ConnectionError, TimeoutError, RuntimeError):
            print("FAILED: {}".format(error), file=sys.stderr)
        else:
            print("FAILED ({}): check local input, expiry, network and file path; secrets omitted".format(type(error).__name__), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
