#!/usr/bin/env python3
"""
compare_parity.py -- Baked-Perf-Fix-Pipeline M2d automated visual-parity gate.

Consumes TWO bench run directories (each containing run.log, perf.csv,
hud_capture_150.png -- the shape temp_bench/<variant>/ run scripts already
produce) and emits a JSON report plus exactly one final
"PARITY: PASS|FAIL|REPORT <summary>" stdout line.

Two comparison modes (see parity_thresholds.json for the full policy):
  same_path  -- HARD GATE. Compares a run against its own golden (e.g. a fresh
                baked run vs a committed baked golden). Bit-exact equality is
                NOT required (documented ~1-cell near-tie nondeterminism across
                launches) but drift must stay within a small cell-count budget.
  cross_path -- REPORT-ONLY until Milestone M5 (Baked-Perf-Fix-Pipeline-Plan-
                2026-07.md) closes baked-lighting parity. Quantifies the KNOWN
                virtual<->baked lighting divergence without failing the gate.

Metrics extracted:
  - From run.log's [CornellDiag] block: the 25x25 instIdx map (SHA-256 of the
    normalized map text + cell-agreement %%), the set of body-code digits
    (0-7) present, and the out-of-bounds hit count ("NNNNN/NNNNN").
  - From the two PNG captures: mean absolute luminance delta, p99 delta, %%
    pixels differing beyond parity_thresholds.json's pixel_diff_threshold.
    Decoded via Pillow (present on both the WSL and Windows-native Python used
    by this pipeline -- see this file's module docstring tail for the exact
    check). No Pillow -> hard error, not a silent skip, since image metrics
    are load-bearing for cross_path reporting.
  - From perf.csv: per-pass ms means over frames 31-160, EXCLUDING 150 and 151
    (the tick-150/151 CornellDiag-capture spike the pipeline's own bench
    convention already excludes), for both runs, plus their deltas.

Usage:
    python compare_parity.py --mode same_path --run RUN_DIR --golden GOLDEN_TXT
    python compare_parity.py --mode cross_path --run-a BAKED_DIR --run-b VIRTUAL_DIR
    python compare_parity.py --mode same_path --run RUN_DIR --run-b OTHER_RUN_DIR
        (same_path also accepts two live run dirs instead of a golden text file --
         used e.g. to prove byte-identical cold/warm cache artifacts against each
         other, see this repo's M2d gate-proof #1)

Exit code: 0 on PASS/REPORT, 1 on FAIL (same_path only -- cross_path never fails
while "enforced": false in parity_thresholds.json).
"""
import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("PARITY: FAIL missing dependency: Pillow (pip install pillow) is required "
          "to decode hud_capture_150.png -- see this file's docstring.", file=sys.stderr)
    sys.exit(1)

THIS_DIR = Path(__file__).resolve().parent
DEFAULT_THRESHOLDS_PATH = THIS_DIR / "parity_thresholds.json"

INSTIDX_HEADER_RE = re.compile(r"instIdx map \((\d+)x(\d+)")
INSTIDX_ROW_RE = re.compile(r"\|([0-7.]+)\|")
OOB_RE = re.compile(r"out-of-bounds hits.*?:\s*(\d+)/(\d+)")
PERF_EXCLUDED_FRAMES = {150, 151}
PERF_FRAME_LO = 31
PERF_FRAME_HI = 160


class ParityError(RuntimeError):
    """Raised for malformed inputs -- always surfaced as PARITY: FAIL, never swallowed."""


# ---------------------------------------------------------------------------
# run.log parsing -- [CornellDiag] instIdx map + OOB count.
# ---------------------------------------------------------------------------

