# VIXEN Data Visualization Tools

Python scripts for aggregating benchmark data and generating charts for documentation.

## Quick Start

```bash
# 1. Create virtual environment (one-time)
cd tools
python -m venv .venv
.venv\Scripts\activate  # Windows
# source .venv/bin/activate  # Linux/Mac

# 2. Install dependencies
pip install -r requirements.txt

# 3. Run the full pipeline
python refresh_visualizations.py
```

## Pipeline Overview

```
benchmark_results/*.json  →  aggregate_results.py  →  data/benchmarks.xlsx
                                                              ↓
Vixen-Docs/Assets/charts/*.png  ←  generate_charts.py  ←─────┘
```

## Scripts

### refresh_visualizations.py

Master script that runs the full pipeline:

```bash
# Full refresh
python refresh_visualizations.py

# Skip aggregation (use existing Excel)
python refresh_visualizations.py --skip-aggregate

# Generate specific charts only
python refresh_visualizations.py --charts fps_by_pipeline fps_by_resolution

# Check prerequisites only
python refresh_visualizations.py --check
```

### aggregate_results.py

Aggregates JSON benchmark results into Excel workbook:

```bash
python aggregate_results.py
python aggregate_results.py --output custom_path.xlsx
python aggregate_results.py --input path/to/json/results
```

**Output sheets:**
- **Summary** - One row per benchmark run with key metrics
- **Comparison** - Pipeline comparison with aggregated statistics
- **Frame Data** - Per-frame metrics (first 5000 frames)
- **Metadata** - Generation timestamp and source info

### generate_charts.py

Generates PNG charts from Excel data:

```bash
python generate_charts.py
python generate_charts.py --input custom_data.xlsx
python generate_charts.py --charts fps_by_pipeline bandwidth_comparison
python generate_charts.py --list  # Show available charts
```

**Available charts:**
- `fps_by_pipeline` - Bar chart comparing FPS across pipelines
- `frame_time_by_pipeline` - Bar chart of frame times
- `fps_by_resolution` - Grouped bars: FPS by resolution and pipeline
- `fps_by_scene` - Grouped bars: FPS by scene and pipeline
- `frame_time_distribution` - Box plot of frame time distributions
- `bandwidth_comparison` - Memory bandwidth by pipeline
- `resolution_heatmap` - Heatmap of FPS across resolution/scene

## Directory Structure

```
VIXEN/
├── data/
│   └── benchmarks.xlsx        # Aggregated data (generated)
├── tools/
│   ├── requirements.txt       # Python dependencies
│   ├── chart_config.py        # Shared configuration
│   ├── aggregate_results.py   # JSON → Excel
│   ├── generate_charts.py     # Excel → PNG
│   └── refresh_visualizations.py  # Master script
├── benchmark_results/
│   └── *.json                 # Source benchmark data
└── Vixen-Docs/
    └── Assets/
        └── charts/            # Generated charts (PNG)
```

## Embedding in Obsidian

After generating charts, embed them in Obsidian markdown:

```markdown
## Performance Results

![[charts/fps_by_pipeline.png]]

### Detailed Analysis

![[charts/fps_by_resolution.png]]
```

## Configuration

Edit `chart_config.py` to customize:
- Color palette (COLORS dict)
- Chart styling (CHART_STYLE dict)
- Output paths

## Troubleshooting

### "No benchmark results found"
- Run benchmarks first: `./binaries/vixen_benchmark.exe`
- Check `benchmark_results/` contains `.json` files

### "Excel file not found"
- Run `python aggregate_results.py` first
- Or run full pipeline: `python refresh_visualizations.py`

### Charts not appearing in Obsidian
- Ensure Obsidian attachment folder is set to `Assets`
- Refresh Obsidian file cache (Ctrl+R)
- Check `Vixen-Docs/Assets/charts/` contains PNG files
