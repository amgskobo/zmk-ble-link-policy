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

    def test_split_has_an_independent_opt_in_and_central_only_source(self):
        text = (ROOT / 'Kconfig').read_text().split('config ZMK_BLE_SPLIT_POLICY\n', 1)[1]
        text = text.split('config ZMK_BLE_SPLIT_POLICY_IDLE_INTERVAL', 1)[0]
        self.assertIn('ZMK_SPLIT_BLE && ZMK_SPLIT_ROLE_CENTRAL && BT_CENTRAL', text)
        self.assertIn('ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS = 1', text)
        self.assertNotIn('depends on ZMK_BLE_LINK_POLICY', text)
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        self.assertIn('if(CONFIG_ZMK_BLE_SPLIT_POLICY)', cmake)
        self.assertIn('src/split_service.c', cmake)
        source = (ROOT / 'src/split_service.c').read_text()
        self.assertNotIn('zmk_endpoints_selected', source)
        self.assertNotIn('zmk_ble_profile', source)
        self.assertIn('BT_CONN_ROLE_CENTRAL', source)

    def test_split_migration_retains_all_prior_case_families(self):
        path = ROOT / 'tests/native/split-links'
        self.assertEqual(len(re.findall(r'ZTEST\(', (path / 'main.c').read_text())), 23)
        self.assertEqual(len(re.findall(r'ZTEST\(', (path / 'activity.c').read_text())), 9)
        for name in ['split_service.c', 'split_policy.h']:
            self.assertNotIn('AKKB46', (ROOT / 'src' / name).read_text())
