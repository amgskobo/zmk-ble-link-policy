# SPDX-License-Identifier: MIT
"""Synthetic log controls; these fixtures are not kernel executions."""
import unittest
from split_output import check_control


class SplitControlAdmission(unittest.TestCase):
    def output(self):
        return ("START - test_split_wake\n"
                "Assertion failed at fixture.c:71: require_fast_restore: requests != 2\n"
                " FAIL - test_split_wake in 0.010 seconds\n"
                "===================================================================\n"
                "PROJECT EXECUTION FAILED\n")

    def test_designated_assertion_is_admitted(self):
        check_control(self.output(), 1, 'test_split_wake', 'require_fast_restore')

    def test_zero_exit_and_incomplete_suite_reject(self):
        for output, code in [(self.output(), 0),
                             (self.output().replace('PROJECT EXECUTION FAILED', ''), 1)]:
            with self.subTest(code=code, output=output), self.assertRaises(AssertionError):
                check_control(output, code, 'test_split_wake', 'require_fast_restore')

    def test_unrelated_assertion_or_case_failure_reject(self):
        for before, after in [('require_fast_restore', 'unrelated'),
                              ('START - test_split_wake', 'START - test_other'),
                              (' FAIL - test_split_wake', ' PASS - test_split_wake')]:
            with self.subTest(before=before), self.assertRaises(AssertionError):
                check_control(self.output().replace(before, after), 1,
                              'test_split_wake', 'require_fast_restore')

    def test_assertion_in_a_different_case_cannot_supply_the_failure(self):
        output = self.output().replace('Assertion failed at fixture.c:71: require_fast_restore: requests != 2\n', '')
        output += '\nSTART - test_other\nAssertion failed at fixture.c:71: require_fast_restore: x\n'
        with self.assertRaises(AssertionError):
            check_control(output, 1, 'test_split_wake', 'require_fast_restore')

    def test_recovering_sanitizer_never_kills_a_runtime_control(self):
        for diagnostic in ['fixture.c:12:3: runtime error: signed overflow\n',
                           'ERROR: AddressSanitizer: heap-buffer-overflow\n']:
            with self.subTest(diagnostic=diagnostic), self.assertRaisesRegex(AssertionError, 'Sanitizer'):
                check_control(self.output() + diagnostic, 1, 'test_split_wake', 'require_fast_restore')

    def test_compiler_failure_is_not_a_runtime_control(self):
        with self.assertRaises(AssertionError):
            check_control('error: invalid source\n', 1, 'test_split_wake', 'require_fast_restore')
