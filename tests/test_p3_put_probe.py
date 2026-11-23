import importlib.util
from pathlib import Path
import unittest
import zlib

spec = importlib.util.spec_from_file_location('p3_probe', Path(__file__).resolve().parents[1] / 'tools/p3_put_probe.py')
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


class ProbeTests(unittest.TestCase):
    def test_long_signed_url_headers_are_preserved_and_crc_matches(self):
        url = 'https://example.test/upload?signature=' + 'a%2Fb+' * 120
        text, digest = probe.prepare(dict(url=url, length=48001, headers={
            'Content-Type': 'application/octet-stream', 'X-Test': 'signed-value'}))
        lines = text.splitlines()
        self.assertTrue(all(len(line.encode('ascii')) + 2 < 64 for line in lines))
        data = bytearray()
        for line in lines[1:-1]:
            _, _, command, offset, chunk = line.split()
            self.assertEqual(command, 'data')
            self.assertEqual(int(offset), len(data))
            data.extend(bytes.fromhex(chunk))
        self.assertEqual(data, ('PUT\n' + url + '\nContent-Type: application/octet-stream\r\nX-Test: signed-value\r\n').encode())
        self.assertEqual(lines[0], f'app spi2putcfg begin {len(data)} {zlib.crc32(data):08x}')
        self.assertEqual(lines[-1], 'app spi2putgen 48001')
        self.assertEqual(digest, '7ee27ed3249b4b1c21d1bc86d40d9db9393ec7637f24f4fbdc13e29f7746be3c')

    def test_max_metadata_commands_fit_uart_ring_with_crlf(self):
        prefix = 'https://example.test/'
        url = prefix + 'x' * (probe.META_MAX - len('PUT\n\n') - len(prefix))
        text, _ = probe.prepare(dict(url=url, length=1, headers={}))
        lines = text.splitlines()
        self.assertTrue(all(len(line.encode('ascii')) + 2 < 64 for line in lines))
        chunks = [line.split() for line in lines[1:-1]]
        self.assertEqual([int(row[3]) for row in chunks], list(range(0, probe.META_MAX, 16)))
        rebuilt = b''.join(bytes.fromhex(row[4]) for row in chunks)
        self.assertEqual(rebuilt, ('PUT\n' + url + '\n').encode('ascii'))
        self.assertEqual(lines[0], f'app spi2putcfg begin {probe.META_MAX} {zlib.crc32(rebuilt):08x}')

    def test_rejects_unsupported_or_ambiguous_requests(self):
        base = dict(url='https://example.test/p', length=1)
        for change in (
            dict(url='http://example.test/p'), dict(url='https://example.test'),
            dict(url='https://u:p@example.test/p'), dict(url='https://example.test/p#x'),
            dict(url='https://example.test/p\r\nX: x'),
            dict(url='https://example.test/' + 'x' * 3072),
            dict(length=0), dict(length=True), dict(length=4800001), dict(offset=48000),
            dict(headers={'Host': 'example.test'}), dict(headers={'Content-Length': '1'}),
            dict(headers={'X-Test': 'a', 'x-test': 'b'}), dict(headers={'X-Test': 'a\r\nb'}),
        ):
            with self.subTest(change=list(change)):
                with self.assertRaises(ValueError):
                    probe.prepare(dict(base, **change))

    def test_decode_binary_and_failure_response(self):
        body = bytes(range(256)) * 4
        lines = [f'SPI HTTP DATA tid=9 offset={i} hex={body[i:i+32].hex()}'
                 for i in range(0, len(body), 32)]
        lines.append(f'SPI HTTP tid=9 http=503 bytes={len(body)}')
        self.assertEqual(probe.decode('\n'.join(lines), 9), (503, body))
        for bad in (lines[1:], lines[:-1], [lines[0]] + lines, lines + [lines[-1]],
                    lines[:-1] + ['SPI HTTP tid=9 http=200 bytes=1']):
            with self.assertRaises(ValueError):
                probe.decode('\n'.join(bad), 9)

    def test_empty_response_and_transaction_filter(self):
        self.assertEqual(probe.decode('SPI HTTP DATA tid=1 offset=0 hex=00\nSPI HTTP tid=2 http=204 bytes=0', 2), (204, b''))


if __name__ == '__main__':
    unittest.main()
