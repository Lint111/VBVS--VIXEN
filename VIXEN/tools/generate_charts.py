#!/usr/bin/env python3
"""
Generate charts from Excel benchmark data.

Reads data from data/benchmarks.xlsx (normalized schema) and generates PNG charts
to Vixen-Docs/Assets/charts/ for embedding in Obsidian.

Schema expects:
- Benchmarks: Machine/GPU metadata with benchmark_id
- Summary: Test statistics with benchmark_id reference
- HW_RT_Frames, Compute_Frames, Fragment_Frames: Per-frame data by pipeline
- Cross_Machine: Multi-GPU comparison data

Usage:
    python generate_charts.py [--input PATH] [--charts CHART_NAMES...]
"""

import argparse
from pathlib import Path

import pandas as pd
import matplotlib.pyplot as plt
import numpy as np

from chart_config import (
    apply_style, save_chart, get_pipeline_color,
    EXCEL_FILE, CHARTS_OUTPUT_DIR, COLORS
)


def load_excel_data(excel_path: Path) -> dict[str, pd.DataFrame]:
    """Load all sheets from Excel workbook."""
    sheets = {}

    try:
        xl = pd.ExcelFile(excel_path)
        for sheet_name in xl.sheet_names:
            sheets[sheet_name] = pd.read_excel(xl, sheet_name=sheet_name)
            print(f"Loaded sheet: {sheet_name} ({len(sheets[sheet_name])} rows)")
    except FileNotFoundError:
        print(f"Error: Excel file not found: {excel_path}")
        print("Run aggregate_results.py first to create the Excel file.")

    return sheets


def get_summary_with_gpu(data: dict[str, pd.DataFrame]) -> pd.DataFrame:
    """Join Summary with Benchmarks to get GPU info."""
    if 'Summary' not in data or 'Benchmarks' not in data:
        return pd.DataFrame()

    summary = data['Summary'].copy()
    benchmarks = data['Benchmarks'][['benchmark_id', 'gpu_name', 'machine_name']].copy()

    if summary.empty or benchmarks.empty:
        return summary

    return summary.merge(benchmarks, on='benchmark_id', how='left')


def chart_fps_by_pipeline(data: dict[str, pd.DataFrame]) -> Path | None:
    """Create bar chart comparing FPS across pipelines."""
    if 'Summary' not in data:
        print("Skipping fps_by_pipeline: No Summary sheet")
        return None

    df = data['Summary']
    if df.empty or 'fps_mean' not in df.columns:
        return None

    # Group by pipeline
    comparison = df.groupby('pipeline').agg({
        'fps_mean': 'mean',
    }).round(2)

    if comparison.empty:
        return None

    fig, ax = plt.subplots(figsize=(10, 6))

    pipelines = comparison.index.tolist()
    fps_values = comparison['fps_mean'].tolist()
    colors = [get_pipeline_color(p) for p in pipelines]

    bars = ax.bar(pipelines, fps_values, color=colors, edgecolor='black', linewidth=0.5)

    for bar, val in zip(bars, fps_values):
        ax.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 1,
                f'{val:.1f}', ha='center', va='bottom', fontsize=10)

    ax.set_xlabel('Pipeline')
    ax.set_ylabel('Average FPS')
    ax.set_title('Performance Comparison: Average FPS by Pipeline')
    ax.set_ylim(0, max(fps_values) * 1.15)

    return save_chart(fig, 'fps_by_pipeline')


def chart_frame_time_by_pipeline(data: dict[str, pd.DataFrame]) -> Path | None:
    """Create bar chart comparing frame times across pipelines."""
    if 'Summary' not in data:
        print("Skipping frame_time_by_pipeline: No Summary sheet")
        return None

    df = data['Summary']
    if df.empty or 'frame_time_mean_ms' not in df.columns:
        return None

    comparison = df.groupby('pipeline').agg({
        'frame_time_mean_ms': 'mean',
    }).round(2)

    if comparison.empty:
        return None

    fig, ax = plt.subplots(figsize=(10, 6))

    pipelines = comparison.index.tolist()
    frame_times = comparison['frame_time_mean_ms'].tolist()
    colors = [get_pipeline_color(p) for p in pipelines]

    bars = ax.bar(pipelines, frame_times, color=colors, edgecolor='black', linewidth=0.5)

    for bar, val in zip(bars, frame_times):
        ax.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.2,
                f'{val:.2f}ms', ha='center', va='bottom', fontsize=10)

    ax.set_xlabel('Pipeline')
    ax.set_ylabel('Average Frame Time (ms)')
    ax.set_title('Performance Comparison: Average Frame Time by Pipeline')
    ax.set_ylim(0, max(frame_times) * 1.2)

    return save_chart(fig, 'frame_time_by_pipeline')


