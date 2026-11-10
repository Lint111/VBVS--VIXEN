#!/usr/bin/env python3
"""Summarise a sample-build.ps1 CSV. Early runs wrote numbers with a thousands separator ("1,234"); rows
are re-parsed by trying every merge of the comma-split tokens and keeping the one that satisfies
count*max >= sum >= max (cl.exe memory), which is unambiguous in practice."""
import itertools, sys

def parse(line, ncols):
    tok = line.strip().split(",")
    if ncols >= 9:
        return [float(x) for x in tok]
    if len(tok) == 8:
        return [float(x) for x in tok] + [float("nan")] * 3
    # t may merge, cpu is the token containing '.', then 6 more fields, any numeric one may merge
    ci = next(i for i, x in enumerate(tok) if "." in x)
    t = float("".join(tok[:ci])); cpu = float(tok[ci]); rest = tok[ci + 1:]
    # rest -> count,max,sum,free,lcount,lsum ; count and lcount are < 1000 single tokens
    best = []
    def split(i, fields):
        if len(fields) == 6:
            if i == len(rest): best.append(list(fields))
            return
        for n in (1, 2, 3):
            if i + n > len(rest): break
            chunk = rest[i:i + n]
            if n > 1 and (any(len(c) != 3 for c in chunk[1:]) or len(chunk[0]) > 3): continue
            split(i + n, fields + [float("".join(chunk))])
    split(0, [])
    ok = [b for b in best if b[0] <= 64 and b[4] <= 64 and (b[0] == 0 or b[1] <= b[2] <= b[0] * b[1] + 1) and (b[0] > 0 or b[1] == b[2] == 0)]
    if len(ok) != 1: return None
    return [t, cpu] + ok[0] + [float("nan")] * 3

rows, bad = [], 0
lines = open(sys.argv[1]).read().splitlines()
ncols = len(lines[0].split(","))
for line in lines[1:]:
    r = parse(line, ncols)
    if r: rows.append(r)
    else: bad += 1
cl = [r for r in rows if r[2] > 0]
print(f"samples={len(rows)} unresolved_rows={bad} cpu_mean={sum(r[1] for r in rows)/len(rows):.1f}% "
      f"cpu_mean_while_cl={sum(r[1] for r in cl)/max(1,len(cl)):.1f}% cpu_peak={max(r[1] for r in rows):.0f}% "
      f"cl_peak_count={int(max(r[2] for r in rows))} cl_peak_single_ws_mb={int(max(r[3] for r in rows))} "
      f"cl_peak_sum_ws_mb={int(max(r[4] for r in rows))} win_free_mb_min={int(min(r[5] for r in rows))} "
      f"link_peak_sum_ws_mb={int(max(r[7] for r in rows))} win_avail_mb_min={min((r[8] for r in rows if r[8]==r[8]), default=float('nan')):.0f} "
      f"cl_peak_single_private_mb={max((r[9] for r in rows if len(r)>9 and r[9]==r[9]), default=float('nan')):.0f} cl_peak_sum_private_mb={max((r[10] for r in rows if len(r)>10 and r[10]==r[10]), default=float('nan')):.0f}")
