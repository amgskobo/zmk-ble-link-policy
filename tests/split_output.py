# SPDX-License-Identifier: MIT
"""Associate a split fault with its specified assertion and completed case."""
from native_output import check_run


def check_control(output, returncode, case, assertion_site):
    check_run(output, returncode, 1, expect_assertion=True)
    assert "PROJECT EXECUTION FAILED" in output, "Incomplete split control suite"
    marker = "START - " + case + "\n"
    assert marker in output, "Missing designated split control case"
    block = output.split(marker, 1)[1].split("\n===================================================================", 1)[0]
    assert "Assertion failed" in block, "Designated case has no assertion"
    assert ": " + assertion_site + ":" in block, "Wrong assertion site"
    assert " FAIL - " + case + " in " in block, "Designated case did not fail"
