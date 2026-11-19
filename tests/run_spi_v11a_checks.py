"""Record V11A regression results without replacing the original V11 evidence."""
from pathlib import Path
import json
import os
import re
import subprocess
import sys

A = Path(__file__).resolve().parents[1]
U = Path('D:/UMA503/UMA503-V7_OPENCPU_OC_2025.03.14_1644')
SUITES = {
    'master_ring': (A, 'test_spi_circular_dma_v11.py'),
    'master_command': (A, 'test_spi_circular_host_v11.py'),
    'master_legacy': (A, 'test_spi2_dma_v5.py'),
    'command_legacy': (A, 'test_spi_block2_host.py'),
    'ready': (A, 'test_spi_ready_v7.py'),
    'slave_ring': (U, 'test_spi_circular_v11.py'),
    'slave_legacy': (U, 'test_spi_link_dma.py'),
}
name = sys.argv[1]
root, script = SUITES[name]
out = A / 'artifacts/spi_circular_v11a_20261118'
out.mkdir(parents=True, exist_ok=True)
cmd = [sys.executable, str(root/'tests'/script), '--build-dir', str(out/name)]
env = os.environ.copy()
env['PYTHONIOENCODING'] = 'utf-8'
with (out/(name+'.log')).open('wb') as log:
    result = subprocess.run(cmd, cwd=root, env=env, stdout=log, stderr=subprocess.STDOUT)
raw = (out/(name+'.log')).read_text(encoding='utf-8', errors='replace')
match = re.search(r'Ran (\d+) tests? in', raw)
summary = {'suite': name, 'returncode': result.returncode,
           'tests': int(match.group(1)) if match else None, 'command': cmd}
(out/(name+'_exit.json')).write_text(json.dumps(summary, indent=2)+'\n')
print(json.dumps(summary))
print(raw[-1200:])
raise SystemExit(result.returncode)
