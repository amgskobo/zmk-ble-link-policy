# SPDX-License-Identifier: GPL-3.0-only
"""Exercise dependency cleanliness and lane drift with real synthetic Git trees."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from ci_support import ROOT, workspace_identity

spec = importlib.util.spec_from_file_location('dependency_ci_runner', ROOT / 'tests/run-ci.py')
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class DependencyIdentity(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='zblp-dependency-identity-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.workspace = self.root / 'workspace'
        self.dependency = self.workspace / 'zmk'
        self.dependency.mkdir(parents=True)
        self.environment = dict(os.environ, GIT_CONFIG_GLOBAL=os.devnull,
                                GIT_CONFIG_NOSYSTEM='1',
                                GIT_AUTHOR_NAME='Synthetic Fixture',
                                GIT_AUTHOR_EMAIL='fixture@example.invalid',
                                GIT_COMMITTER_NAME='Synthetic Fixture',
                                GIT_COMMITTER_EMAIL='fixture@example.invalid')
        self.git('init', '--quiet', '--initial-branch=main')
        self.source = self.dependency / 'source.c'
        self.source.write_text('int fixture(void) { return 1; }\n')
        (self.dependency / '.gitignore').write_text('__pycache__/\n')
        self.git('add', 'source.c', '.gitignore')
        self.git('commit', '--quiet', '-m', 'synthetic dependency')
        self.revision = self.git('rev-parse', 'HEAD')
        self.pins = {'cores': {'upstream': [
            dict(name='zmk', path='zmk', revision=self.revision)]}}

    def git(self, *arguments):
        return subprocess.check_output(['git', *arguments], cwd=self.dependency,
                                       env=self.environment, stderr=subprocess.STDOUT,
                                       text=True).strip()

    def identity(self, workspace=None, core='upstream'):
        return workspace_identity(workspace or self.workspace, core, self.pins)

    def run_lane(self, action=None, failed=False):
        output = self.root / 'result'
        with patch.object(sys, 'argv', ['run-ci.py', '--suite', 'native',
                                       '--workspace', str(self.workspace),
                                       '--output', str(output)]), \
                patch.object(runner, 'workspace_identity', side_effect=self.identity), \
                patch.object(runner, 'execute', side_effect=action) as execute:
            if failed:
                with self.assertRaises(SystemExit) as failure:
                    runner.main()
                self.assertEqual(failure.exception.code, 1)
            else:
                runner.main()
        return json.loads((output / 'result.json').read_text()), execute

    def test_clean_dependency_and_ignored_runtime_cache_are_accepted(self):
        cache = self.dependency / '__pycache__'
        cache.mkdir()
        (cache / 'generated.pyc').write_bytes(b'synthetic runtime cache')
        result = self.identity()
        self.assertEqual(result[0]['actual_revision'], self.revision)
        self.assertTrue(result[0]['working_tree_clean'])

    def test_unstaged_source_change_is_rejected_without_repair(self):
        self.source.write_text('int fixture(void) { return 2; }\n')
        before = self.git('status', '--porcelain=v1')
        with self.assertRaisesRegex(ValueError, 'Dependency source differs'):
            self.identity()
        self.assertEqual(self.git('rev-parse', 'HEAD'), self.revision)
        self.assertEqual(self.git('status', '--porcelain=v1'), before)
        self.assertIn('return 2', self.source.read_text())

    def test_staged_change_and_tracked_deletion_are_rejected(self):
        self.source.write_text('int fixture(void) { return 3; }\n')
        self.git('add', 'source.c')
        with self.assertRaisesRegex(ValueError, 'Dependency source differs'):
            self.identity()
        self.git('reset', '--quiet', 'HEAD', '--', 'source.c')
        self.source.unlink()
        with self.assertRaisesRegex(ValueError, 'Dependency source differs'):
            self.identity()

    def test_untracked_source_is_rejected(self):
        (self.dependency / 'extra.c').write_text('int extra(void) { return 0; }\n')
        with self.assertRaisesRegex(ValueError, 'Dependency source differs'):
            self.identity()

    def test_clean_lane_records_both_dependency_checks(self):
        facts, execute = self.run_lane()
        execute.assert_called_once()
        self.assertEqual(facts['status'], 'PASS')
        self.assertTrue(facts['dependencies_unchanged'])
        self.assertEqual(facts['dependencies'], facts['dependencies_after'])

    def test_dirty_dependency_stops_the_lane_before_execution(self):
        self.source.write_text('int fixture(void) { return 4; }\n')
        facts, execute = self.run_lane(failed=True)
        execute.assert_not_called()
        self.assertEqual(facts['status'], 'FAIL')
        self.assertIn('Dependency source differs', facts['error'])

    def test_source_change_during_a_successful_command_fails_the_lane(self):
        def change(*unused):
            self.source.write_text('int fixture(void) { return 5; }\n')
        facts, execute = self.run_lane(change, failed=True)
        execute.assert_called_once()
        self.assertFalse(facts['dependencies_unchanged'])
        self.assertIn('Selected dependencies changed during the lane', facts['error'])

    def test_revision_change_during_a_successful_command_fails_the_lane(self):
        def change(*unused):
            self.git('commit', '--quiet', '--allow-empty', '-m', 'synthetic drift')
        facts, execute = self.run_lane(change, failed=True)
        execute.assert_called_once()
        self.assertFalse(facts['dependencies_unchanged'])
        self.assertIn('Wrong selected revision', facts['error'])


if __name__ == '__main__':
    unittest.main()
