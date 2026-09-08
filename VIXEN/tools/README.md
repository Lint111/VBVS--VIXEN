# VIXEN Tools

## Gates

### `check-no-new-mutex.sh` — no-new-mutex declaration gate

Lock-free federation phase 0 (`Vixen-Docs/01-Architecture/2026-09-08-lockfree-federation-architecture.md`
§5 phase 0/6, OD-11). Engine-side twin of the kernel's R-B enforcement gate: a declaration check that
fails when `VIXEN/libraries` or `VIXEN/application` gains a mutex that is not declared. A mutex is
declared when its file's owning declarations are pinned with their inventory family IDs in
`scripts/no-new-mutex.allowlist`, or when it is spelled through `Core/LockCensus.h`
(`Vixen::LockCensus::Mutex<Family::XX>`), which names its inventory row in the type.

```bash
VIXEN/scripts/check-no-new-mutex.sh          # PASS/FAIL (+ WARN for UNCLASSIFIED rows); exit 1 on FAIL
VIXEN/scripts/check-no-new-mutex.sh --fix    # re-sync the allowlist from the tree; new files land as UNCLASSIFIED
```

Wired into every build as the `no_new_mutex_check` target (`-DVIXEN_NO_NEW_MUTEX_GATE=OFF` to opt out;
skipped automatically where bash/python3 are absent). The allowlist diff is the declaration to review:
a lock removal re-syncs its row downward, an addition must carry a family (or be classified — an
`UNCLASSIFIED` row warns until the inventory has a row for it).

### Lock census (`-DVIXEN_LOCK_CENSUS=ON`)

The measurement half of phase 0 (design §9). `Core/LockCensus.h` swaps the frame-path mutex
declarations (VK1/VK2, EB1-3, RM1/RM7-12, C1/C3/C4, RG2-4) for counting wrappers; `RenderGraph`
samples them once per frame together with the executed wave shape (waves, rows, max wave width,
whole-slot rows, worker count, frame CPU ms). Off by default — a pure type alias, no code.

```bash
cmake --preset vixen-wsl -B build/wsl-census -DVIXEN_LOCK_CENSUS=ON
VIXEN_LOCK_CENSUS_CSV=/tmp/census.csv VIXEN_EXIT_AFTER_FRAMES=300 ./build/wsl-census/binaries/VIXEN   # one CSV row per frame
# summary (mean per frame per family) is printed as "[LockCensus] ..." lines at graph teardown
# 1/2/N sweep (sequential + lowered w=1,2,4): VIXEN/scripts/lock-census-sweep.sh build/wsl-census/binaries/VIXEN out/ 300
# byte-identity across worker counts is checked with the existing capture/golden tools, not by the sweep
```

## Data visualization

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
