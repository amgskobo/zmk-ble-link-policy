#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the inherited policy oracle, isolation checks and compiled negative controls."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time
root = Path(__file__).resolve().parents[1]
ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('--output', required=True, type=Path)
a = ap.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
rows = []
def run(label, command):
    start = time.time()
    with (a.output / (label + '.log')).open('w') as log:
        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, cwd=root)
    rows.append(dict(label=label, command=command, exit=result.returncode, start=start, end=time.time()))
    (a.output / 'results.json').write_text(json.dumps(rows, indent=2))
    assert result.returncode == 0, label
for label, flags in [('sanitized', ['-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all']), ('optimized', ['-O2'])]:
    binary = a.output / label
    run(label + '-build', ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic', *flags, '-I' + str(root / 'src'), str(root / 'src/link_policy.c'), str(root / 'tests/host/test_link_policy.c'), '-o', str(binary)])
    for repeat in range(3): run(label + '-run-' + str(repeat), [str(binary)])
run('ownership', ['python3', '-m', 'unittest', 'discover', '-s', 'tests', '-p', 'test_*.py', '-v'])
run('pure-mutations', ['python3', 'tests/mutation/mutate_idle_links.py', '--output', str(a.output / 'mutations')])
(a.output / 'source-hashes.json').write_text(json.dumps({p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in (root / 'src').glob('*')}, indent=2))
print('PASS: inherited pure suite, optimization/sanitizers, observations, ownership and59 compiled faults')