def chart_fps_by_resolution(data: dict[str, pd.DataFrame]) -> Path | None:
    """Create grouped bar chart of FPS across resolutions for each pipeline."""
    if 'Summary' not in data:
        print("Skipping fps_by_resolution: No Summary sheet")
        return None

    df = data['Summary']
    if df.empty or 'resolution' not in df.columns:
        return None

    pivot = df.pivot_table(
        values='fps_mean',
        index='resolution',
        columns='pipeline',
        aggfunc='mean'
    )

    if pivot.empty:
        return None

    fig, ax = plt.subplots(figsize=(12, 6))

    x = np.arange(len(pivot.index))
    width = 0.8 / len(pivot.columns)

    for i, pipeline in enumerate(pivot.columns):
        offset = (i - len(pivot.columns)/2 + 0.5) * width
        values = pivot[pipeline].fillna(0).tolist()
        color = get_pipeline_color(pipeline)
        ax.bar(x + offset, values, width, label=pipeline, color=color, edgecolor='black', linewidth=0.5)

    ax.set_xlabel('Resolution')
    ax.set_ylabel('Average FPS')
    ax.set_title('FPS by Resolution and Pipeline')
    ax.set_xticks(x)
    ax.set_xticklabels([str(int(r)) for r in pivot.index])
    ax.legend(loc='upper right')

    return save_chart(fig, 'fps_by_resolution')


def chart_fps_by_scene(data: dict[str, pd.DataFrame]) -> Path | None:
    """Create grouped bar chart of FPS across scenes for each pipeline."""
    if 'Summary' not in data:
        print("Skipping fps_by_scene: No Summary sheet")
        return None

    df = data['Summary']
    if df.empty or 'scene' not in df.columns:
        return None

    pivot = df.pivot_table(
        values='fps_mean',
        index='scene',
        columns='pipeline',
        aggfunc='mean'
    )

    if pivot.empty:
        return None

    fig, ax = plt.subplots(figsize=(12, 6))

    x = np.arange(len(pivot.index))
    width = 0.8 / len(pivot.columns)

    for i, pipeline in enumerate(pivot.columns):
        offset = (i - len(pivot.columns)/2 + 0.5) * width
        values = pivot[pipeline].fillna(0).tolist()
        color = get_pipeline_color(pipeline)
        ax.bar(x + offset, values, width, label=pipeline, color=color, edgecolor='black', linewidth=0.5)

    ax.set_xlabel('Scene')
    ax.set_ylabel('Average FPS')
    ax.set_title('FPS by Scene and Pipeline')
    ax.set_xticks(x)
    ax.set_xticklabels(pivot.index, rotation=45, ha='right')
    ax.legend(loc='upper right')

    return save_chart(fig, 'fps_by_scene')


def chart_frame_time_distribution(data: dict[str, pd.DataFrame]) -> Path | None:
    """Create box plot of frame time distributions."""
    if 'Summary' not in data:
        print("Skipping frame_time_distribution: No Summary sheet")
        return None

    df = data['Summary']
    if df.empty or 'frame_time_mean_ms' not in df.columns:
        return None

    pipelines = df['pipeline'].unique()

    fig, ax = plt.subplots(figsize=(10, 6))

    frame_times_by_pipeline = [
        df[df['pipeline'] == p]['frame_time_mean_ms'].dropna().tolist()
        for p in pipelines
    ]

    valid_data = [(p, ft) for p, ft in zip(pipelines, frame_times_by_pipeline) if ft]

    if not valid_data:
        plt.close(fig)
        return None

    pipelines_filtered, frame_times_filtered = zip(*valid_data)

    bp = ax.boxplot(frame_times_filtered, tick_labels=pipelines_filtered, patch_artist=True)

    for patch, pipeline in zip(bp['boxes'], pipelines_filtered):
        patch.set_facecolor(get_pipeline_color(pipeline))
        patch.set_alpha(0.7)

    ax.set_xlabel('Pipeline')
    ax.set_ylabel('Frame Time (ms)')
    ax.set_title('Frame Time Distribution by Pipeline')

    return save_chart(fig, 'frame_time_distribution')


