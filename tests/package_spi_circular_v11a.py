"""Verify/package V11A ATS; pair with the byte-identical, preserved UMA V11.

No serial/device access. Never overwrite an existing firmware candidate.
"""
from pathlib import Path
import difflib
import hashlib
import json
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

sys.path.insert(0, str(Path(tempfile.gettempdir())/'uma503_dma_validation_testdeps'))
from elftools.elf.elffile import ELFFile

A = Path(__file__).resolve().parents[1]
U = Path('D:/UMA503/UMA503-V7_OPENCPU_OC_2025.03.14_1644')
PREV = 'artifacts/spi_circular_v11_20261118'
OUT = 'artifacts/spi_circular_v11a_20261118'
AO, UO = A/OUT, U/OUT
B = A/'TAG_ZS3085E_241230/application/bt_watch_no_psram/outdir/ats3085e_dev_watch_ext_nor'
AP = A/'artifacts/ATS3085E_SPI_CIRCULAR_V11A_20261118.fw'
DOC = 'docs/SPI_HARDWARE_CIRCULAR_V11_20261118.md'
RESULT = 'docs/SPI_HARDWARE_CIRCULAR_V11A_RESULT_20261118.md'

def sha(p):
    return hashlib.sha256(p.read_bytes() if isinstance(p, Path) else p).hexdigest()

def capture(root, baseline, before, allowed, added):
    out = root/OUT
    changes, hashes, patch = [], {}, []
    for rel in sorted(set(baseline)|set(added)):
        p = root/rel
        raw = p.read_bytes()
        hashes[rel] = sha(raw)
        if rel not in baseline or hashes[rel] != baseline[rel]:
            assert rel in allowed or rel in added, ('Unexpected source change', str(p))
            changes.append(rel)
            old = (before/rel).read_text(encoding='utf-8-sig') if rel in baseline else ''
            patch.extend(difflib.unified_diff(old.splitlines(True), raw.decode('utf-8-sig').splitlines(True),
                                             fromfile='v11/'+rel, tofile='v11a/'+rel))
        dst = out/'source'/rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_bytes(raw)
    (out/'this_session.patch').write_text(''.join(patch), encoding='utf-8')
    (out/'git_status_after.txt').write_bytes(subprocess.check_output(['git','status','--short'], cwd=root))
    return hashes, changes

