# SPDX-License-Identifier: GPL-3.0-only
from pathlib import Path
import re
import unittest
ROOT = Path(__file__).resolve().parents[1]
class Ownership(unittest.TestCase):
    def test_no_application_or_interposition_dependency(self):
        for path in (ROOT / 'src').glob('*'):
            text = path.read_text()
            for forbidden in ('zmk_ble_messenger', 'zmk/studio', '__wrap_', '__real_', 'malloc(', 'bt_le_adv_', 'bt_le_ext_adv_'):
                self.assertNotIn(forbidden, text, str(path))
        self.assertNotIn('zephyr_ld_options', (ROOT / 'CMakeLists.txt').read_text())
    def test_explicit_host_role_gate(self):
        text = (ROOT / 'Kconfig').read_text()
        self.assertIn('depends on !ZMK_SPLIT || ZMK_SPLIT_ROLE_CENTRAL', text)
        self.assertIn('depends on ZMK_BLE && BT_PERIPHERAL', text)
        self.assertIn('default n', text)
        self.assertNotRegex(text, r'(?m)^\s*(select|imply) ')
    def test_migration_keeps_expected_policy_cases(self):
        text = (ROOT / 'tests/native/idle-links/main.c').read_text()
        self.assertEqual(len(re.findall(r'ZTEST\(', text)), 32)
        self.assertEqual(len(re.findall(r'ZTEST\(', (ROOT / 'tests/native/idle-links/fast.c').read_text())), 9)
        self.assertNotIn('zmk_ble_messenger', text)