def chart_bandwidth_comparison(data: dict[str, pd.DataFrame]) -> Path | None:
    """Create bar chart of memory bandwidth by pipeline."""
    if 'Summary' not in data:
        print("Skipping bandwidth_comparison: No Summary sheet")
        return None

    df = data['Summary']
    if df.empty or 'bandwidth_mean_gbps' not in df.columns:
        return None

    bw_by_pipeline = df.groupby('pipeline')['bandwidth_mean_gbps'].mean()

    if bw_by_pipeline.empty:
        return None

    fig, ax = plt.subplots(figsize=(10, 6))

    pipelines = bw_by_pipeline.index.tolist()
    bandwidth = bw_by_pipeline.values.tolist()
    colors = [get_pipeline_color(p) for p in pipelines]

    bars = ax.bar(pipelines, bandwidth, color=colors, edgecolor='black', linewidth=0.5)

    for bar, val in zip(bars, bandwidth):
        ax.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.5,
                f'{val:.1f}', ha='center', va='bottom', fontsize=10)

    ax.set_xlabel('Pipeline')
    ax.set_ylabel('Average Bandwidth (GB/s)')
    ax.set_title('Memory Bandwidth by Pipeline')

    return save_chart(fig, 'bandwidth_comparison')


def chart_resolution_heatmap(data: dict[str, pd.DataFrame]) -> Path | None:
    """Create heatmap of FPS across resolution and scene."""
    if 'Summary' not in data:
        print("Skipping resolution_heatmap: No Summary sheet")
        return None

    df = data['Summary']
    if df.empty:
        return None

    pivot = df.pivot_table(
        values='fps_mean',
        index='scene',
        columns='resolution',
        aggfunc='mean'
    )

    if pivot.empty or pivot.shape[0] < 2 or pivot.shape[1] < 2:
        print("Skipping resolution_heatmap: Not enough data for heatmap")
        return None

    fig, ax = plt.subplots(figsize=(10, 8))

    im = ax.imshow(pivot.values, cmap='RdYlGn', aspect='auto')

    cbar = ax.figure.colorbar(im, ax=ax)
    cbar.ax.set_ylabel('FPS', rotation=-90, va='bottom')

    ax.set_xticks(np.arange(len(pivot.columns)))
    ax.set_yticks(np.arange(len(pivot.index)))
    ax.set_xticklabels([str(int(c)) for c in pivot.columns])
    ax.set_yticklabels(pivot.index)

    for i in range(len(pivot.index)):
        for j in range(len(pivot.columns)):
            val = pivot.iloc[i, j]
            if not np.isnan(val):
                ax.text(j, i, f'{val:.0f}', ha='center', va='center', color='black', fontsize=9)

    ax.set_xlabel('Resolution')
    ax.set_ylabel('Scene')
    ax.set_title('FPS Heatmap: Resolution vs Scene')

    return save_chart(fig, 'resolution_heatmap')


def chart_cross_machine_comparison(data: dict[str, pd.DataFrame]) -> Path | None:
    """Create grouped bar chart comparing GPUs across pipelines."""
    if 'Cross_Machine' not in data:
        print("Skipping cross_machine: No Cross_Machine sheet")
        return None

    df = data['Cross_Machine']
    if df.empty or 'gpu_name' not in df.columns:
        return None

    # Pivot: GPU x Pipeline -> FPS
    pivot = df.pivot_table(
        values='avg_fps',
        index='gpu_name',
        columns='pipeline',
        aggfunc='mean'
    )

    if pivot.empty or len(pivot.index) < 1:
        print("Skipping cross_machine: Not enough GPUs for comparison")
        return None

    fig, ax = plt.subplots(figsize=(14, 6))

    x = np.arange(len(pivot.index))
    width = 0.8 / len(pivot.columns)

    for i, pipeline in enumerate(pivot.columns):
        offset = (i - len(pivot.columns)/2 + 0.5) * width
        values = pivot[pipeline].fillna(0).tolist()
        color = get_pipeline_color(pipeline)
        ax.bar(x + offset, values, width, label=pipeline, color=color, edgecolor='black', linewidth=0.5)

    ax.set_xlabel('GPU')
    ax.set_ylabel('Average FPS')
    ax.set_title('Cross-Machine GPU Performance Comparison')
    ax.set_xticks(x)
    ax.set_xticklabels(pivot.index, rotation=15, ha='right')
    ax.legend(loc='upper right')

    return save_chart(fig, 'cross_machine_comparison')


