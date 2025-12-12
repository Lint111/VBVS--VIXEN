"""
Chart configuration and styling for VIXEN data visualization pipeline.
Defines consistent colors, fonts, and chart templates.
"""

import matplotlib.pyplot as plt
from pathlib import Path

# Project paths
PROJECT_ROOT = Path(__file__).parent.parent
DATA_DIR = PROJECT_ROOT / "data"
BENCHMARK_RESULTS_DIR = PROJECT_ROOT / "benchmark_results"
CHARTS_OUTPUT_DIR = PROJECT_ROOT / "Vixen-Docs" / "Assets" / "charts"
EXCEL_FILE = DATA_DIR / "benchmarks.xlsx"

# Ensure output directory exists
CHARTS_OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

# VIXEN color palette
COLORS = {
    'primary': '#4472C4',      # Blue - main data
    'secondary': '#ED7D31',    # Orange - comparison
    'accent': '#70AD47',       # Green - positive/success
    'warning': '#FFC000',      # Yellow - warnings
    'error': '#C00000',        # Red - errors/failures
    'neutral': '#7F7F7F',      # Gray - neutral/baseline

    # Pipeline-specific colors
    'hw_rt': '#4472C4',        # Hardware RT - Blue
    'sw_raymarch': '#ED7D31',  # Software Raymarch - Orange
    'compressed': '#70AD47',   # Compressed SVO - Green
    'fragment': '#9E480E',     # Fragment shader - Brown
    'compute': '#5B9BD5',      # Compute shader - Light blue
}

# Chart style configuration
CHART_STYLE = {
    'figure.figsize': (10, 6),
    'figure.dpi': 150,
    'figure.facecolor': 'white',
    'axes.facecolor': 'white',
    'axes.edgecolor': '#333333',
    'axes.labelcolor': '#333333',
    'axes.titlesize': 14,
    'axes.labelsize': 11,
    'axes.grid': True,
    'grid.alpha': 0.3,
    'grid.linestyle': '--',
    'xtick.labelsize': 10,
    'ytick.labelsize': 10,
    'legend.fontsize': 10,
    'legend.framealpha': 0.9,
    'font.family': 'sans-serif',
}


def apply_style():
    """Apply VIXEN chart style to matplotlib."""
    plt.rcParams.update(CHART_STYLE)


def get_pipeline_color(pipeline_name: str) -> str:
    """Get color for a specific pipeline type."""
    name_lower = pipeline_name.lower()
    if 'hw' in name_lower or 'hardware' in name_lower or 'rt' in name_lower:
        return COLORS['hw_rt']
    elif 'sw' in name_lower or 'software' in name_lower or 'march' in name_lower:
        return COLORS['sw_raymarch']
    elif 'compress' in name_lower:
        return COLORS['compressed']
    elif 'fragment' in name_lower:
        return COLORS['fragment']
    elif 'compute' in name_lower:
        return COLORS['compute']
    return COLORS['primary']


def save_chart(fig, name: str, tight: bool = True):
    """Save chart to the charts output directory."""
    if tight:
        fig.tight_layout()

    output_path = CHARTS_OUTPUT_DIR / f"{name}.png"
    fig.savefig(output_path, dpi=150, bbox_inches='tight', facecolor='white')
    plt.close(fig)
    print(f"Saved: {output_path}")
    return output_path
