# SPDX-License-Identifier: GPL-3.0-only
"""Native sanitizer admission and preserved baseline/runtime-assertion gates."""
import unittest
from native_output import check_run

SUCCESS = "pass = 32, fail = 0, skip = 0, total = 32\nPROJECT EXECUTION SUCCESSFUL\n"


class NativeOutput(unittest.TestCase):
    def test_complete_clean_output_and_locationless_labels_are_accepted(self):
        for label in ("", "runtime error: expected parser label\n",
                      "test_runtime_error: boundary\n", "source.c: runtime error: label\n"):
            with self.subTest(label=label):
                check_run(label + SUCCESS, 0, 32)

    def test_real_report_forms_are_rejected_despite_success(self):
        reports = [
            "==42==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x123\n",
            "SUMMARY: AddressSanitizer: global-buffer-overflow control.c:12\n",
            "AddressSanitizer:DEADLYSIGNAL\n",
            "==42==ERROR: LeakSanitizer: detected memory leaks\n",
            "control.c:12:8: runtime error: signed integer overflow\n",
            "/synthetic/path/control.c:12: runtime error: invalid shift\n",
            "SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior control.c:12\n",
            "UndefinedBehaviorSanitizer:DEADLYSIGNAL\n",
        ]
        for report in reports:
            for output in (report + SUCCESS, SUCCESS + report):
                with self.subTest(report=report), self.assertRaisesRegex(
                        AssertionError, "^Sanitizer diagnostic in native output:"):
                    check_run(output, 0, 32)

    def test_original_exit_markers_counts_and_zero_skips_are_required(self):
        for output, code in [(SUCCESS, 1), (SUCCESS.replace("PROJECT EXECUTION SUCCESSFUL", ""), 0),
                             (SUCCESS.replace("pass = 32", "pass = 31"), 0),
                             (SUCCESS.replace("skip = 0", "skip = 1"), 0),
                             (SUCCESS.replace("total = 32", "total = 0"), 0), ("", 0)]:
            with self.subTest(output=output, code=code), self.assertRaises(AssertionError):
                check_run(output, code, 32)

    def test_runtime_fault_needs_its_assertion_and_no_sanitizer(self):
        check_run("Assertion failed at control.c:42\n", 1, 32, expect_assertion=True)
        for output, code in [("Assertion failed\n", 0), ("build failed\n", 1),
                             ("timeout\n", 124),
                             ("control.c:42:8: runtime error: overflow\nAssertion failed\n", 1)]:
            with self.subTest(output=output, code=code), self.assertRaises(AssertionError):
                check_run(output, code, 32, expect_assertion=True)


if __name__ == "__main__":
    unittest.main()