def parse_cornell_diag(run_log_path: Path) -> dict:
    """Extracts the instIdx map (as normalized text), its SHA-256, the set of
    body-code digits present, and the OOB hit count from a run.log.

    The instIdx map block looks like:
        [CornellDiag] instIdx map (25x25, cell=20px; 0=Lwall ...):
        [CornellDiag] |.........................|
        [CornellDiag] |...4444444444444444444...|
        ... (exactly grid_size rows) ...
    Each row is bounded by '|' on both sides; only [0-7.] characters are legal
    inside. A SEPARATE "worldPos.z depth map" block follows immediately after
    using different glyphs (-=#+.) -- this parser stops as soon as it has
    collected `grid_size` rows so it never accidentally consumes the depth map.
    """
    text = run_log_path.read_text(encoding="utf-8", errors="replace")
    lines = text.splitlines()

    header_idx = None
    grid_size = None
    for i, line in enumerate(lines):
        m = INSTIDX_HEADER_RE.search(line)
        if m:
            header_idx = i
            w, h = int(m.group(1)), int(m.group(2))
            if w != h:
                raise ParityError(f"{run_log_path}: instIdx map is not square ({w}x{h})")
            grid_size = w
            break
    if header_idx is None:
        raise ParityError(f"{run_log_path}: no '[CornellDiag] instIdx map' header found")

    rows = []
    for line in lines[header_idx + 1:]:
        m = INSTIDX_ROW_RE.search(line)
        if not m:
            # Row lines are contiguous immediately after the header in every
            # observed run.log; a non-matching line means the block ended.
            if rows:
                break
            continue
        rows.append(m.group(1))
        if len(rows) == grid_size:
            break

    if len(rows) != grid_size:
        raise ParityError(
            f"{run_log_path}: expected {grid_size} instIdx map rows, found {len(rows)}")
    for r in rows:
        if len(r) != grid_size:
            raise ParityError(
                f"{run_log_path}: instIdx map row width {len(r)} != grid_size {grid_size}: {r!r}")

    normalized_text = "\n".join(rows)
    map_sha256 = hashlib.sha256(normalized_text.encode("utf-8")).hexdigest()
    bodies_present = sorted({c for row in rows for c in row if c != "."})

    oob_match = OOB_RE.search(text)
    if not oob_match:
        raise ParityError(f"{run_log_path}: no 'out-of-bounds hits' line found")
    oob_count, oob_total = int(oob_match.group(1)), int(oob_match.group(2))

    return {
        "grid_size": grid_size,
        "rows": rows,
        "normalized_text": normalized_text,
        "sha256": map_sha256,
        "bodies_present": bodies_present,
        "oob_count": oob_count,
        "oob_total": oob_total,
    }


def cell_agreement(rows_a: list[str], rows_b: list[str]) -> tuple[int, int, int]:
    """Returns (agreeing_cells, differing_cells, total_cells). Rows must be the
    same grid size -- callers check `grid_size` equality before calling this."""
    total = 0
    differing = 0
    for ra, rb in zip(rows_a, rows_b):
        for ca, cb in zip(ra, rb):
            total += 1
            if ca != cb:
                differing += 1
    return total - differing, differing, total


# ---------------------------------------------------------------------------
# perf.csv parsing -- per-pass ms means over frames 31-160 excl. 150/151.
# ---------------------------------------------------------------------------

