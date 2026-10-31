# SPDX-License-Identifier: GPL-3.0-only
import unittest
from integration_checks import rejected_peripheral_warning


class RejectionWarning(unittest.TestCase):
    warning = ("warning: ZMK_BLE_LINK_POLICY (defined at /fixture/module/Kconfig:2) was "
               "assigned the value 'y' but got the value 'n'. Check these unsatisfied "
               "dependencies: (!ZMK_SPLIT || ZMK_SPLIT_ROLE_CENTRAL) (=n). See docs.")

    def test_exact_warning_with_or_without_wrapping(self):
        self.assertTrue(rejected_peripheral_warning(self.warning))
        self.assertTrue(rejected_peripheral_warning(
            self.warning.replace("was assigned", "was\nassigned")
                        .replace("|| ZMK", "||\nZMK")))

    def test_other_symbol_and_wrong_values_or_expression_are_rejected(self):
        for wrong in (self.warning.replace("warning: ZMK_BLE_LINK_POLICY", "warning: ZMK_USB"),
                      self.warning.replace("got the value 'n'", "got the value 'y'"),
                      self.warning.replace("|| ZMK_SPLIT_ROLE_CENTRAL", "&& ZMK_SPLIT_ROLE_CENTRAL"),
                      self.warning.replace("ZMK_SPLIT_ROLE_CENTRAL", "OTHER_ROLE")):
            with self.subTest(wrong=wrong):
                self.assertFalse(rejected_peripheral_warning(wrong))

    def test_unrelated_warning_cannot_supply_the_dependency(self):
        log = self.warning.split(" Check these")[0] + "\n\n" + self.warning.replace(
            "warning: ZMK_BLE_LINK_POLICY", "warning: ZMK_USB")
        self.assertFalse(rejected_peripheral_warning(log))


if __name__ == "__main__":
    unittest.main()
