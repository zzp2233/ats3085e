import base64
import contextlib
import io
import json
from pathlib import Path
import socket
import sys
import tempfile
import threading
import time
import unittest
from urllib.parse import parse_qs

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import onenet_device as device

KEY = base64.b64encode(b"offline-test-key-not-a-device-secret").decode()


class DeviceTests(unittest.TestCase):
    def config(self):
        return device.configuration(KEY, int(time.time()) + 3600)

    def test_token_known_vector_and_device_resource(self):
        fields = parse_qs(device.token(KEY, 1900000000))
        self.assertEqual(fields["res"], ["products/S1U4S78Iya/devices/868504079888857"])
        # Independently checked with System.Security.Cryptography.HMACSHA1.
        self.assertEqual(fields["sign"], ["rAteUkZ9swhjNrkWoeOYt+dCBhg="])

    def test_reject_bad_key_expiry_identity_signature(self):
        for key in ("not base64!", ""):
            with self.assertRaises(ValueError): device.token(key, 1900000000)
        for key, value in (("expires", 1), ("device", "different"), ("tls", "false"),
                           ("port", 8883), ("password", "version=2018-10-31")):
            config = self.config(); config[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError): device.validate(config)

    def test_output_contains_token_not_key_and_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "onenet.json"
            config = self.config()
            device.write_private(path, config)
            self.assertNotIn(KEY, path.read_text())
            self.assertEqual(json.loads(path.read_text()), config)
            with self.assertRaises(FileExistsError): device.write_private(path, config)

    def exchange(self, connack=0, suback=0, code=200, stale=False, retained=False):
        client, server = socket.socketpair()
        errors = []
        def broker():
            try:
                deadline = time.monotonic() + 3
                kind, data = device.receive(server, deadline)
                self.assertEqual(kind, 0x10)
                self.assertIn(device.PRODUCT.encode(), data)
                # Deliberately fragment CONNACK over multiple writes.
                server.sendall(b"\x20"); server.sendall(bytes((2, 0, connack)))
                if connack: return
                kind, data = device.receive(server, deadline)
                self.assertEqual(kind, 0x82)
                server.sendall(device.packet(0x90, bytes((0, 1, suback))))
                if suback: return
                kind, data = device.receive(server, deadline)
                self.assertEqual(kind, 0x30)
                n = int.from_bytes(data[:2], "big")
                topic = data[2:2+n] + b"/reply"
                post = json.loads(data[2+n:])
                self.assertEqual(post["params"], {"record_state": {"value": 0}})
                def reply(identifier, result, flags=0x30):
                    server.sendall(device.packet(flags, device.mqtt_string(topic) +
                        json.dumps(dict(id=identifier, code=result)).encode()))
                if stale: reply("old-id", 200)
                if retained: reply(post["id"], 400, 0x31)
                reply(post["id"], code)
                if code == 200:
                    self.assertEqual(device.receive(server, deadline), (0xe0, b""))
            except BaseException as error: errors.append(error)
            finally: server.close()
        thread = threading.Thread(target=broker)
        thread.start()
        try:
            with contextlib.redirect_stdout(io.StringIO()):
                return device.probe(self.config(), timeout=3, connector=lambda *a, **k: client)
        finally:
            thread.join(4)
            self.assertFalse(thread.is_alive())
            if errors: raise errors[0]

    def test_full_exchange(self): self.assertEqual(self.exchange(), 200)
    def test_stale_and_retained_are_not_success(self):
        self.assertEqual(self.exchange(stale=True, retained=True), 200)
    def test_login_failure(self):
        with self.assertRaisesRegex(ConnectionError, "CONNACK=5"): self.exchange(connack=5)
    def test_subscription_failure(self):
        with self.assertRaises(ConnectionError): self.exchange(suback=128)
    def test_property_failure(self):
        with self.assertRaisesRegex(RuntimeError, "code=400"): self.exchange(code=400)
    def test_malformed_reply(self):
        for payload in (b'[]', b'{"id":"x","code":true}', b'{"id":"x","code":"200"}'):
            if payload == b'[]': self.assertIsNone(device.reply_code(payload, "x"))
            else:
                with self.assertRaises(ValueError): device.reply_code(payload, "x")
    def test_wire_bounds(self):
        for data in (b"\x30\x80\x80\x80\x80", b"\x30\xff\x7f", b"\x30\x05ab"):
            client, server = socket.socketpair()
            try:
                server.sendall(data); server.close()
                with self.assertRaises((ValueError, ConnectionError)):
                    device.receive(client, time.monotonic() + 1)
            finally: client.close(); server.close()


if __name__ == "__main__": unittest.main(verbosity=2)
