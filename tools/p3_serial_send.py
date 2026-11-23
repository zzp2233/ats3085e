"""Send a validated P3 command file with ACK pacing; never retry a PUT.

Close the serial terminal on COM4 first. COM6 can remain open for modem logs.
Raw receive logs may contain echoed signed URLs: keep them in .local.
"""
import argparse
import hashlib
import json
from datetime import datetime
from pathlib import Path
import re
import time
import zlib

import p3_put_probe


class ReplyTimeout(TimeoutError):
    """Safe diagnostic: stage and byte counts only, never request contents."""


def verify_result(raw, size):
    """Require HTTP + transfer + data evidence, not just the shell PASS word."""
    clean = re.sub(rb'\x1b\[[0-9;]*[A-Za-z]', b'', raw).decode('ascii', errors='replace')
    files = re.findall(r'SPI FILE tid=(\d+) bytes=(\d+) transfers=(\d+) resent=(\d+) '
                       r'ms=(\d+) Bps=(\d+) ready_us=(\d+) status=(\d+) ret=(-?\d+)', clean)
    tests = re.findall(r'SPI FILE TEST groups=(\d+) crc32=([0-9a-fA-F]{8}) (PASS|FAIL)', clean)
    if len(files) != 1 or len(tests) != 1:
        raise ValueError('Missing or ambiguous board result; complete not allowed')
    tid, count, transfers, resent, elapsed, bps, ready, status, ret = map(int, files[0])
    groups, crc, verdict = tests[0]
    digest, expected_crc = hashlib.sha256(), 0
    for chunk in p3_put_probe.generated_chunks(size):
        digest.update(chunk)
        expected_crc = zlib.crc32(chunk, expected_crc)
    code, response = p3_put_probe.decode(clean, tid)
    if (count != size or not transfers or ret != 0 or status != 0 or verdict != 'PASS' or
            int(groups) != (size + 47999) // 48000 or int(crc, 16) != expected_crc or
            not 200 <= code < 300):
        raise ValueError('Board result does not confirm successful PUT; complete not allowed')
    return dict(tid=tid, bytes=count, http=code, response_bytes=len(response), ret=ret,
                groups=int(groups), crc32=crc.lower(), sha256=digest.hexdigest(),
                transfers=transfers, resent=resent, elapsed_ms=elapsed, ready_us=ready)


def validate(text):
    lines = [s.strip() for s in text.splitlines() if s.strip()]
    if len(lines) < 3 or any(len(s.encode('ascii')) + 2 >= 64 for s in lines):
        raise ValueError('Expected short P3 command lines (under 64 bytes including CRLF)')
    begin = re.fullmatch(r'app spi2putcfg begin (\d+) ([0-9a-fA-F]{8})', lines[0])
    end = re.fullmatch(r'app spi2putgen (\d+)', lines[-1])
    if not begin or not end or not 1 <= int(end[1]) <= 4800000:
        raise ValueError('Invalid begin or PUT command')
    total = int(begin[1])
    if not 1 <= total <= 3072:
        raise ValueError('Invalid metadata length')
    data = bytearray()
    plan = [(lines[0], 0)]
    for line in lines[1:-1]:
        m = re.fullmatch(r'app spi2putcfg data (\d+) ([0-9a-fA-F]+)', line)
        if not m or int(m[1]) != len(data) or len(m[2]) % 2:
            raise ValueError('Missing, repeated or reordered metadata command')
        data.extend(bytes.fromhex(m[2]))
        plan.append((line, len(data)))
    if len(data) != total or zlib.crc32(data) != int(begin[2], 16):
        raise ValueError('Metadata length or CRC mismatch')
    if not data.startswith(b'PUT\nhttps://'):
        raise ValueError('Expected HTTPS PUT metadata')
    return plan, lines[-1], total