def main():
    assert not AP.exists(), 'Never overwrite an existing candidate'
    UO.mkdir(parents=True, exist_ok=True)
    prev = json.loads((A/PREV/'manifest.json').read_text(encoding='utf-8'))
    for key in ('ats_package','uma_package'):
        assert sha(Path(prev[key]['path'])) == prev[key]['sha256']
    v10 = json.loads((U/'artifacts/spi_speed_v10_block2_20261118/manifest.json').read_text())
    for key in ('ats_package','uma_package'):
        assert sha(Path(v10[key]['path'])) == v10[key]['sha256']
    assert sha(U/'startup/app_release/build/app.bin') == prev['uma_app_sha256']
    assert json.loads((AO/'build_exit.json').read_text())['returncode'] == 0
    log = (AO/'build.log').read_text(encoding='utf-8', errors='replace')
    assert 'build finished' in log
    assert not re.search(r'warning:|error:|undefined reference|FAILED:|CovertFWFormat fail|Out of memory', log, re.I)
    tests = {}
    for name in ('master_ring','master_command','master_legacy','command_legacy','ready','slave_ring','slave_legacy'):
        r = json.loads((AO/(name+'_exit.json')).read_text())
        assert r['returncode'] == 0 and r['tests'] > 0
        assert re.search(r'\nOK\s*$', (AO/(name+'.log')).read_text())
        tests[name] = r['tests']
    assert sum(tests.values()) == 185, tests
    assert json.loads((AO/'master_ring_os_exit.json').read_text())['returncode'] == 0
    assert re.search(r'\nOK\s*$', (AO/'master_ring_os.log').read_text())
    repro = (AO/'repro_before_fix_clean.log').read_text()
    assert 'AssertionError: -5 != 0 : block 0' in repro and 'FAILED (failures=1)' in repro
    zephyr = (B/'zephyr/zephyr.bin').read_bytes()
    for marker in (b'rev=11a', b'SPI11 DMA sample', b'SPI11 BLOCKED', b'SPI11 RX headers'):
        assert marker in zephyr, marker

    data = (B/'_firmware/ats3085e_dev_watch_ext_nor_261118.fw').read_bytes()
    assert data[:8] == b'ACTTEST0' and 0 < data[12] <= 15
    assert struct.unpack_from('<I',data,8)[0] == zlib.crc32(data[12:512])
    assert struct.unpack_from('<I',data,24)[0] == zlib.crc32(data[512:])
    assert struct.unpack_from('<I',data,20)[0] == len(data)
    members = {}
    for i in range(data[12]):
        name, off, size, crc = struct.unpack_from('<12s4xII4xI',data,32+i*32)
        name = name.rstrip(b'\0').decode()
        payload = data[off:off+size]
        assert off >= 512 and off+size <= len(data) and zlib.crc32(payload) == crc
        p = B/'_firmware/bin_orig/ATT'/name
        if not p.exists(): p = B/'_firmware/bin_orig'/name
        assert p.read_bytes() == payload, name
        members[name] = sha(payload)
    packed = (B/'_firmware/bin_orig/app.bin').read_bytes()
    assert len(packed) == (len(zephyr)+511)//512*512
    assert packed[:8] == b'ACTHHTCA'
    assert packed[512:len(zephyr)] == zephyr[512:] and not any(packed[len(zephyr):])
    size = struct.unpack_from('<I',packed,0x18)[0]
    assert size == (len(zephyr)+3)//4*4-512
    assert (sum(struct.unpack('<%dI'%(size//4),packed[512:512+size]))+
            struct.unpack_from('<I',packed,0x1c)[0]) & 0xffffffff == 0xffffffff
    assert sum(struct.unpack('<128I',packed[:512])) & 0xffffffff == 0xffffffff
    used = int(re.search(r'\bSRAM:\s+(\d+) B',log).group(1))
    assert used <= 911*1024
    with (B/'zephyr/zephyr.elf').open('rb') as f:
        elf = ELFFile(f)
        syms = {s.name:(s['st_value'],s['st_size']) for s in elf.get_section_by_name('.symtab').iter_symbols()}
    buffers = {}
    for name, size in (('spi2_dma_tx',512),('spi2_dma_rx',1024)):
        address, actual_size = syms[name]
        assert actual_size == size and address%4 == 0
        assert 0x2ff60400 <= address < address+size <= 0x2ff60400+911*1024
        buffers[name] = {'address':address,'size':size}

    prefix = 'TAG_ZS3085E_241230/'
    allowed = [prefix+'zephyr/drivers/spi/spi_acts_spi2_circular.h',
               prefix+'application/bt_watch_no_psram/src/main/spi_link_circular.h',
               'tests/test_spi_circular_dma_v11.py','tests/test_spi_circular_host_v11.py',DOC]
    added = ['tests/run_spi_v11a_checks.py','tests/package_spi_circular_v11a.py',RESULT]
    ah, ac = capture(A,prev['ats_sources'],AO/'before',allowed,added)
    uh, uc = capture(U,prev['uma_sources'],U/PREV/'source',[DOC],[RESULT])
    for rel, h in prev['uma_sources'].items():
        if rel != DOC: assert uh[rel] == h, rel
    for n in ('zephyr.bin','zephyr.elf','zephyr.map'):
        shutil.copy2(B/'zephyr'/n,AO/n)
    inputs = {p.name:sha(p) for p in (AO/'logs').iterdir() if p.is_file()}
    manifest = {
        'version':'V11A_RX_COMPLETION_SAMPLE_ORDER','date_local':'2026-11-18',
        'hardware_tested_this_revision':False,
        'v11_hardware_observation':'1 and 10 blocks passed; target 500 failed at transfer 46; rebooted target 5000 failed its first ACK; first-ACK cause unresolved',
        'repro':'Old production driver fails -5 on RX drain between status and event reads; V11A passes',
        'test_counts':tests,'test_total':sum(tests.values()),
        'optimized_model':'13 additional repeated checks passed with -Os and model-only -fno-if-conversion/-fno-if-conversion2; excluded from unique total',
        'ats_sram_used_bytes':used,'ats_sram_link_margin_bytes':911*1024-used,
        'ats_compile_warnings':0,'ats_dma_buffers':buffers,
        'ats_package':{'path':str(AP),'bytes':len(data),'sha256':sha(data),'members':members},
        'uma_package':prev['uma_package'],'uma_firmware_reused_unchanged':True,
        'ats_zephyr_sha256':sha(zephyr),'uma_app_sha256':prev['uma_app_sha256'],
        'protocol_unchanged':True,'block_bytes':512,'payload_bytes':480,'ack_delay_blocks':2,
        'ats_sources':ah,'uma_sources':uh,'ats_session_changes':ac,'uma_session_changes':uc,
        'input_log_sha256':inputs,'v10_and_v11_packages_preserved':True,
        'package_checks':'ATF/header/member CRCs, SDK member identity and app body/checksums; proprietary inner ATF container compared with SDK output',
    }
    # Publish only after all checks, including preservation of existing work.
    with AP.open('xb') as f: f.write(data)
    for out in (AO,UO):
        (out/'manifest.json').write_text(json.dumps(manifest,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
        (out/'SHA256.txt').write_text(sha(AP)+'  '+str(AP)+'\n'+
            prev['uma_package']['sha256']+'  '+prev['uma_package']['path']+'\n',encoding='utf-8')
    print(json.dumps({k:manifest[k] for k in ('test_total','ats_sram_link_margin_bytes',
        'ats_compile_warnings','uma_firmware_reused_unchanged','hardware_tested_this_revision')},indent=2))
    print(AP)

if __name__ == '__main__': main()
