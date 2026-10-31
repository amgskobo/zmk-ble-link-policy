# SPDX-License-Identifier: GPL-3.0-only
"""Pinned test-workspace selection and retained command/source evidence."""
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import time
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[1]
PINS = ROOT / 'tests/ci/pins.json'
ENV = dict(os.environ, GIT_NO_LAZY_FETCH='1', GIT_TERMINAL_PROMPT='0')
REQUIRED = {'zmk', 'zephyr', 'cmsis', 'hal_nordic', 'hal_stm32', 'lvgl', 'tinycrypt', 'mbedtls', 'nanopb',
            'zmk-studio-messages'}


def load_pins(path=PINS):
    pins = json.loads(path.read_text(encoding='utf-8'))
    if pins['schema'] != 1 or not re.fullmatch(r'[^\s]+@sha256:[0-9a-f]{64}', pins['image']):
        raise ValueError('Expected schema1 and a digest-pinned image')
    if set(pins['cores']) != {'upstream', 'dya'}:
        raise ValueError('Both supported cores must be explicit')
    for projects in pins['cores'].values():
        if len(projects) != len(REQUIRED) or {p['name'] for p in projects} != REQUIRED:
            raise ValueError('Incomplete or duplicate project set')
        paths = []
        for project in projects:
            keys = {'name', 'path', 'url', 'revision'}
            if project['name'] == 'zephyr':
                keys.add('west-commands')
                if project.get('west-commands') != 'scripts/west-commands.yml':
                    raise ValueError('The pinned Zephyr build extension must be registered')
            if set(project) != keys:
                raise ValueError('Unexpected project fields or moving imports')
            url = urlsplit(project['url'])
            path = PurePosixPath(project['path'])
            if (url.scheme != 'https' or url.netloc != 'github.com' or
                    url.query or url.fragment or not re.fullmatch(r'/[\w.-]+/[\w.-]+', url.path)):
                raise ValueError('Expected a public HTTPS GitHub repository URL')
            if not re.fullmatch(r'[0-9a-f]{40}', project['revision']):
                raise ValueError('Project revisions must be complete immutable commits')
            if (path.is_absolute() or not path.parts or '..' in path.parts or
                    path.parts[0] in ('.west', 'manifest') or str(path) != project['path'] or
                    '\\' in project['path'] or ':' in project['path']):
                raise ValueError('Invalid workspace project path')
            if any(path == old or path in old.parents or old in path.parents for old in paths):
                raise ValueError('Overlapping workspace project paths')
            paths.append(path)
    return pins


def head(path):
    return subprocess.check_output(
        ['git', '-c', 'safe.directory=' + str(path), '-C', str(path),
         'rev-parse', 'HEAD'], env=ENV, text=True, stderr=subprocess.PIPE).strip()


def workspace_identity(workspace, core, pins=None):
    pins = pins or load_pins()
    rows = []
    for project in pins['cores'][core]:
        path = workspace / project['path']
        revision = head(path)
        if revision != project['revision']:
            raise ValueError('Wrong selected revision: ' + project['name'])
        rows.append(dict(project, resolved_path=str(path), actual_revision=revision))
    return rows


def source_identity(root=ROOT):
    files = {}
    for path in sorted(root.rglob('*')):
        if set(path.relative_to(root).parts) & {'.git', '__pycache__', 'artifacts', '.build'}:
            continue
        if path.is_file():
            files[path.relative_to(root).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
    return files


def execute(command, cwd, output, label, env=None):
    log = output / (label + '.log')
    record = output / (label + '.json')
    if log.exists() or record.exists():
        raise FileExistsError('Command evidence already exists: ' + label)
    facts = dict(command=[str(x) for x in command], cwd=str(cwd), start=time.time(), exit=None)
    record.write_text(json.dumps(facts, indent=2) + '\n', encoding='utf-8')
    try:
        with log.open('xb') as stream:
            result = subprocess.run(command, cwd=cwd, env=env or ENV,
                                    stdout=stream, stderr=subprocess.STDOUT)
        facts['exit'] = result.returncode
    finally:
        facts['end'] = time.time()
        record.write_text(json.dumps(facts, indent=2) + '\n', encoding='utf-8')
    if facts['exit'] != 0:
        raise RuntimeError('Command failed; retain ' + str(record))
    return facts