class Sender:
    def __init__(self, port, log, char_delay=0.005, ack_timeout=10, result_timeout=180):
        self.port, self.log = port, log
        self.char_delay, self.ack_timeout, self.result_timeout = char_delay, ack_timeout, result_timeout
        self.stage = 'startup'

    def send(self, command):
        # Drain old input before each request so old ACKs cannot satisfy it.
        self.port.reset_input_buffer()
        for byte in (command + '\r').encode('ascii'):
            if self.port.write(bytes((byte,))) != 1:
                raise OSError('Serial write incomplete; stopped without retry')
            time.sleep(self.char_delay)
        self.port.flush()

    def wait(self, pattern, timeout):
        deadline = time.monotonic() + timeout
        data = b''
        received = high_bytes = 0
        while time.monotonic() < deadline:
            chunk = self.port.read(4096)
            if chunk:
                received += len(chunk)
                high_bytes += sum(byte >= 128 for byte in chunk)
                self.log.write(chunk)
                self.log.flush()
                data = (data + chunk)[-65536:]
                clean = re.sub(rb'\x1b\[[0-9;]*[A-Za-z]', b'', data)
                match = re.search(pattern, clean)
                if match:
                    return match
                if b'FAULT' in clean or b'assert' in clean.lower():
                    raise RuntimeError('Board fault; stopped without retry')
        hint = ('Received mostly non-ASCII bytes; check baud rate and 8N1 settings.'
                if received and high_bytes * 4 > received else
                'Check board power/port and matching serial settings.' if not received else
                'Received data but no expected ACK; inspect the private log.')
        raise ReplyTimeout('{}: {} bytes received. {} No automatic retry.'.format(self.stage, received, hint))

    def run(self, plan, put, total, stage_only=False):
        self.send('\x03')
        time.sleep(0.1)
        self.stage = 'clear configuration (PUT not started)'
        print('Waiting for configuration-clear ACK...', flush=True)
        self.send('app spi2putcfg clear')
        self.wait(rb'SPI PUT CFG cleared', self.ack_timeout)
        for command, expected in plan:
            self.stage = 'metadata {}/{} (PUT not started)'.format(expected, total)
            self.send(command)
            ack = self.wait(rb'SPI PUT CFG bytes=(\d+)/(\d+)(?:\r|\n)', self.ack_timeout)
            if (int(ack[1]), int(ack[2])) != (expected, total):
                raise RuntimeError('Unexpected metadata ACK; stopped without retry')
            print('Metadata {}/{}'.format(expected, total), flush=True)
        if stage_only:
            print('Staged only; PUT has not been sent.')
            return 0
        print('Starting one PUT; waiting for board result. No automatic retry.', flush=True)
        self.stage = 'PUT result (completion uncertain if no reply)'
        self.send(put)
        result = self.wait(rb'SPI FILE TEST groups=(\d+) crc32=([0-9a-fA-F]+) (PASS|FAIL)', self.result_timeout)
        print(result[0].decode('ascii'))
        # Capture the following FLOW/READY diagnostics without printing URL echoes.
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline:
            chunk = self.port.read(4096)
            if chunk:
                self.log.write(chunk)
                self.log.flush()
        return 0 if result[3] == b'PASS' else 2


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--commands', type=Path, required=True)
    p.add_argument('--port', default='COM4')
    p.add_argument('--baud', type=int, help='Required for sending: use the same baud as your working serial terminal')
    p.add_argument('--stage-only', action='store_true', help='Load metadata without starting PUT')
    p.add_argument('--check', action='store_true', help='Validate file without opening the serial port')
    p.add_argument('--result-json', type=Path, help='Write validated successful board result for orchestration')
    args = p.parse_args()
    try:
        plan, put, total = validate(args.commands.read_text(encoding='utf-8-sig'))
        if args.check:
            print('Validated: {} metadata bytes, {} commands; serial port not opened.'.format(total, len(plan)+1))
            return 0
        if args.baud is None or args.baud <= 0:
            p.error('--baud is required for sending; use the baud rate of the working COM4 terminal')
        if args.result_json and args.result_json.exists():
            raise ValueError('Refusing to overwrite a result')
        import serial
        folder = Path('.local/p3-serial')
        folder.mkdir(parents=True, exist_ok=True)
        log_path = folder / (datetime.now().strftime('%Y%m%d-%H%M%S-%f') + '.log')
        print('Private COM4 receive log: {}'.format(log_path), flush=True)
        print('Serial settings: {} baud={} 8N1, flow control off'.format(args.port, args.baud), flush=True)
        # Configure modem-control lines before opening; do not intentionally reset boards.
        port = serial.Serial(port=None, baudrate=args.baud, timeout=0.1, write_timeout=2)
        port.dtr = False
        port.rts = False
        port.port = args.port
        with log_path.open('xb') as log:
            try:
                port.open()
                result = Sender(port, log).run(plan, put, total, args.stage_only)
            finally:
                port.close()
        if result or args.stage_only:
            return result
        proof = verify_result(log_path.read_bytes(), int(put.split()[-1]))
        print('Verified PUT: HTTP={} bytes={} groups={} crc32={}'.format(
            proof['http'], proof['bytes'], proof['groups'], proof['crc32']))
        if args.result_json:
            with args.result_json.open('x', encoding='utf-8') as output:
                json.dump(proof, output, indent=2)
        return 0
    except ReplyTimeout as error:
        print('Stopped: {}'.format(error))
        return 1
    except (OSError, ValueError, RuntimeError, ImportError, KeyboardInterrupt) as error:
        # Do not expose exceptions containing request text/credentials.
        print('Stopped ({}). Check file/port and private log; no automatic retry.'.format(type(error).__name__))
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
