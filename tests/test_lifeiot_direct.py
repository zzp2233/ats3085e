import hashlib
import hmac
import json
from pathlib import Path
import ssl
import struct
import sys
import tempfile
import unittest
import subprocess
from argparse import Namespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import lifeiot_direct as direct


class DirectTests(unittest.TestCase):
    def ready(self):
        return dict(syncStatus='SYNCING', syncSessionId='test-session', multipartUploadId='test-upload',
                    uploadedBytes=48001, contiguousUploadedBytes=48001, pendingParts=[])

    def state_dir(self, root):
        metadata, digest = direct.generated_metadata(48001, '20261120180000', '2026-11-20T10:00:00Z')
        direct.write_json(root / 'state.json', dict(origin=direct.ORIGIN, imei='868504079888857',
                          source='generated-not-audio', payload=metadata, sha256=digest))

    def test_complete_signature_uses_complete_path(self):
        headers = direct.signed_headers('868504079888857', b'offline', b'{}', timestamp=1,
                                        nonce='n', path=direct.COMPLETE_PATH)
        expected = hmac.new(b'offline', ('POST\n' + direct.COMPLETE_PATH +
            '\n868504079888857\n1\nn\n' + hashlib.sha256(b'{}').hexdigest()).encode(), hashlib.sha256).hexdigest()
        self.assertEqual(headers['X-Device-Signature'], expected)

    def test_complete_rejects_pending_short_changed_or_unknown_sessions(self):
        identity = self.ready()
        for change in ({'pendingParts': [{}]}, {'uploadedBytes': 48000},
                       {'contiguousUploadedBytes': 48000}, {'uploadedBytes': True},
                       {'syncSessionId': 'other'}, {'multipartUploadId': 'other'},
                       {'syncStatus': 'FAILED'}, {'pendingParts': None}):
            with self.subTest(change=change), self.assertRaises(ValueError):
                direct.complete_preflight(dict(identity, **change), identity, 48001)

    def test_complete_preflights_and_posts_only_ids(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.state_dir(root)
            ready = self.ready()
            done = dict(ready, syncStatus='SYNC_SUCCESS')
            with patch.object(direct, 'request_init', return_value=(200, json.dumps({'status': '00000', 'result': ready}))) as init, \
                 patch.object(direct, 'request_post', return_value=(200, json.dumps({'status': '00000', 'result': done}).encode())) as post:
                # Production request_init returns bytes, keep persisted response realistic.
                init.return_value = (200, json.dumps({'status': '00000', 'result': ready}).encode())
                direct.complete_upload(root, b'offline', ready)
                self.assertEqual(post.call_count, 1)
                self.assertEqual(post.call_args.args[0], direct.COMPLETE_PATH)
                self.assertEqual(post.call_args.args[3], {'syncSessionId': 'test-session', 'multipartUploadId': 'test-upload'})
                init.return_value = (200, json.dumps({'status': '00000', 'result': done}).encode())
                direct.complete_upload(root, b'offline', ready)
                self.assertEqual(post.call_count, 1, 'already complete must not POST twice')

    def test_complete_failure_and_timeout_never_retry(self):
        for reply in ((403, b'{}'), (200, b'{"status":"FAIL"}'),
                      (200, b'{"status":"00000","result":{"syncStatus":"SYNCING"}}'), TimeoutError()):
            with tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                self.state_dir(root)
                with patch.object(direct, 'request_init', return_value=(200, json.dumps({'status': '00000', 'result': self.ready()}).encode())), \
                     patch.object(direct, 'request_post') as post:
                    if isinstance(reply, Exception):
                        post.side_effect = reply
                    else:
                        post.return_value = reply
                    with self.assertRaises((ValueError, TimeoutError)):
                        direct.complete_upload(root, b'offline', self.ready())
                    self.assertEqual(post.call_count, 1)

    def upload_args(self, root):
        return Namespace(command='upload', directory=root, secret_file=None, reference_project=None,
                         ca_file=None, imei=None, bytes=None, local_id=None, port='COM4', baud=2000000)

    def test_upload_failure_never_calls_complete(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.state_dir(root)
            with patch.object(direct, 'signing_key', return_value=b'offline'), \
                 patch.object(direct, 'request_init', return_value=(200, json.dumps(self.response()).encode())), \
                 patch.object(direct.subprocess, 'run', side_effect=subprocess.CalledProcessError(2, 'serial')), \
                 patch.object(direct, 'complete_upload') as complete:
                with self.assertRaises(subprocess.CalledProcessError):
                    direct.run(self.upload_args(root))
                complete.assert_not_called()

    def test_upload_success_completes_same_session_and_no_pending_skips_serial(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.state_dir(root)
            def serial(command, check):
                self.assertTrue(check)
                report = Path(command[command.index('--result-json') + 1])
                state = direct.load_state(root)
                direct.write_json(report, dict(bytes=48001, sha256=state['sha256'], ret=0, http=200))
            with patch.object(direct, 'signing_key', return_value=b'offline'), \
                 patch.object(direct, 'request_init', return_value=(200, json.dumps(self.response()).encode())) as init, \
                 patch.object(direct.subprocess, 'run', side_effect=serial) as send, \
                 patch.object(direct, 'complete_upload') as complete:
                direct.run(self.upload_args(root))
                self.assertEqual(send.call_count, 1)
                self.assertEqual(complete.call_args.args[2]['syncSessionId'], 'test-session')
                init.return_value = (200, json.dumps({'status': '00000', 'result': self.ready()}).encode())
                direct.run(self.upload_args(root))
                self.assertEqual(send.call_count, 1)
                self.assertEqual(complete.call_count, 2)

    def response(self):
        return {'status': '00000', 'result': {
            'syncStatus': 'SYNCING', 'syncSessionId': 'test-session',
            'multipartUploadId': 'test-upload', 'pendingParts': [
                {'partNumber': 1, 'offset': 0, 'length': 48001,
                 'url': 'https://storage.example.test/p?Signature=a%2Fb%2B'}]}}

    def test_signing_matches_reference_recipe(self):
        body = b'{"localFileId":"20261120180000"}'
        headers = direct.signed_headers('868504079888857', b'offline-test-key', body,
                                        timestamp=1795168800000, nonce='unique-test-nonce')
        canonical = (b'POST\n/voice-card/device/v1/oss/audio-sync/multipart/init\n'
                     b'868504079888857\n1795168800000\nunique-test-nonce\n' +
                     hashlib.sha256(body).hexdigest().encode())
        expected = hmac.new(b'offline-test-key', canonical, hashlib.sha256).hexdigest()
        self.assertEqual(headers['X-Device-Signature'], expected)
        self.assertEqual(headers['X-Device-Timestamp'], '1795168800000')
        self.assertEqual(headers['Content-Type'], 'application/json')

    def test_sample_md5_matches_reference_small_and_cross_group_sources(self):
        for size in (1, 99, 100, 101, 199, 200, 201, 48001):
            with self.subTest(size=size):
                data = bytes(((i // 480) ^ ((i // 480) >> 8) ^ ((i % 480) * 29)) & 255
                             for i in range(size))
                sample = data if size <= 200 else data[:100] + data[-100:]
                metadata, digest = direct.generated_metadata(size, '20261120180000', '2026-11-20T10:00:00Z')
                self.assertEqual(metadata['fileMd5'], hashlib.md5(sample + struct.pack('<I', size)).hexdigest())
                self.assertEqual(digest, hashlib.sha256(data).hexdigest())
                self.assertEqual(metadata['uploadSignVersion'], 'V2')
                self.assertEqual(metadata['audioFormat'], 'bin')
                self.assertNotIn('imei', metadata)
        for size in (True, 0, -1, direct.PART_SIZE + 1):
            with self.assertRaises(ValueError):
                direct.generated_metadata(size, '20261120180000', '')

    def test_valid_first_part_preserves_url_and_headers(self):
        response = self.response()
        part = response['result']['pendingParts'][0]
        part['headers'] = {'Content-Type': 'application/octet-stream', 'X-Signed': 'exact'}
        request, progress = direct.prepare_part(200, json.dumps(response), 48001)
        self.assertEqual(progress, 'PENDING_FIRST_PART')
        self.assertEqual(request['url'], part['url'])
        self.assertEqual(request['headers'], part['headers'])

    def test_auth_failures_never_produce_put_commands(self):
        for code, body in ((401, {'status': 'A0002'}), (200, {'status': 'A0002'}),
                           (500, self.response()), (200, []), (200, {})):
            with self.assertRaises(ValueError):
                direct.prepare_part(code, json.dumps(body), 48001)

    def test_rejects_wrong_ranges_plain_http_and_oversized_metadata(self):
        for mutation in ({'offset': 1}, {'offset': False}, {'length': 48000},
                         {'partNumber': 2}, {'partNumber': True},
                         {'url': 'http://storage.example.test/p'},
                         {'url': 'https://storage.example.test/' + 'x' * 3072},
                         {'headers': {'Content-Length': '48001'}}):
            response = self.response()
            response['result']['pendingParts'][0].update(mutation)
            with self.assertRaises(ValueError):
                direct.prepare_part(200, json.dumps(response), 48001)
        response = self.response()
        response['result']['pendingParts'] *= 2
        with self.assertRaises(ValueError):
            direct.prepare_part(200, json.dumps(response), 48001)

    def test_empty_pending_or_already_complete_does_not_reupload(self):
        response = self.response()
        response['result']['pendingParts'] = []
        self.assertEqual(direct.prepare_part(200, json.dumps(response), 48001), (None, 'NO_PENDING_PARTS'))
        response['result']['syncStatus'] = 'SYNC_SUCCESS'
        self.assertEqual(direct.prepare_part(200, json.dumps(response), 48001), (None, 'SYNC_SUCCESS'))

    def test_transport_is_exact_backend_and_tls_verification_is_required(self):
        context = direct.tls_context()
        self.assertTrue(context.check_hostname)
        self.assertEqual(context.verify_mode, ssl.CERT_REQUIRED)
        self.assertEqual(context.minimum_version, ssl.TLSVersion.TLSv1_2)
        self.assertEqual(context.maximum_version, ssl.TLSVersion.TLSv1_2)
        with patch.object(direct.http.client, 'HTTPSConnection') as connection:
            reply = connection.return_value.getresponse.return_value
            reply.status, reply.read.return_value = 401, b'{"status":"A0002"}'
            metadata, _ = direct.generated_metadata(48001, '20261120180000', '2026-11-20T10:00:00Z')
            code, raw = direct.request_init('868504079888857', b'offline-key', metadata)
            self.assertEqual((code, raw), (401, b'{"status":"A0002"}'))
            self.assertEqual(connection.call_args.args, ('lifeiot.cmaiot.cn', 8081))
            args = connection.return_value.request.call_args.args
            self.assertEqual(args[:2], ('POST', direct.INIT_PATH))
            self.assertEqual(json.loads(args[2]), metadata)
            connection.return_value.close.assert_called_once()

    def test_reference_key_is_loaded_without_executing_source(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            header = root / 'application/bt_earphone/src/onenet/cloud_storage.h'
            header.parent.mkdir(parents=True)
            header.write_text('#define CLOUD_SIGN_SECRET "offline-test-key"\n', encoding='utf-8')
            self.assertEqual(direct.signing_key(reference_project=root), b'offline-test-key')
            header.write_text('#define CLOUD_SIGN_SECRET "a"\n#define CLOUD_SIGN_SECRET "b"\n', encoding='utf-8')
            with self.assertRaises(ValueError):
                direct.signing_key(reference_project=root)


if __name__ == '__main__':
    unittest.main()