def chart_gpu_scaling(data: dict[str, pd.DataFrame]) -> Path | None:
    """Create line chart showing how each GPU scales across resolutions."""
    summary_with_gpu = get_summary_with_gpu(data)
    if summary_with_gpu.empty:
        print("Skipping gpu_scaling: Could not merge Summary with Benchmarks")
        return None

    if 'gpu_name' not in summary_with_gpu.columns:
        return None

    # Pivot: resolution x GPU -> FPS (averaged across all pipelines/scenes)
    pivot = summary_with_gpu.pivot_table(
        values='fps_mean',
        index='resolution',
        columns='gpu_name',
        aggfunc='mean'
    )

    if pivot.empty or len(pivot.columns) < 1:
        return None

    fig, ax = plt.subplots(figsize=(10, 6))

    for gpu in pivot.columns:
        values = pivot[gpu].dropna()
        ax.plot(values.index, values.values, marker='o', linewidth=2, markersize=8, label=gpu)

    ax.set_xlabel('Resolution')
    ax.set_ylabel('Average FPS')
    ax.set_title('GPU Scaling: FPS vs Resolution')
    ax.legend(loc='upper right')
    ax.grid(True, alpha=0.3)

    return save_chart(fig, 'gpu_scaling')


# Registry of all chart functions
CHART_GENERATORS = {
    'fps_by_pipeline': chart_fps_by_pipeline,
    'frame_time_by_pipeline': chart_frame_time_by_pipeline,
    'fps_by_resolution': chart_fps_by_resolution,
    'fps_by_scene': chart_fps_by_scene,
    'frame_time_distribution': chart_frame_time_distribution,
    'bandwidth_comparison': chart_bandwidth_comparison,
    'resolution_heatmap': chart_resolution_heatmap,
    'cross_machine_comparison': chart_cross_machine_comparison,
    'gpu_scaling': chart_gpu_scaling,
}


def generate_all_charts(data: dict[str, pd.DataFrame], charts: list[str] | None = None) -> list[Path]:
    """Generate all or specified charts."""
    apply_style()

    charts_to_generate = charts if charts else list(CHART_GENERATORS.keys())
    generated = []

    for chart_name in charts_to_generate:
        if chart_name not in CHART_GENERATORS:
            print(f"Unknown chart: {chart_name}")
            continue

        print(f"Generating: {chart_name}...")
        try:
            path = CHART_GENERATORS[chart_name](data)
            if path:
                generated.append(path)
        except Exception as e:
            print(f"Error generating {chart_name}: {e}")

    return generated


def main():
    parser = argparse.ArgumentParser(description="Generate charts from Excel benchmark data")
    parser.add_argument('--input', '-i', type=Path, default=EXCEL_FILE,
                        help=f"Input Excel file (default: {EXCEL_FILE})")
    parser.add_argument('--charts', '-c', nargs='*',
                        help=f"Specific charts to generate (default: all). Options: {list(CHART_GENERATORS.keys())}")
    parser.add_argument('--list', '-l', action='store_true',
                        help="List available chart types")

    args = parser.parse_args()

    if args.list:
        print("Available charts:")
        for name in CHART_GENERATORS.keys():
            print(f"  - {name}")
        return 0

    if not args.input.exists():
        print(f"Error: Excel file not found: {args.input}")
        print("Run aggregate_results.py first to create the Excel file.")
        return 1

    data = load_excel_data(args.input)
    if not data:
        return 1

    generated = generate_all_charts(data, args.charts)

    print(f"\nGenerated {len(generated)} charts in {CHARTS_OUTPUT_DIR}")
    return 0


if __name__ == "__main__":
    exit(main())