def parse_perf_csv(perf_csv_path: Path) -> dict:
    import csv
    with perf_csv_path.open(newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        fieldnames = reader.fieldnames or []
        if "frame" not in fieldnames:
            raise ParityError(f"{perf_csv_path}: missing 'frame' column")
        pass_columns = [c for c in fieldnames if c not in ("frame",)]
        sums = {c: 0.0 for c in pass_columns}
        counts = {c: 0 for c in pass_columns}
        for row in reader:
            try:
                frame = int(row["frame"])
            except (TypeError, ValueError):
                continue
            if frame < PERF_FRAME_LO or frame > PERF_FRAME_HI:
                continue
            if frame in PERF_EXCLUDED_FRAMES:
                continue
            for c in pass_columns:
                raw = row.get(c, "")
                if raw in (None, ""):
                    continue
                try:
                    sums[c] += float(raw)
                    counts[c] += 1
                except ValueError:
                    continue
    means = {c: (sums[c] / counts[c] if counts[c] > 0 else None) for c in pass_columns}
    return {"means_ms": means, "frame_count": max(counts.values()) if counts else 0}


# ---------------------------------------------------------------------------
# PNG luminance comparison.
# ---------------------------------------------------------------------------

def load_luminance(png_path: Path) -> list:
    """Returns a flat list of per-pixel luminance (Rec. 601 weights, matching
    the shipped shader's own luminance-threshold convention elsewhere in this
    codebase) as floats in [0,255]."""
    with Image.open(png_path) as im:
        im = im.convert("RGB")
        w, h = im.size
        pixels = list(im.getdata())
    return [0.299 * r + 0.587 * g + 0.114 * b for (r, g, b) in pixels]


def compare_images(png_a: Path, png_b: Path, pixel_diff_threshold: float) -> dict:
    with Image.open(png_a) as ia, Image.open(png_b) as ib:
        if ia.size != ib.size:
            raise ParityError(
                f"image size mismatch: {png_a} is {ia.size}, {png_b} is {ib.size}")
        size = ia.size

    lum_a = load_luminance(png_a)
    lum_b = load_luminance(png_b)
    deltas = sorted(abs(a - b) for a, b in zip(lum_a, lum_b))
    n = len(deltas)
    mean_abs_delta = sum(deltas) / n if n else 0.0
    p99_index = max(0, min(n - 1, int(round(0.99 * (n - 1)))))
    p99_delta = deltas[p99_index] if n else 0.0
    pixels_over = sum(1 for d in deltas if d > pixel_diff_threshold)
    pct_pixels_over = (100.0 * pixels_over / n) if n else 0.0

    return {
        "size": size,
        "mean_abs_luminance_delta": mean_abs_delta,
        "p99_luminance_delta": p99_delta,
        "pct_pixels_over_threshold": pct_pixels_over,
        "pixel_diff_threshold": pixel_diff_threshold,
    }


# ---------------------------------------------------------------------------
# Run-directory loading.
# ---------------------------------------------------------------------------

def load_run_dir(run_dir: Path) -> dict:
    run_log = run_dir / "run.log"
    perf_csv = run_dir / "perf.csv"
    hud_png = run_dir / "hud_capture_150.png"
    for p in (run_log, perf_csv, hud_png):
        if not p.exists():
            raise ParityError(f"run dir {run_dir} is missing expected file: {p.name}")
    return {
        "dir": str(run_dir),
        "cornell_diag": parse_cornell_diag(run_log),
        "perf": parse_perf_csv(perf_csv),
        "hud_png_path": hud_png,
    }


def load_golden_txt(golden_path: Path) -> dict:
    """Golden files are the normalized map text (grid_size rows, one per line)
    plus a header comment block and an OOB line -- see goldens/*.txt for the
    exact committed shape. Lines starting with '#' are header/comments and
    ignored; the first `grid_size` non-comment, non-blank lines matching
    [0-7.]+ are the map; a following 'OOB: N/M' line (if present) supplies the
    OOB count for informational comparison only (same_path's hard gate is the
    map hash/cell-agreement, not OOB drift against a golden)."""
    text = golden_path.read_text(encoding="utf-8", errors="replace")
    rows = []
    oob_count = oob_total = None
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        oob_m = re.match(r"OOB:\s*(\d+)/(\d+)", stripped)
        if oob_m:
            oob_count, oob_total = int(oob_m.group(1)), int(oob_m.group(2))
            continue
        if re.fullmatch(r"[0-7.]+", stripped):
            rows.append(stripped)
    if not rows:
        raise ParityError(f"golden {golden_path}: no instIdx map rows found")
    grid_size = len(rows[0])
    for r in rows:
        if len(r) != grid_size:
            raise ParityError(f"golden {golden_path}: inconsistent row width in {r!r}")
    normalized_text = "\n".join(rows)
    return {
        "grid_size": grid_size,
        "rows": rows,
        "normalized_text": normalized_text,
        "sha256": hashlib.sha256(normalized_text.encode("utf-8")).hexdigest(),
        "bodies_present": sorted({c for row in rows for c in row if c != "."}),
        "oob_count": oob_count,
        "oob_total": oob_total,
    }


def write_golden_txt(cornell_diag: dict, out_path: Path, header_note: str) -> None:
    lines = [f"# {header_note}", "#"]
    lines.extend(cornell_diag["rows"])
    lines.append(f"OOB: {cornell_diag['oob_count']}/{cornell_diag['oob_total']}")
    out_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


# ---------------------------------------------------------------------------
# Comparison modes.
# ---------------------------------------------------------------------------

def compare_same_path(run: dict, golden: dict, thresholds: dict, image_ref: Path | None) -> dict:
    cfg = thresholds["same_path"]
    run_cd = run["cornell_diag"]
    if run_cd["grid_size"] != golden["grid_size"]:
        raise ParityError(
            f"grid size mismatch: run has {run_cd['grid_size']}, golden has {golden['grid_size']}")

    agree, differ, total = cell_agreement(run_cd["rows"], golden["rows"])
    agreement_pct = 100.0 * agree / total if total else 0.0
    hash_equal = run_cd["sha256"] == golden["sha256"]
    cells_within_tolerance = differ <= cfg["instidx_max_differing_cells"]
    map_ok = hash_equal or cells_within_tolerance

    bodies_match = run_cd["bodies_present"] == golden["bodies_present"]
    bodies_ok = bodies_match if cfg["require_bodies_match"] else True

    oob_ok = True
    oob_relative_delta = None
    if golden.get("oob_count") is not None and golden.get("oob_total"):
        golden_rate = golden["oob_count"] / golden["oob_total"] if golden["oob_total"] else 0.0
        run_rate = run_cd["oob_count"] / run_cd["oob_total"] if run_cd["oob_total"] else 0.0
        oob_relative_delta = abs(run_rate - golden_rate) / golden_rate if golden_rate else (
            0.0 if run_rate == 0.0 else float("inf"))
        oob_ok = oob_relative_delta <= cfg["oob_count_max_relative_delta"]

    image_result = None
    image_ok = True
    if image_ref is not None:
        image_result = compare_images(run["hud_png_path"], image_ref, thresholds["pixel_diff_threshold"])
        image_ok = image_result["p99_luminance_delta"] <= cfg["luminance_p99_max_delta"]

    passed = map_ok and bodies_ok and oob_ok and image_ok

    return {
        "mode": "same_path",
        "enforced": cfg["enforced"],
        "passed": passed,
        "hash_equal": hash_equal,
        "cells_differing": differ,
        "cells_total": total,
        "cell_agreement_pct": agreement_pct,
        "cells_within_tolerance": cells_within_tolerance,
        "run_bodies_present": run_cd["bodies_present"],
        "golden_bodies_present": golden["bodies_present"],
        "bodies_match": bodies_match,
        "oob_relative_delta": oob_relative_delta,
        "oob_ok": oob_ok,
        "run_oob": f"{run_cd['oob_count']}/{run_cd['oob_total']}",
        "golden_oob": (f"{golden['oob_count']}/{golden['oob_total']}"
                       if golden.get("oob_count") is not None else None),
        "image": image_result,
        "perf_a": run["perf"]["means_ms"],
    }


def compare_cross_path(run_a: dict, run_b: dict, thresholds: dict) -> dict:
    cfg = thresholds["cross_path"]
    cd_a, cd_b = run_a["cornell_diag"], run_b["cornell_diag"]
    if cd_a["grid_size"] != cd_b["grid_size"]:
        raise ParityError(
            f"grid size mismatch: run_a has {cd_a['grid_size']}, run_b has {cd_b['grid_size']}")

    agree, differ, total = cell_agreement(cd_a["rows"], cd_b["rows"])
    agreement_pct = 100.0 * agree / total if total else 0.0
    bodies_match = cd_a["bodies_present"] == cd_b["bodies_present"]

    image_result = compare_images(run_a["hud_png_path"], run_b["hud_png_path"],
                                   thresholds["pixel_diff_threshold"])

    perf_a = run_a["perf"]["means_ms"]
    perf_b = run_b["perf"]["means_ms"]
    perf_deltas = {}
    for key in perf_a:
        va, vb = perf_a.get(key), perf_b.get(key)
        if va is None or vb is None:
            perf_deltas[key] = None
        else:
            perf_deltas[key] = vb - va

    # cross_path is REPORT-ONLY (per parity_thresholds.json's "enforced" flag) --
    # `meets_target` is informational (what M5 will eventually gate on), and
    # never determines the tool's PASS/FAIL exit status while enforced=false.
    meets_target = (
        agreement_pct >= cfg["instidx_agreement_pct_target"]
        and (bodies_match if cfg["require_bodies_match"] else True)
        and image_result["mean_abs_luminance_delta"] <= cfg["luminance_mean_abs_delta_budget"]
        and image_result["p99_luminance_delta"] <= cfg["luminance_p99_delta_budget"]
        and image_result["pct_pixels_over_threshold"] <= cfg["pct_pixels_over_threshold_budget"]
    )

    return {
        "mode": "cross_path",
        "enforced": cfg["enforced"],
        "cell_agreement_pct": agreement_pct,
        "cells_differing": differ,
        "cells_total": total,
        "run_a_bodies_present": cd_a["bodies_present"],
        "run_b_bodies_present": cd_b["bodies_present"],
        "bodies_match": bodies_match,
        "run_a_oob": f"{cd_a['oob_count']}/{cd_a['oob_total']}",
        "run_b_oob": f"{cd_b['oob_count']}/{cd_b['oob_total']}",
        "image": image_result,
        "perf_a": perf_a,
        "perf_b": perf_b,
        "perf_delta_b_minus_a": perf_deltas,
        "meets_future_m5_target": meets_target,
    }


# ---------------------------------------------------------------------------
# CLI.
# ---------------------------------------------------------------------------

def build_summary_line(result: dict) -> tuple[str, str]:
    """Returns (status_word, one_line_summary) for the final stdout line."""
    if result["mode"] == "same_path":
        status = "PASS" if result["passed"] else "FAIL"
        summary = (
            f"same_path {status.lower()} -- "
            f"hash_equal={result['hash_equal']} "
            f"cells_differing={result['cells_differing']}/{result['cells_total']} "
            f"bodies_match={result['bodies_match']} "
            f"oob_ok={result['oob_ok']}"
        )
        if result.get("image"):
            summary += f" luminance_p99_delta={result['image']['p99_luminance_delta']:.2f}"
        return status, summary
    else:
        status = "REPORT"
        img = result["image"]
        summary = (
            f"cross_path (report-only, enforced={result['enforced']}) -- "
            f"cell_agreement={result['cell_agreement_pct']:.1f}% "
            f"bodies_match={result['bodies_match']} "
            f"luminance_mean_abs_delta={img['mean_abs_luminance_delta']:.2f} "
            f"luminance_p99_delta={img['p99_luminance_delta']:.2f} "
            f"pct_pixels_over_threshold={img['pct_pixels_over_threshold']:.1f}% "
            f"meets_future_m5_target={result['meets_future_m5_target']}"
        )
        return status, summary


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                      formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--mode", choices=["same_path", "cross_path"], required=True)
    parser.add_argument("--run", type=Path, help="same_path: the run dir to check")
    parser.add_argument("--golden", type=Path, help="same_path: a golden .txt (map+OOB)")
    parser.add_argument("--run-b", type=Path,
                        help="same_path: a second run dir instead of --golden; "
                             "cross_path: the second run dir (e.g. virtual)")
    parser.add_argument("--run-a", type=Path, help="cross_path: the first run dir (e.g. baked)")
    parser.add_argument("--thresholds", type=Path, default=DEFAULT_THRESHOLDS_PATH)
    parser.add_argument("--json-out", type=Path, help="optional path to write the full JSON report")
    parser.add_argument("--write-golden", type=Path,
                        help="same_path only: instead of comparing, write --run's instIdx map "
                             "to this path as a new golden .txt and exit 0")
    parser.add_argument("--golden-header", default="",
                        help="header comment line for --write-golden (e.g. 'HEAD abc123, 2026-07-16')")
    args = parser.parse_args()

    thresholds = json.loads(args.thresholds.read_text(encoding="utf-8"))

    try:
        if args.write_golden:
            if args.mode != "same_path" or not args.run:
                raise ParityError("--write-golden requires --mode same_path --run RUN_DIR")
            run = load_run_dir(args.run)
            write_golden_txt(run["cornell_diag"], args.write_golden,
                              args.golden_header or f"golden written from {args.run}")
            print(f"PARITY: PASS wrote golden {args.write_golden}")
            return 0

        if args.mode == "same_path":
            if not args.run:
                raise ParityError("--mode same_path requires --run")
            run = load_run_dir(args.run)
            image_ref = None
            if args.golden and args.run_b:
                raise ParityError("pass exactly one of --golden / --run-b for same_path")
            if args.golden:
                golden = load_golden_txt(args.golden)
            elif args.run_b:
                other = load_run_dir(args.run_b)
                golden = other["cornell_diag"]
                golden["oob_count"] = other["cornell_diag"]["oob_count"]
                golden["oob_total"] = other["cornell_diag"]["oob_total"]
                image_ref = other["hud_png_path"]
            else:
                raise ParityError("--mode same_path requires --golden or --run-b")
            result = compare_same_path(run, golden, thresholds, image_ref)
        else:
            if not args.run_a or not args.run_b:
                raise ParityError("--mode cross_path requires --run-a and --run-b")
            run_a = load_run_dir(args.run_a)
            run_b = load_run_dir(args.run_b)
            result = compare_cross_path(run_a, run_b, thresholds)
    except ParityError as e:
        print(f"PARITY: FAIL {e}", file=sys.stderr)
        return 1

    if args.json_out:
        args.json_out.write_text(json.dumps(result, indent=2, default=str), encoding="utf-8")

    status, summary = build_summary_line(result)
    print(f"PARITY: {status} {summary}")

    if result["mode"] == "same_path" and not result["passed"]:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
