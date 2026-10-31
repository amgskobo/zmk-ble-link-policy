"""Exercise the real preparation entry with unavailable historical Git objects."""
import copy
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class PartialCachePreparation(unittest.TestCase):
    def test_current_commit_can_be_copied_without_unavailable_old_history(self):
        output = Path(tempfile.mkdtemp(prefix='zblp-partial-cache-'))
        print('Retained partial-cache control: ' + str(output), flush=True)
        origin = output / 'origin'
        origin.mkdir()
        environment = dict(os.environ, GIT_AUTHOR_NAME='Synthetic Fixture',
                           GIT_AUTHOR_EMAIL='fixture@example.invalid',
                           GIT_COMMITTER_NAME='Synthetic Fixture',
                           GIT_COMMITTER_EMAIL='fixture@example.invalid',
                           GIT_NO_LAZY_FETCH='1', GIT_TERMINAL_PROMPT='0')

        def git(*arguments, cwd=origin, allow_lazy=False):
            selected_env = dict(environment, GIT_NO_LAZY_FETCH='0') if allow_lazy else environment
            return subprocess.check_output(['git', *arguments], cwd=cwd, env=selected_env,
                                           stderr=subprocess.STDOUT, text=True).strip()

        git('init', '--initial-branch=main')
        (origin / 'input.txt').write_text('old synthetic bytes\n')
        git('add', 'input.txt')
        git('commit', '-m', 'synthetic old input')
        old_blob = git('rev-parse', 'HEAD:input.txt')
        (origin / 'input.txt').write_text('current synthetic bytes\n')
        git('add', 'input.txt')
        git('commit', '-m', 'synthetic selected input')
        selected = git('rev-parse', 'HEAD')
        git('config', 'uploadpack.allowFilter', 'true')
        source = output / 'partial-template'
        # Prepare an actual local partial clone, then materialize only its
        # selected working tree. No external network or user cache is involved.
        git('clone', '--filter=blob:none', '--no-local', '--no-checkout',
            str(origin), str(source), cwd=output)
        git('-c', 'protocol.file.allow=always', 'checkout', '--detach', selected,
            cwd=source, allow_lazy=True)
        git('config', 'remote.origin.url', str(output / 'unavailable-origin'), cwd=source)
        self.assertEqual(git('show', 'HEAD:input.txt', cwd=source), 'current synthetic bytes')
        with self.assertRaises(subprocess.CalledProcessError):
            git('cat-file', '-e', old_blob, cwd=source)
        cache = output / 'cache'
        owner = output / 'owner'
        (owner / 'tests/ci').mkdir(parents=True)
        for name in ('ci_support.py', 'prepare-workspace.py'):
            shutil.copy2(ROOT / 'tests' / name, owner / 'tests' / name)
        pins = copy.deepcopy(json.loads((ROOT / 'tests/ci/pins.json').read_text()))
        for projects in pins['cores'].values():
            for project in projects:
                project['revision'] = selected
        for project in pins['cores']['upstream']:
            destination = cache / project['path']
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copytree(source, destination)
        (owner / 'tests/ci/pins.json').write_text(json.dumps(pins))
        command = [sys.executable, str(owner / 'tests/prepare-workspace.py'), '--core', 'upstream',
                   '--output', str(output / 'prepared'), '--from-cache', str(cache)]
        result = subprocess.run(command, env=environment, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, timeout=90)
        (output / 'raw.log').write_text(result.stdout)
        self.assertEqual(result.returncode, 0, result.stdout)
        # The prepared workspace must remain readable if the original cache is
        # unmounted; this tests behavior rather than a particular clone flag.
        cache.rename(output / 'retained-source-cache')
        for project in pins['cores']['upstream']:
            prepared = output / 'prepared' / project['path']
            self.assertEqual(git('rev-parse', 'HEAD', cwd=prepared), selected)
            self.assertEqual((prepared / 'input.txt').read_text(), 'current synthetic bytes\n')
            readable = subprocess.run(['git', 'show', 'HEAD:input.txt'], cwd=prepared,
                                      env=environment, stdout=subprocess.PIPE,
                                      stderr=subprocess.STDOUT, text=True)
            self.assertEqual(readable.returncode, 0,
                             'prepared input still requires the unmounted cache: ' + readable.stdout)
            self.assertEqual(readable.stdout.strip(), 'current synthetic bytes')
        self.assertEqual(git('rev-parse', 'HEAD'), selected)
        with self.assertRaises(subprocess.CalledProcessError):
            git('cat-file', '-e', old_blob, cwd=source)


if __name__ == '__main__':
    unittest.main(verbosity=2)
