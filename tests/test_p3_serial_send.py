import contextlib
import importlib.util
import io
from pathlib import Path
import unittest
import sys
from unittest.mock import patch
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
spec = importlib.util.spec_from_file_location('sender', Path(__file__).resolve().parents[1]/'tools/p3_serial_send.py')
sender = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sender)


def commands():
    data = b'PUT\nhttps://example.test/put\n'
    lines = ['app spi2putcfg begin {} {:08x}'.format(len(data), zlib.crc32(data))]
    for i in range(0, len(data), 16):
        lines.append('app spi2putcfg data {} {}'.format(i, data[i:i+16].hex()))
    return '\n'.join(lines + ['app spi2putgen 48001'])


class Port:
    def __init__(self, wrong=False, omit=False, result=b'PASS'):
        self.buf = b''
        self.reply = b''
        self.sent = []
        self.wrong, self.omit, self.result = wrong, omit, result

    def reset_input_buffer(self):
        self.reply = b''

    def flush(self):
        pass

    def write(self, value):
        self.buf += value
        if value != b'\r':
            return 1
        line, self.buf = self.buf[:-1].decode(), b''
        self.sent.append(line)
        words = line.split()
        if line.endswith(' clear'):
            self.reply = b'SPI PUT CFG cleared\r\n'
        elif len(words)>2 and words[2]=='begin':
            self.total = int(words[3])
            self.reply = 'SPI PUT CFG bytes=0/{}\r\n'.format(self.total).encode()
        elif len(words)>2 and words[2]=='data':
            n = int(words[3])+len(words[4])//2
            self.reply = b'' if self.omit else 'SPI PUT CFG bytes={}/{}\r\n'.format(n+int(self.wrong),self.total).encode()
        elif line.startswith('app spi2putgen'):
            self.reply = b'SPI FILE TEST groups=2 crc32=12345678 '+self.result+b'\r\n'
        return 1

    def read(self, size):
        # Fragment ACKs to model UART reads.
        data, self.reply = self.reply[:7], self.reply[7:]
        return data


class SenderTests(unittest.TestCase):
    def good_result(self):
        return (b'SPI HTTP tid=7 http=200 bytes=0\r\n'
                b'SPI FILE tid=7 bytes=48001 transfers=127 resent=0 ms=10899 Bps=4404 ready_us=10689565 status=0 ret=0\r\n'
                b'SPI FILE TEST groups=2 crc32=887489ae PASS\r\n')

    def test_result_requires_http_size_crc_and_same_transaction(self):
        good = self.good_result()
        result = sender.verify_result(good, 48001)
        self.assertEqual(result['http'], 200)
        self.assertEqual(result['bytes'], 48001)
        self.assertEqual(result['sha256'], '7ee27ed3249b4b1c21d1bc86d40d9db9393ec7637f24f4fbdc13e29f7746be3c')
        for bad in (good.replace(b'http=200', b'http=403'),
                    good.replace(b'HTTP tid=7', b'HTTP tid=8'),
                    good.replace(b'bytes=48001', b'bytes=48000'),
                    good.replace(b'887489ae', b'00000000'),
                    good.replace(b'ret=0', b'ret=-1'),
                    good.replace(b'groups=2', b'groups=1'),
                    good.replace(b'status=0', b'status=4'),
                    good + good, good.splitlines()[-1]):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                sender.verify_result(bad, 48001)

    def run_sender(self, port, stage=False):
        ticks = iter(i*.001 for i in range(100000))
        with patch.object(sender.time,'sleep'), patch.object(sender.time,'monotonic',side_effect=lambda: next(ticks)), contextlib.redirect_stdout(io.StringIO()):
            plan, put, total = sender.validate(commands())
            return sender.Sender(port,io.BytesIO(),ack_timeout=.1).run(plan,put,total,stage)

    def test_fragmented_ack_and_one_put_only(self):
        p = Port()
        self.assertEqual(self.run_sender(p),0)
        self.assertEqual(p.sent.count('app spi2putgen 48001'),1)

    def test_missing_or_wrong_ack_never_starts_put(self):
        for p, error in [(Port(wrong=True),RuntimeError),(Port(omit=True),TimeoutError)]:
            with self.assertRaises(error):
                self.run_sender(p)
            self.assertFalse(any(s.startswith('app spi2putgen') for s in p.sent))

    def test_garbled_reply_reports_serial_settings_without_raw_bytes(self):
        port = Port()
        port.reply = bytes([0xff, 0xfe, 0x80, 0x00]) * 30
        ticks = iter(i*.001 for i in range(1000))
        with patch.object(sender.time, 'monotonic', side_effect=lambda: next(ticks)):
            tx = sender.Sender(port, io.BytesIO())
            tx.stage = 'clear configuration (PUT not started)'
            with self.assertRaises(sender.ReplyTimeout) as error:
                tx.wait(b'NEVER_MATCH', .05)
        self.assertIn('check baud rate and 8N1', str(error.exception))
        self.assertIn('PUT not started', str(error.exception))
        self.assertNotIn('\\xff', str(error.exception))
        self.assertEqual(port.sent, [])

    def test_board_failure_no_retry_and_stage_only(self):
        p = Port(result=b'FAIL')
        self.assertEqual(self.run_sender(p),2)
        self.assertEqual(p.sent.count('app spi2putgen 48001'),1)
        p = Port()
        self.assertEqual(self.run_sender(p,True),0)
        self.assertFalse(any(s.startswith('app spi2putgen') for s in p.sent))

    def test_incomplete_corrupt_and_arbitrary_commands_rejected(self):
        good = commands().splitlines()
        for lines in [good[:1]+good[2:],good[:2]+good[1:],good[:-1]+['reboot'],
                      [good[0].replace('begin 28','begin 29')]+good[1:]]:
            # Ensure mutated length independently of URL size.
            if lines == good:
                lines = [good[0][:-1]+('0' if good[0][-1]!='0' else '1')]+good[1:]
            with self.assertRaises(ValueError):
                sender.validate('\n'.join(lines))


if __name__ == '__main__':
    unittest.main(verbosity=2)
