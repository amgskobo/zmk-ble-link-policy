#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Create a new fixed test workspace; network access must be requested explicitly."""
import argparse
import json
from pathlib import Path
from ci_support import execute, load_pins, workspace_identity


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--core', choices=('upstream', 'dya'), required=True)
    ap.add_argument('--output', type=Path, required=True)
    origin = ap.add_mutually_exclusive_group(required=True)
    origin.add_argument('--allow-network', action='store_true', help='Fetch fixed commits into this new workspace only')
    origin.add_argument('--from-cache', type=Path, help='Clone exact objects from a read-only existing workspace; no fetch')
    args = ap.parse_args()
    pins = load_pins()
    output = args.output.resolve()
    if output.exists():
        raise FileExistsError('Workspace already exists; it will not be updated')
    cache = args.from_cache.resolve() if args.from_cache else None
    if cache:
        workspace_identity(cache, args.core, pins)
    output.mkdir(parents=True, exist_ok=False)
    logs = output / 'preparation'
    logs.mkdir()
    manifest = output / 'manifest'
    manifest.mkdir()
    # An owner manifest selects only this XIAO/native fixture's required modules.
    # It imports no moving branches and rewrites no dependency source/manifests.
    projects = pins['cores'][args.core]
    (manifest / 'west.yml').write_text(json.dumps(
        {'manifest': {'projects': projects, 'self': {'path': 'manifest'}}}, indent=2) + '\n', encoding='utf-8')
    execute(['west', 'init', '-l', str(manifest)], output, logs, 'west-init')
    for project in projects:
        destination = output / project['path']
        if cache:
            source = cache / project['path']
            # Materialize only the selected HEAD, without borrowed object paths
            # or unrelated history from a partial/promisor cache. The cache
            # identity gate above already requires this HEAD to equal the pin.
            execute(['git', '-c', 'safe.directory=' + str(source), 'clone',
                     '--no-local', '--depth=1', '--single-branch', '--no-checkout',
                     str(source), str(destination)],
                    output, logs, 'clone-' + project['name'])
            execute(['git', '-C', str(destination), 'checkout', '--detach', project['revision']],
                    output, logs, 'checkout-' + project['name'])
        else:
            execute(['west', 'update', '--narrow', '-o=--depth=1', project['name']],
                    output, logs, 'fetch-' + project['name'])
    identity = workspace_identity(output, args.core, pins)
    (output / 'workspace.json').write_text(json.dumps(
        dict(core=args.core, network=args.allow_network, local_objects_from=str(cache) if cache else None,
             projects=identity), indent=2) + '\n', encoding='utf-8')
    print('PASS: new pinned ' + args.core + ' workspace; no existing cache updated')


if __name__ == '__main__':
    main()
