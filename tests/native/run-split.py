#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run the split owner's kernel cases and faults, including both real activity cores."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ci_support import ENV, ROOT, source_identity, workspace_identity
from native_output import check_run
from split_output import check_control

FIXTURE = ROOT / 'tests/native/split-links'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--workspace', required=True, type=Path, help='Pinned stock workspace')
    parser.add_argument('--dya-workspace', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--jobs', type=int,
                        default=os.environ.get('CMAKE_BUILD_PARALLEL_LEVEL', '2'),
                        help='Positive worker count; defaults to CMAKE_BUILD_PARALLEL_LEVEL or 2')
    args = parser.parse_args()
    output = args.output.resolve()
    if args.output.is_symlink() or output.exists() or ROOT == output or ROOT in output.parents or args.jobs < 1:
        parser.error('Use a fresh external output and positive job count')
    output.mkdir(parents=True)
    workspaces = {'upstream': args.workspace.resolve(), 'dya': args.dya_workspace.resolve()}

    def dependencies():
        return {core: workspace_identity(path, core) for core, path in workspaces.items()}

    before, selected = source_identity(), dependencies()
    (output / 'source-before.json').write_text(json.dumps(before, indent=2))
    (output / 'dependencies.json').write_text(json.dumps(selected, indent=2))
    common = workspaces['upstream']
    env = dict(ENV, ZEPHYR_BASE=str(common / 'zephyr'),
               Zephyr_DIR=str(common / 'zephyr/share/zephyr-package/cmake'),
               ZEPHYR_TOOLCHAIN_VARIANT='host', CMAKE_BUILD_PARALLEL_LEVEL=str(args.jobs),
               CCACHE_DIR=str(output / 'ccache'), PYTHONDONTWRITEBYTECODE='1')
    commands, results = [], []

    def run(label, command):
        row = dict(label=label, command=[str(x) for x in command], start=time.time(), exit=None)
        commands.append(row)
        try:
            process = subprocess.run(command, cwd=ROOT, env=env, text=True,
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=900)
            row['exit'] = process.returncode
            (output / (label + '.log')).write_text(process.stdout)
            return process
        finally:
            row['end'] = time.time()
            (output / 'commands.json').write_text(json.dumps(commands, indent=2))

    counts = {'default': 22, 'preempt': 23, 'activity-upstream': 9, 'activity-dya': 9}
    controls = [
        ('idle-stays-fast', 'default', 'test_idle_uses_30ms_then_stops', 'split_service.c',
         '.idle_interval = CONFIG_ZMK_BLE_SPLIT_POLICY_IDLE_INTERVAL,',
         '.idle_interval = CONFIG_ZMK_SPLIT_BLE_PREF_INT,'),
        ('extra-retry', 'default', 'test_refused_requests_are_bounded', 'split_policy.h',
         '#define ZBLP_SPLIT_REQUEST_LIMIT 3U', '#define ZBLP_SPLIT_REQUEST_LIMIT 4U'),
        ('host-role-selected', 'default', 'test_idle_uses_30ms_then_stops', 'split_service.c',
         'info->role == BT_CONN_ROLE_CENTRAL', 'info->role == BT_CONN_ROLE_PERIPHERAL'),
        ('quiet-wake-cancelled', 'preempt', 'test_quiet_worker_preserves_preempting_activity_wake',
         'split_service.c', '/* Do not cancel a quiet running worker:', ''),
    ]
    for core in workspaces:
        controls.append(('active-gated-' + core, 'activity-' + core, 'test_split_key_immediate_wake',
                         'split_policy.h',
                         'const bool first_active_request = p->mode == ZBLP_SPLIT_ACTIVE && p->attempts == 0;',
                         'const bool first_active_request = false;'))
    plan = [(name, name, None) for name in counts]
    plan += [(control[0], control[1], control) for control in controls]
    try:
        for name, variant, control in plan:
            directory = output / name
            directory.mkdir()
            extra = []
            if variant == 'preempt':
                extra.append('-DZBLP_SPLIT_PREEMPT_TEST=ON')
            if variant.startswith('activity-'):
                core = variant.removeprefix('activity-')
                extra += ['-DZBLP_SPLIT_ACTIVITY_TEST=ON',
                          '-DZBLP_ZMK_ROOT=' + str(workspaces[core] / 'zmk'),
                          '-DZBLP_SPLIT_CORE_DYA=' + ('ON' if core == 'dya' else 'OFF')]
            if control:
                _, _, case, filename, old, new = control
                copied = directory / 'source'
                copied.mkdir()
                for filename_to_copy in ['split_service.c', 'split_policy.h']:
                    shutil.copy2(ROOT / 'src' / filename_to_copy, copied / filename_to_copy)
                path = copied / filename
                text = path.read_text()
                if name == 'quiet-wake-cancelled':
                    position = text.index('k_spin_unlock(&policy_lock, key);', text.index(old))
                    position += len('k_spin_unlock(&policy_lock, key);')
                    text = text[:position] + '\n        k_work_cancel_delayable(&split_work);' + text[position:]
                else:
                    if text.count(old) != 1:
                        raise ValueError('Control site is not unique: ' + name)
                    text = text.replace(old, new)
                path.write_text(text)
                extra.append('-DZBLP_SPLIT_POWER_SOURCE=' + str(copied / 'split_service.c'))
            build = directory / 'build'
            compiled = run(name + '-build', ['west', 'build', '-p=always', '-s', str(FIXTURE),
                            '-d', str(build), '-b', 'native_sim/native/64', '--', '-DWEST=OFF',
                            '-DZEPHYR_MODULES=' + str(common / 'modules/hal/cmsis'), *extra])
            if compiled.returncode:
                raise RuntimeError('Build failed; not a runtime fault result: ' + name)
            binary = build / 'zephyr/zephyr.exe'
            exits = []
            for repeat in range(1, 4):
                process = run(name + '-run-' + str(repeat), [str(binary), '-no-rt'])
                exits.append(process.returncode)
                if control:
                    site = 'require_fast_restore' if name.startswith('active-gated-') else 'split_idle_' + case
                    check_control(process.stdout, process.returncode, case, site)
                else:
                    check_run(process.stdout, process.returncode, counts[variant])
            results.append(dict(name=name, variant=variant, control=bool(control), run_exits=exits,
                                binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest()))
            (output / 'results.json').write_text(json.dumps(results, indent=2))
        print('PASS: split22/23/9/9 cases x3; six named runtime controls x3', flush=True)
    finally:
        after = source_identity()
        (output / 'source-after.json').write_text(json.dumps(after, indent=2))
        if before != after or dependencies() != selected:
            raise RuntimeError('Source/dependency drift invalidates this split run')


if __name__ == '__main__':
    main()
