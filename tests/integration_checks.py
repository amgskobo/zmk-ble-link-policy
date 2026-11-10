# SPDX-License-Identifier: GPL-3.0-only
"""Validate the policy-specific Kconfig rejection independent of line wrapping."""
import re


def rejected_peripheral_warning(log):
    warning = re.search(r"^warning: ZMK_BLE_LINK_POLICY\b.*?(?=\n\s*\n|\Z)",
                        log, flags=re.MULTILINE | re.DOTALL)
    if warning is None:
        return False
    text = " ".join(warning.group().split())
    return ("was assigned the value 'y' but got the value 'n'." in text
            and "unsatisfied dependencies: (!ZMK_SPLIT || ZMK_SPLIT_ROLE_CENTRAL)" in text)


def rejected_split_warning(log, dependency):
    warning = re.search(r"^warning: ZMK_BLE_SPLIT_POLICY\b.*?(?=\n\s*\n|\Z)",
                        log, flags=re.MULTILINE | re.DOTALL)
    if warning is None:
        return False
    text = " ".join(warning.group().split())
    return ("was assigned the value 'y' but got the value 'n'." in text
            and 'unsatisfied dependencies:' in text and dependency in text)
