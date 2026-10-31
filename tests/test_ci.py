# SPDX-License-Identifier: GPL-3.0-only
"""CI selection, immutable workspaces and failure/provenance regression guards."""
import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import yaml
from ci_support import ROOT, execute, load_pins, source_identity, workspace_identity

spec = importlib.util.spec_from_file_location('policy_ci_runner', ROOT / 'tests/run-ci.py')
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class CI(unittest.TestCase):
    def test_fixed_graph_rejects_floating_credentials_and_aliased_paths(self):
        pins = load_pins()
        mutations = [
            lambda p: p['cores']['upstream'][0].update(revision='main'),
            lambda p: p['cores']['upstream'][0].update(imports=True),
            lambda p: p['cores']['upstream'][1].pop('west-commands'),
            lambda p: p['cores']['upstream'][0].update(url='https://token@github.com/owner/repo'),
            lambda p: p['cores']['dya'][0].update(path='../outside'),
            lambda p: p['cores']['dya'][0].update(path='/outside'),
            lambda p: p['cores']['dya'][0].update(path='modules'),
            lambda p: p['cores']['upstream'].pop(),
            lambda p: p['cores'].pop('dya'),
            lambda p: p.update(image='zmkfirmware/zmk-build-arm:stable'),
        ]
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'pins.json'
            for mutation in mutations:
                invalid = copy.deepcopy(pins)
                mutation(invalid)
                path.write_text(json.dumps(invalid), encoding='utf-8')
                with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                    load_pins(path)

    def test_wrong_actual_head_is_rejected_before_build(self):
        with patch('ci_support.head', return_value='0' * 40), self.assertRaisesRegex(ValueError, 'Wrong selected revision'):
            workspace_identity(Path('/unchanged'), 'upstream')

    def test_existing_workspace_never_fetches_or_replaces_content(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            marker = path / 'keep'
            marker.write_text('original', encoding='utf-8')
            result = subprocess.run([sys.executable, str(ROOT / 'tests/prepare-workspace.py'),
                                     '--core', 'upstream', '--output', str(path), '--allow-network'],
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('Workspace already exists', result.stdout)
            self.assertEqual(sorted(p.name for p in path.iterdir()), ['keep'])
            self.assertEqual(marker.read_text(), 'original')

    def test_failed_command_keeps_actual_exit_order_and_times(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp)
            with self.assertRaises(RuntimeError):
                execute([sys.executable, '-c', "print('first'); print('second'); raise SystemExit(7)"],
                        output, output, 'failure')
            facts = json.loads((output / 'failure.json').read_text())
            self.assertEqual(facts['exit'], 7)
            self.assertGreaterEqual(facts['end'], facts['start'])
            self.assertEqual((output / 'failure.log').read_text().splitlines(), ['first', 'second'])
            with self.assertRaises(FileExistsError):
                execute([sys.executable, '-c', 'pass'], output, output, 'failure')

    def test_changed_source_cannot_receive_a_passing_lane(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / 'fresh'
            with patch.object(sys, 'argv', ['run-ci.py', '--suite', 'host', '--output', str(output)]), \
                    patch.object(runner, 'source_identity', side_effect=[{'src/a.c': 'before'}, {'src/a.c': 'after'}]), \
                    patch.object(runner, 'head', return_value='1' * 40), \
                    patch.object(runner, 'execute', return_value={}):
                with self.assertRaises(SystemExit) as failure:
                    runner.main()
            self.assertEqual(failure.exception.code, 1)
            facts = json.loads((output / 'result.json').read_text())
            self.assertEqual(facts['status'], 'FAIL')
            self.assertFalse(facts['source_unchanged'])
            self.assertNotEqual((output / 'source-before.json').read_text(), (output / 'source-after.json').read_text())

    def test_all_original_lanes_and_both_cores_are_in_hosted_dispatch(self):
        data = yaml.load((ROOT / '.github/workflows/test.yml').read_text(), Loader=yaml.BaseLoader)
        job = data['jobs']['test']
        self.assertEqual(data['permissions'], {'contents': 'read'})
        self.assertEqual(job['container']['image'], load_pins()['image'])
        lanes = {(row['suite'], row['core']) for row in job['strategy']['matrix']['include']}
        self.assertEqual(lanes, {('host', 'upstream'), ('native', 'upstream'),
                                 ('integration', 'upstream'), ('integration', 'dya')})
        artifact = next(step for step in job['steps'] if step.get('uses', '').startswith('actions/upload-artifact@'))
        self.assertEqual(artifact['if'], 'always()')
        self.assertEqual(artifact['with']['include-hidden-files'], 'true')
        self.assertIn('preparation', artifact['with']['path'])

    def test_host_native_and_integration_dispatch_owner_entries_without_partial_filters(self):
        for suite, entry in [('host', 'tests/run-host.py'), ('native', 'tests/mutation/mutate_link_service.py'),
                             ('integration', 'tests/run-integration.py')]:
            command = runner.lane_command(suite, Path('/workspace'), Path('/evidence'), Path('/build'))
            self.assertEqual(command[1], entry)
            self.assertNotIn('--only', command)
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'src').mkdir()
            (root / 'src/real.c').write_text('source')
            (root / '__pycache__').mkdir()
            (root / '__pycache__/ignored').write_text('runtime')
            self.assertEqual(set(source_identity(root)), {'src/real.c'})
