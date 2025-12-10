#!/usr/bin/env python3
"""
Master refresh script for the data visualization pipeline.

Orchestrates the full pipeline:
1. Aggregate JSON benchmark results -> Excel
2. Generate charts from Excel -> PNG
3. Report what was updated

Usage:
    python refresh_visualizations.py [--skip-aggregate] [--charts CHART_NAMES...]
"""

import argparse
import subprocess
import sys
from pathlib import Path
from datetime import datetime

from chart_config import (
    PROJECT_ROOT, DATA_DIR, BENCHMARK_RESULTS_DIR,
    CHARTS_OUTPUT_DIR, EXCEL_FILE
)


def run_script(script_name: str, args: list[str] = None) -> bool:
    """Run a Python script and return success status."""
    script_path = Path(__file__).parent / script_name
    cmd = [sys.executable, str(script_path)]
    if args:
        cmd.extend(args)

    print(f"\n{'='*60}")
    print(f"Running: {' '.join(cmd)}")
    print('='*60)

    result = subprocess.run(cmd, cwd=PROJECT_ROOT)
    return result.returncode == 0


def check_prerequisites() -> list[str]:
    """Check for required files and directories."""
    issues = []

    if not BENCHMARK_RESULTS_DIR.exists():
        issues.append(f"Benchmark results directory not found: {BENCHMARK_RESULTS_DIR}")

    json_files = list(BENCHMARK_RESULTS_DIR.glob("*.json")) if BENCHMARK_RESULTS_DIR.exists() else []
    if not json_files:
        issues.append(f"No JSON benchmark files found in {BENCHMARK_RESULTS_DIR}")

    return issues


def print_summary(aggregate_success: bool, charts_generated: int):
    """Print pipeline execution summary."""
    print("\n" + "="*60)
    print("PIPELINE SUMMARY")
    print("="*60)

    print(f"Timestamp: {datetime.now().isoformat()}")
    print(f"Excel file: {EXCEL_FILE}")
    print(f"Charts directory: {CHARTS_OUTPUT_DIR}")
    print()

    if aggregate_success:
        print("[OK] Data aggregation: SUCCESS")
    else:
        print("[FAIL] Data aggregation: FAILED")

    print(f"[OK] Charts generated: {charts_generated}")

    # List generated charts
    if CHARTS_OUTPUT_DIR.exists():
        charts = list(CHARTS_OUTPUT_DIR.glob("*.png"))
        if charts:
            print("\nGenerated charts:")
            for chart in sorted(charts):
                print(f"  - {chart.name}")

    print("\n" + "="*60)
    print("To embed in Obsidian, use:")
    print("  ![[charts/fps_by_pipeline.png]]")
    print("="*60)


def main():
    parser = argparse.ArgumentParser(description="Refresh all data visualizations")
    parser.add_argument('--skip-aggregate', action='store_true',
                        help="Skip JSON->Excel aggregation (use existing Excel)")
    parser.add_argument('--charts', '-c', nargs='*',
                        help="Specific charts to generate (default: all)")
    parser.add_argument('--check', action='store_true',
                        help="Check prerequisites only, don't run pipeline")

    args = parser.parse_args()

    print("="*60)
    print("VIXEN Data Visualization Pipeline")
    print("="*60)

    # Check prerequisites
    issues = check_prerequisites()
    if issues:
        print("\nPrerequisite issues:")
        for issue in issues:
            print(f"  [!] {issue}")

        if args.check:
            return 1 if issues else 0

        if not args.skip_aggregate:
            print("\nCannot proceed without benchmark data.")
            print("Run benchmarks first, or use --skip-aggregate with existing Excel file.")
            return 1

    if args.check:
        print("\n[OK] All prerequisites met")
        return 0

    aggregate_success = True
    charts_generated = 0

    # Step 1: Aggregate results
    if not args.skip_aggregate:
        aggregate_success = run_script("aggregate_results.py")
        if not aggregate_success:
            print("\nWarning: Aggregation failed. Attempting chart generation anyway...")

    # Step 2: Generate charts
    if EXCEL_FILE.exists():
        chart_args = []
        if args.charts:
            chart_args = ['--charts'] + args.charts

        if run_script("generate_charts.py", chart_args):
            # Count generated charts
            charts_generated = len(list(CHARTS_OUTPUT_DIR.glob("*.png")))
    else:
        print(f"\nError: Excel file not found: {EXCEL_FILE}")
        print("Run without --skip-aggregate to create it.")
        return 1

    # Summary
    print_summary(aggregate_success, charts_generated)

    return 0 if aggregate_success and charts_generated > 0 else 1


if __name__ == "__main__":
    exit(main())
