#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Dispatch this owner's complete CI lane with source/dependency/command evidence."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
from ci_support import ENV, ROOT, execute, head, load_pins, source_identity, workspace_identity


def lane_command(suite, workspace, output, build_root, dya_workspace=None):
    if suite == 'host':
        return [sys.executable, 'tests/run-host.py', '--output', str(output / 'host')]
    if suite == 'split':
        return [sys.executable, 'tests/native/run-split.py', '--workspace', str(workspace),
                '--dya-workspace', str(dya_workspace), '--output', str(output / 'split')]
    if suite == 'native':
        command = [sys.executable, 'tests/mutation/mutate_link_service.py',
                   '--zephyr-base', str(workspace / 'zephyr'),
                   '--cmsis-module', str(workspace / 'modules/hal/cmsis'),
                   '--output', str(output / 'native')]
    else:
        command = [sys.executable, 'tests/run-integration.py', '--workspace', str(workspace),
                   '--output', str(output / 'integration')]
    return command + (['--build-root', str(build_root)] if build_root else [])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--suite', choices=('host', 'native', 'split', 'integration'), required=True)
    ap.add_argument('--core', choices=('upstream', 'dya'), default='upstream')
    ap.add_argument('--workspace', type=Path)
    ap.add_argument('--dya-workspace', type=Path)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--build-root', type=Path)
    args = ap.parse_args()
    if args.suite != 'host' and args.workspace is None:
        ap.error('native/integration require an explicit unchanged workspace')
    if args.suite == 'split' and (args.dya_workspace is None or args.core != 'upstream' or args.build_root):
        ap.error('split requires stock --workspace and --dya-workspace; builds stay in its fresh output')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    before = source_identity()
    (output / 'source-before.json').write_text(json.dumps(before, indent=2) + '\n', encoding='utf-8')
    facts = dict(suite=args.suite, core=args.core, mode='full', status='RUNNING')
    dependencies_before = None
    dya_before = None
    try:
        try:
            facts['module_head'] = head(ROOT)
        except subprocess.CalledProcessError:
            facts['module_head'] = None
        facts['image'] = load_pins()['image']
        if args.workspace:
            args.workspace = args.workspace.resolve()
            dependencies_before = workspace_identity(args.workspace, args.core)
            facts['dependencies'] = dependencies_before
        if args.suite == 'split':
            args.dya_workspace = args.dya_workspace.resolve()
            dya_before = workspace_identity(args.dya_workspace, 'dya')
            facts['dya_dependencies'] = dya_before
        env = dict(ENV, ZMK_TEST_MODE='full')
        if args.workspace:
            env.update(ZEPHYR_BASE=str(args.workspace / 'zephyr'),
                       Zephyr_DIR=str(args.workspace / 'zephyr/share/zephyr-package/cmake'))
        command = lane_command(args.suite, args.workspace, output,
                               args.build_root.resolve() if args.build_root else None,
                               args.dya_workspace)
        execute(command, ROOT, output, args.suite, env)
        facts['status'] = 'PASS'
    except Exception as error:
        facts.update(status='FAIL', error=type(error).__name__ + ': ' + str(error))
    finally:
        after = source_identity()
        (output / 'source-after.json').write_text(json.dumps(after, indent=2) + '\n', encoding='utf-8')
        facts['source_unchanged'] = before == after
        if before != after:
            facts.update(status='FAIL', error='Owner source changed during the lane; retain both identities')
        if dependencies_before is not None:
            try:
                dependencies_after = workspace_identity(args.workspace, args.core)
                facts['dependencies_after'] = dependencies_after
                facts['dependencies_unchanged'] = dependencies_before == dependencies_after
                if not facts['dependencies_unchanged']:
                    facts.update(status='FAIL', error='Selected dependencies changed during the lane')
            except Exception as error:
                facts.update(status='FAIL', dependencies_unchanged=False,
                             error='Selected dependencies changed during the lane: ' +
                                   type(error).__name__ + ': ' + str(error))
        if dya_before is not None:
            try:
                dya_after = workspace_identity(args.dya_workspace, 'dya')
                facts['dya_dependencies_after'] = dya_after
                if dya_before != dya_after:
                    facts.update(status='FAIL', error='DYA source changed during split lane')
            except Exception as error:
                facts.update(status='FAIL', error='DYA dependency drift: ' + str(error))
        (output / 'result.json').write_text(json.dumps(facts, indent=2) + '\n', encoding='utf-8')
    print(args.suite + ' ' + facts['status'])
    if facts['status'] != 'PASS':
        raise SystemExit(1)


if __name__ == '__main__':
    main()
