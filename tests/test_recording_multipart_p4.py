# 本文件使用本机回环 HTTP 服务验证录音分片上传，包括服务端签名检查、预签名 PUT、断点续传及响应丢失后的对账。
"""运行命令：python -m unittest discover -s tests -p test_recording_multipart_p4.py"""
from contextlib import contextmanager
import hashlib
import hmac
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from io import BytesIO
import json
from tempfile import TemporaryFile
from threading import Thread
import time
import unittest
from urllib.parse import urlsplit

from recording_multipart_p4 import (
    COMPLETE, INIT, Recording, UploadConfig, UploadError, device_signature,
    loopback_transport, upload,
)


# 用内存记录模拟服务端的已存分片和请求，仅本地测试使用固定设备号和密钥。
class TestCloud:
    imei = "000000000000001"
    secret = b"offline-fixture-only"

    # 保存原始录音、分片大小、已用 nonce 和故障开关，以便测试重建每次服务端状态。
    def __init__(self, data, part_size):
        self.data = data
        self.part_size = part_size
        self.done = False
        self.requests = []
        self.nonces = set()
        self.parts = {}
        self.current_url = None
        self.reject_next_put = False
        self.lose_complete_response = False
        self.lose_put_response = False
        self.invalid_range = False

    # 验证设备标识、时间窗、nonce 和 HMAC；签名通过后才登记 nonce，防止重复使用。
    def signed(self, method, path, headers, body):
        imei = headers.get("X-Device-Imei")
        timestamp = headers.get("X-Device-Timestamp", "")
        nonce = headers.get("X-Device-Nonce")
        signature = headers.get("X-Device-Signature", "")
        if (imei != self.imei or not timestamp.isdecimal()
                or abs(int(timestamp) - time.time_ns() // 1_000_000) > 60_000
                or not nonce or nonce in self.nonces):
            return False
        expected = device_signature(method, path, imei, timestamp, nonce, body, self.secret)
        if not hmac.compare_digest(signature, expected):
            return False
        self.nonces.add(nonce)
        return True

    # 模拟 init，根据已存分片返回待上传范围和签名地址；已完成的录音直接返回成功状态。
    def init(self, obj):
        expected = Recording(BytesIO(self.data))
        if (obj.get("fileSize") != expected.size or obj.get("fileMd5") != expected.md5
                or obj.get("uploadSignVersion") != "V2" or obj.get("partSize") != self.part_size
                or obj.get("localFileId") != "p4-recording"):
            return 400, {}
        if self.done:
            return 200, {"status": "00000", "result": {"syncStatus": "SYNC_SUCCESS"}}
        parts = []
        for n, offset in enumerate(range(0, len(self.data), self.part_size), 1):
            if n in self.parts:
                continue
            length = min(self.part_size, len(self.data) - offset)
            # 查询串含已转义的分隔符，重组 URL 会改变参与比较的请求目标。
            target = "/test-objects/{}?token=fixture%2B{}%2F%3D".format(n, len(self.requests))
            parts.append(dict(partNumber=n, offset=offset, length=length, url=self.origin + target))
            if len(parts) == 1:
                self.current_url = target
        if self.invalid_range and parts:
            parts[0]["offset"] = len(self.data)
        return 200, {"status": "00000", "result": {
            "syncStatus": "SYNCING", "syncSessionId": "session-fixture",
            "multipartUploadId": "upload-fixture", "pendingParts": parts,
        }}

    # 模拟对象存储 PUT，核对当前允许的完整请求目标、内容类型及对应范围的原始字节。
    def put(self, target, headers, body):
        number = int(urlsplit(target).path.rsplit("/", 1)[-1])
        if (target != self.current_url or self.reject_next_put
                or headers.get("Content-Type") != "application/octet-stream"
                or any(name.lower().startswith("x-device-") for name in headers.keys())):
            self.reject_next_put = False
            return 403, {"code": "SignatureDoesNotMatch"}
        offset = (number - 1) * self.part_size
        if body != self.data[offset:offset + self.part_size]:
            return 422, {"code": "WrongPartBytes"}
        self.parts[number] = body
        if self.lose_put_response:
            self.lose_put_response = False
            return None, None
        return 200, {}

    # 校验两个会话标识并按顺序还原整份录音；可在成功落账后模拟丢失完成响应。
    def complete(self, obj):
        if obj != {"syncSessionId": "session-fixture", "multipartUploadId": "upload-fixture"}:
            return 400, {}
        combined = b"".join(self.parts[n] for n in sorted(self.parts))
        if combined != self.data:
            return 409, {}
        self.done = True
        if self.lose_complete_response:
            self.lose_complete_response = False
            return None, None
        return 200, {"status": "00000", "result": {
            "syncStatus": "SYNC_SUCCESS", "sha256": hashlib.sha256(combined).hexdigest()}}


# 为一个测试启动回环 HTTP 服务；上下文退出时关闭监听并回收线程。
@contextmanager
def serve(cloud):
    class Handler(BaseHTTPRequestHandler):
        # 抑制本地 HTTP 服务的访问日志，测试通过 cloud.requests 断言实际请求。
        def log_message(self, *_):
            pass

        # 按方法及路径分发本地请求，将测试服务的处理结果写回，或按故障注入设置模拟无响应。
        def handle_request(self):
            body = self.rfile.read(int(self.headers["Content-Length"]))
            path = urlsplit(self.path).path
            if self.command == "POST":
                cloud.requests.append((path, len(body)))
                if not cloud.signed("POST", path, self.headers, body):
                    code, obj = 401, {}
                elif path == INIT:
                    code, obj = cloud.init(json.loads(body))
                elif path == COMPLETE:
                    code, obj = cloud.complete(json.loads(body))
                else:
                    code, obj = 404, {}
            else:
                cloud.requests.append((self.path, len(body)))
                code, obj = cloud.put(self.path, self.headers, body)
            if code is not None:
                payload = json.dumps(obj).encode()
                self.send_response(code)
                self.send_header("Content-Length", str(len(payload)))
                self.end_headers()
                self.wfile.write(payload)

        do_POST = handle_request
        do_PUT = handle_request

    with ThreadingHTTPServer(("127.0.0.1", 0), Handler) as server:
        cloud.origin = "http://127.0.0.1:{}".format(server.server_port)
        thread = Thread(target=server.serve_forever, kwargs={"poll_interval": 0.02})
        thread.start()
        try:
            yield UploadConfig(cloud.origin, cloud.imei, cloud.secret)
        finally:
            server.shutdown()
            thread.join()


# 各用例核对客户端、对象存储和业务确认三处进度，验证恢复过程中的请求次数与内容。
class MultipartTests(unittest.TestCase):
    data = bytes(range(251)) * 400
    part_size = 48001

    # 用回环传输驱动真实的上传逻辑，使每个用例都经过序列化、HTTP 请求和响应解析。
    def run_upload(self, cloud, config=None):
        with TemporaryFile() as source, serve(cloud) as local:
            source.write(self.data)
            return upload(config(local) if config else local, loopback_transport,
                          Recording(source), "p4-recording",
                          "recording.msbc", 1000, part_size=self.part_size)

    # 跨越 48,000 字节边界，确认收到的录音可从各个上传分片按序还原。
    def test_normal_upload_crosses_48kb_boundary(self):
        cloud = TestCloud(self.data, self.part_size)
        result = self.run_upload(cloud)
        self.assertEqual(result["sha256"], hashlib.sha256(self.data).hexdigest())
        self.assertEqual([n for path, n in cloud.requests if path.startswith("/test-objects/")],
                         [48001, 48001, len(self.data) - 96002])
        self.assertEqual(cloud.requests[-1][0], COMPLETE)

    # 对象存储拒绝旧 URL 后，重新 init 获取新签名地址。
    def test_403_reinit_uses_new_signed_target(self):
        cloud = TestCloud(self.data, self.part_size)
        cloud.reject_next_put = True
        self.run_upload(cloud)
        paths = [p for p, _ in cloud.requests if p.startswith("/test-objects/1?")]
        self.assertEqual(len(paths), 2)
        self.assertNotEqual(paths[0], paths[1])
        self.assertEqual(cloud.requests[2][0], INIT)

    # 预存已完成的分片，检查断点续传只请求其余部分。
    def test_resume_skips_already_stored_part(self):
        cloud = TestCloud(self.data, self.part_size)
        cloud.parts[1] = self.data[:self.part_size]
        self.run_upload(cloud)
        self.assertFalse(any(p.startswith("/test-objects/1?") for p, _ in cloud.requests))
        self.assertTrue(cloud.done)

    # 服务端已存 PUT 但客户端没拿到响应，重新查询后跳过这份已存分片。
    def test_lost_put_response_reconciles_without_resending_stored_part(self):
        cloud = TestCloud(self.data, self.part_size)
        cloud.lose_put_response = True
        self.run_upload(cloud)
        paths = [p for p, _ in cloud.requests if p.startswith("/test-objects/1?")]
        self.assertEqual(len(paths), 1)
        self.assertEqual(cloud.requests[2][0], INIT)

    # complete 在服务端生效后丢失回包，验证客户端能用 init 确认最终状态。
    def test_lost_complete_response_reconciles_with_init(self):
        cloud = TestCloud(self.data, self.part_size)
        cloud.lose_complete_response = True
        result = self.run_upload(cloud)
        self.assertEqual(result["syncStatus"], "SYNC_SUCCESS")
        self.assertEqual([p for p, _ in cloud.requests][-2:], [COMPLETE, INIT])

    # 错误范围和签名应在数据进入对象存储前导致失败。
    def test_invalid_range_and_bad_signature_stop_before_put(self):
        cloud = TestCloud(self.data, self.part_size)
        cloud.invalid_range = True
        with self.assertRaisesRegex(UploadError, "invalid pending part"):
            self.run_upload(cloud)
        with self.assertRaisesRegex(UploadError, "401"):
            self.run_upload(cloud, lambda cfg: UploadConfig(cfg.origin, cfg.imei, b"wrong"))
        self.assertTrue(all(path == INIT for path, _ in cloud.requests))

    # 用独立计算的签名向量验证签名输入的字节顺序和编码。
    def test_signature_matches_independent_canonical_vector(self):
        # 对照值用 .NET 的 SHA256 和 HMACSHA256 独立计算。
        self.assertEqual(
            device_signature("POST", INIT, TestCloud.imei, "1795069807000", "fixture-nonce",
                             b'{"localFileId":"fixture-01"}', TestCloud.secret),
            "a2f2d176acf089f29d8d09a4911c8a53b97fdf36345aee568facb5942dac20f2")

    # 分别检查不同录音长度下，采样 MD5 使用的首尾数据和长度。
    def test_sample_md5_matches_head_tail_and_length(self):
        data = bytes(range(256))
        for size in (1, 100, 101, 200, 256):
            expected = data[:size] if size <= 200 else data[:100] + data[size-100:size]
            expected += size.to_bytes(4, "little")
            self.assertEqual(Recording(BytesIO(data[:size])).md5, hashlib.md5(expected).hexdigest())

    # 回环测试适配器拒绝外部端点，保证测试请求只发往本机。
    def test_loopback_transport_rejects_external_endpoint(self):
        with self.assertRaisesRegex(UploadError, "127.0.0.1"):
            loopback_transport("PUT", "https://example.invalid", {}, (), 0)
