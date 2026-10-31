#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Decode numeric completed-worker observations; never infer delivery or CPU use."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re

FIELDS = {'v', 'seq', 'work_us', 'api_calls', 'api_us', 'api_max_us', 'invalid',
          'pending_ms', 'active_slot', 'host_mask', 'stale'}
PREFIX = 'zblp_idle_links: linkwork '


def decode(line):
    clean = re.sub(r'\x1b\[[0-9;]*m', '', line)
    if PREFIX not in clean:
        return None
    body = clean.split(PREFIX, 1)[1].strip()
    if not re.fullmatch(r'\w+=\d+(?: \w+=\d+)*', body):
        raise ValueError('Malformed numeric linkwork row')
    pairs = [p.split('=') for p in body.split()]
    if len(pairs) != len(FIELDS) or {p[0] for p in pairs} != FIELDS:
        raise ValueError('Incomplete, duplicate or extra linkwork field')
    values = {k: int(v) for k, v in pairs}
    if any(not 0 <= v <= 0xFFFFFFFF for v in values.values()):
        raise ValueError('Out-of-range linkwork value')
    if (values['v'] != 1 or not 1 <= values['api_calls'] <= 5 or
            values['active_slot'] not in (*range(5), 255) or
            values['host_mask'] > 31 or values['stale'] > 1):
        raise ValueError('Unsupported schema or invalid linkwork state')
    if values['invalid'] == 0 and not (
            values['api_max_us'] <= values['api_us'] <= values['work_us'] and
            values['api_us'] <= values['api_max_us'] * values['api_calls']):
        raise ValueError('Inconsistent linkwork timing')
    stamp = re.search(r'\[(\d+:\d+:\d+[.,]\d+(?:,\d+)?)\]', clean)
    return dict(values, device_timestamp=stamp[1] if stamp else None)


def summarize(records, limit=1):
    if not 1 <= limit <= 5:
        raise ValueError('Invalid request limit')
    rows = []
    previous = None
    groups = {}
    for index, record in enumerate(records):
        row = decode(record['line'])
        if row is None:
            continue
        if previous is not None and not 0 < ((row['seq'] - previous) & 0xFFFFFFFF) < 0x80000000:
            raise ValueError('Duplicate, reordered or reset worker sequence')
        previous = row['seq']
        row.update(order=index, host_utc=record.get('host_utc'),
                   capture_seconds=record.get('capture_seconds'), raw_line=record['line'])
        rows.append(row)
        key = (row['active_slot'], row['host_mask'], row['stale'])
        groups.setdefault(key, []).append(row)
    cohorts = []
    for (slot, mask, stale), cohort in groups.items():
        valid = [r for r in cohort if r['invalid'] == 0]
        result = dict(active_slot=slot, host_mask=mask, stale=stale, samples=len(cohort),
                      valid_timing_samples=len(valid), invalid_timing_samples=len(cohort)-len(valid))
        for field in ('work_us', 'api_us', 'api_max_us'):
            ordered = sorted(r[field] for r in valid)
            if ordered:
                peak = max(valid, key=lambda r: r[field])
                result[field] = dict(mean=sum(ordered)/len(ordered),
                                     p50=ordered[math.ceil(len(ordered)*0.50)-1],
                                     p95=ordered[math.ceil(len(ordered)*0.95)-1], max=ordered[-1],
                                     peak_seq=peak['seq'], peak_order=peak['order'])
            else:
                result[field] = None
        cohorts.append(result)
    return dict(status='OBSERVED' if rows else 'UNAVAILABLE', samples=len(rows),
                request_limit=limit,
                request_limit_check=('PASS' if all(r['api_calls'] <= limit for r in rows)
                                     else 'FAIL') if rows else 'NOT RUN',
                rows=rows, cohorts=cohorts,
                boundary='Completed request workers only. Wall time includes scheduling and blocking; '
                         'excludes the final summary log. No CPU, radio, host HID, idle or stability verdict.')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--console', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--request-limit', type=int, default=1)
    a = ap.parse_args()
    a.output.mkdir(parents=True, exist_ok=False)
    try:
        records = [json.loads(line) for line in a.console.read_text(encoding='utf-8').splitlines()]
        result = summarize(records, a.request_limit)
    except (ValueError, KeyError, TypeError) as error:
        result = dict(status='INVALID', error=str(error), request_limit_check='NOT RUN')
    result['source_sha256'] = hashlib.sha256(a.console.read_bytes()).hexdigest()
    (a.output / 'result.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({k: v for k, v in result.items() if k not in ('rows', 'cohorts')}, indent=2))
    raise SystemExit(result['status'] == 'INVALID' or result['request_limit_check'] == 'FAIL')


if __name__ == '__main__':
    main()
