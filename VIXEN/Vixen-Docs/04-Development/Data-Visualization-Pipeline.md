---
tags: [development, tooling, visualization, pipeline]
created: 2025-12-10
status: active
---

# Data Visualization Pipeline

Automated pipeline for aggregating benchmark data, generating charts, and embedding visualizations in documentation.

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│                    DATA VISUALIZATION PIPELINE                       │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  benchmark_results/*.json                                            │
│           │                                                          │
│           ▼                                                          │
│  ┌─────────────────────┐                                            │
│  │ aggregate_results.py │  JSON → Excel aggregation                 │
│  └─────────────────────┘                                            │
│           │                                                          │
│           ▼                                                          │
│  data/benchmarks.xlsx  ◄──── Excel MCP (manual edits)               │
│           │                                                          │
│           ▼                                                          │
│  ┌─────────────────────┐                                            │
│  │ generate_charts.py  │  Excel → PNG charts                        │
│  └─────────────────────┘                                            │
│           │                                                          │
│           ▼                                                          │
│  Vixen-Docs/Assets/charts/*.png                                     │
│           │                                                          │
│           ▼                                                          │
│  Obsidian markdown: ![[charts/filename.png]]                        │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

## Quick Start

```bash
# One-time setup
cd tools
python -m venv .venv
.venv\Scripts\activate  # Windows
pip install -r requirements.txt

# Run full pipeline
python refresh_visualizations.py
```

## Directory Structure

```
VIXEN/
├── data/                          # Excel data files
│   ├── benchmarks.xlsx            # Aggregated benchmark data
│   ├── benchmarks/                # Multi-tester benchmark storage
│   │   ├── benchmark_001/         # Individual benchmark folders
│   │   ├── benchmark_002/
│   │   └── benchmark_001.zip      # Compressed archives for transport
│   └── templates/                 # Excel templates (future)
│
├── tools/                         # Python scripts
│   ├── .venv/                     # Virtual environment (gitignored)
│   ├── requirements.txt           # Dependencies
│   ├── chart_config.py            # Shared configuration
│   ├── aggregate_results.py       # JSON → Excel
│   ├── generate_charts.py         # Excel → PNG
│   ├── refresh_visualizations.py  # Master script
│   └── README.md                  # Tool documentation
│
├── benchmark_results/             # Source JSON data (local runs)
│   ├── *.json                     # Benchmark result files
│   └── debug_images/              # Frame captures
│
└── Vixen-Docs/
    ├── Assets/
    │   └── charts/                # Generated PNG charts
    └── Analysis/
        └── Benchmark-Results.md   # Report embedding charts
```

## Pipeline Components

### 1. aggregate_results.py

Aggregates JSON benchmark results into an Excel workbook.

**Input:** `benchmark_results/*.json`
**Output:** `data/benchmarks.xlsx`

**Excel Sheets (Normalized Schema):**
| Sheet | Content |
|-------|---------|
| Benchmarks | One row per benchmark run (machine/GPU metadata) |
| Summary | Test statistics linked by benchmark_id |
| HW_RT_Frames | Per-frame data for hardware RT pipeline |
| Compute_Frames | Per-frame data for compute pipeline |
| Fragment_Frames | Per-frame data for fragment pipeline |
| Cross_Machine | Multi-GPU comparison view |

**Usage:**
```bash
python aggregate_results.py                      # Aggregate and append
python aggregate_results.py --cleanup            # Aggregate then delete JSON/images
python aggregate_results.py --machine-name "PC1" # Override machine name
python aggregate_results.py --output custom.xlsx
```

**Multi-Tester Workflow:**
```bash
# Pack benchmark for sharing with other testers
python aggregate_results.py --pack benchmark_results/

# Receive and unpack benchmark archive
python aggregate_results.py --unpack benchmark_001.zip

# List available benchmark folders
python aggregate_results.py --list

# Process all benchmark folders at once
python aggregate_results.py --process-all

# Process all and cleanup source files
python aggregate_results.py --process-all --cleanup
```

**Machine Name Resolution:**
1. `--machine-name` CLI argument (highest priority)
2. `VIXEN_MACHINE_NAME` environment variable
3. `machine_name` in `benchmark_config.json`
4. System hostname (fallback)

### 2. generate_charts.py

Generates PNG charts from Excel data using matplotlib.

**Input:** `data/benchmarks.xlsx`
**Output:** `Vixen-Docs/Assets/charts/*.png`

**Available Charts:**
| Chart | Type | Description |
|-------|------|-------------|
| `fps_by_pipeline` | Bar | FPS comparison across pipelines |
| `frame_time_by_pipeline` | Bar | Frame time comparison |
| `fps_by_resolution` | Grouped Bar | FPS by resolution and pipeline |
| `fps_by_scene` | Grouped Bar | FPS by scene and pipeline |
| `frame_time_distribution` | Box Plot | Frame time variance |
| `bandwidth_comparison` | Bar | Memory bandwidth by pipeline |
| `resolution_heatmap` | Heatmap | FPS across resolution/scene grid |
| `cross_machine_comparison` | Grouped Bar | GPU comparison across pipelines |
| `gpu_scaling` | Line | FPS vs resolution per GPU |

**Usage:**
```bash
python generate_charts.py
python generate_charts.py --charts fps_by_pipeline fps_by_resolution
python generate_charts.py --list  # Show available charts
```

### 3. refresh_visualizations.py

Master script that orchestrates the full pipeline.

**Usage:**
```bash
python refresh_visualizations.py                    # Full refresh
python refresh_visualizations.py --skip-aggregate   # Charts only
python refresh_visualizations.py --charts fps_by_pipeline  # Specific charts
python refresh_visualizations.py --check            # Verify prerequisites
```

### 4. chart_config.py

Shared configuration for colors, styles, and paths.

**Key Settings:**
- `COLORS` - VIXEN color palette for consistent styling
- `CHART_STYLE` - Matplotlib rcParams for chart appearance
- `PROJECT_ROOT`, `DATA_DIR`, `CHARTS_OUTPUT_DIR` - Path constants

## Embedding in Obsidian

### Basic Embedding

```markdown
## Performance Results

![[charts/fps_by_pipeline.png]]
```

### With Sizing

```markdown
![[charts/fps_by_pipeline.png|600]]
```

### Inline Reference

```markdown
The FPS comparison (![[charts/fps_by_pipeline.png|200]]) shows...
```

## Excel as Single Source of Truth

The `data/benchmarks.xlsx` file serves as the central data store:

1. **Automated updates**: `aggregate_results.py` populates from JSON
2. **Manual edits**: Use Excel or Excel MCP to add annotations, filter data
3. **Formula support**: Add calculated columns in Excel
4. **Chart regeneration**: Charts update from Excel on each refresh

### Excel MCP Integration

The data-scientist agent can manipulate Excel directly:

```
mcp__excel__excel_read_sheet    - Read data
mcp__excel__excel_write_to_sheet - Add/modify data
mcp__excel__excel_format_range   - Apply styling
```

## Adding New Charts

1. Add chart function to `generate_charts.py`:

```python
def chart_new_visualization(data: dict[str, pd.DataFrame]) -> Path | None:
    """Create your new chart."""
    # Implementation
    return save_chart(fig, 'new_visualization')
```

2. Register in `CHART_GENERATORS`:

```python
CHART_GENERATORS = {
    # ... existing charts
    'new_visualization': chart_new_visualization,
}
```

3. Run `python generate_charts.py --charts new_visualization`

## Customizing Styles

Edit `chart_config.py`:

```python
COLORS = {
    'primary': '#4472C4',      # Change primary color
    'hw_rt': '#4472C4',        # Pipeline-specific
}

CHART_STYLE = {
    'figure.figsize': (10, 6), # Default size
    'figure.dpi': 150,         # Resolution
}
```

## Troubleshooting

### "No benchmark results found"

```bash
# Run benchmarks first
./binaries/vixen_benchmark.exe

# Check results exist
ls benchmark_results/*.json
```

### "Excel file not found"

```bash
# Generate Excel from JSON
python aggregate_results.py
```

### Charts not appearing in Obsidian

1. Check `Vixen-Docs/Assets/charts/` contains PNG files
2. Verify Obsidian attachment path is set to `Assets`
3. Refresh Obsidian file cache (Ctrl+R)

### Python dependencies missing

```bash
cd tools
.venv\Scripts\activate
pip install -r requirements.txt
```

## Dependencies

**Python packages:**
- `matplotlib>=3.8.0` - Chart generation
- `openpyxl>=3.1.0` - Excel read/write
- `pandas>=2.1.0` - Data manipulation
- `numpy>=1.26.0` - Numerical operations

## Related Documentation

- [[../Analysis/Benchmark-Results]] - Example analysis document
- [[Profiling]] - Benchmark system overview
- [[../05-Progress/features/data-visualization-pipeline]] - Feature tracking

---

*Pipeline created: 2025-12-10*
*Part of VIXEN data-scientist toolchain*
