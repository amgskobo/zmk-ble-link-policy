# SPDX-License-Identifier: GPL-3.0-only
import unittest
from observations import decode, summarize


def sample(seq=1, calls=1, work=30, api=20, maximum=20, invalid=0, slot=0, mask=31, stale=0):
    return dict(host_utc='2026-10-26T15:00:00+00:00', capture_seconds=0.5,
                line=f'[00:00:01.002,003] <inf> zblp_idle_links: linkwork v=1 seq={seq} '
                     f'work_us={work} api_calls={calls} api_us={api} api_max_us={maximum} '
                     f'invalid={invalid} pending_ms=4294967295 active_slot={slot} '
                     f'host_mask={mask} stale={stale}')


class Observations(unittest.TestCase):
    def test_empty_invalid_timing_and_zero_are_distinct(self):
        self.assertEqual(summarize([])['status'], 'UNAVAILABLE')
        result = summarize([sample(work=0, api=0, maximum=0), sample(2, invalid=1)])
        self.assertEqual(result['request_limit_check'], 'PASS')
        self.assertEqual(result['cohorts'][0]['valid_timing_samples'], 1)
        self.assertEqual(result['cohorts'][0]['invalid_timing_samples'], 1)
        self.assertEqual(result['cohorts'][0]['work_us']['max'], 0)

    def test_order_clocks_wrap_and_peak_provenance(self):
        records = [sample(0xFFFFFFFF, work=40), sample(0, work=50), sample(4, work=30)]
        result = summarize(records)
        self.assertEqual([r['seq'] for r in result['rows']], [0xFFFFFFFF, 0, 4])
        self.assertEqual(result['rows'][0]['device_timestamp'], '00:00:01.002,003')
        self.assertEqual(result['rows'][1]['raw_line'], records[1]['line'])
        self.assertEqual(result['cohorts'][0]['work_us']['p95'], 50)
        self.assertEqual(result['cohorts'][0]['work_us']['peak_seq'], 0)
        for seqs in [(1, 1), (2, 1), (1, 0x80000001)]:
            with self.subTest(seqs=seqs), self.assertRaises(ValueError):
                summarize([sample(s) for s in seqs])

    def test_topologies_and_stale_scans_stay_separate(self):
        result = summarize([sample(1), sample(2, slot=255, mask=1), sample(3, stale=1)])
        self.assertEqual(len(result['cohorts']), 3)
        self.assertEqual(sum(g['samples'] for g in result['cohorts']), 3)

    def test_quota_is_a_separate_check_and_baseline_limit_is_explicit(self):
        record = sample(calls=4, work=90, api=80, maximum=20)
        self.assertEqual(summarize([record])['request_limit_check'], 'FAIL')
        self.assertEqual(summarize([record], 5)['request_limit_check'], 'PASS')

    def test_api_sum_and_maximum_match_the_recorded_call_count(self):
        for calls, api, maximum, work in [(1, 20, 20, 30), (5, 50, 10, 60),
                                           (5, 10, 10, 20), (1, 0, 0, 0)]:
            with self.subTest(valid=(calls, api, maximum, work)):
                row = decode(sample(calls=calls, api=api, maximum=maximum, work=work)['line'])
                self.assertEqual(row['api_us'], api)
                self.assertEqual(row['api_max_us'], maximum)
        for calls, api, maximum, work in [(1, 20, 1, 30), (2, 21, 10, 30),
                                           (5, 51, 10, 60), (1, 1, 0, 2)]:
            with self.subTest(invalid=(calls, api, maximum, work)):
                with self.assertRaisesRegex(ValueError, 'Inconsistent linkwork timing'):
                    decode(sample(calls=calls, api=api, maximum=maximum, work=work)['line'])
        # Invalid clock spans retain their raw values without validating timing.
        invalid = summarize([sample(api=20, maximum=1, invalid=1)])
        self.assertEqual(invalid['cohorts'][0]['invalid_timing_samples'], 1)
        self.assertIsNone(invalid['cohorts'][0]['api_us'])

    def test_malformed_schemas_bounds_and_timing_are_rejected(self):
        line = sample()['line']
        for bad in [line.replace('v=1', 'v=2'), line+' extra=1', line+' seq=2',
                    line.replace('api_calls=1', 'api_calls=0'),
                    line.replace('host_mask=31', 'host_mask=32'),
                    line.replace('api_us=20', 'api_us=31'),
                    line.replace('api_max_us=20', 'api_max_us=21'),
                    line.replace('seq=1', 'seq=4294967296')]:
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                decode(bad)
