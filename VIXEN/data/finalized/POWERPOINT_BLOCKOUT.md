# VIXEN Research Presentation - PowerPoint Blockout

**Presentation Title**: Performance Comparison of Vulkan Rendering Pipelines for Voxel Data
**Duration**: 15-20 minutes
**Slides**: 24 recommended
**Last Updated**: December 29, 2025 (synced with Cycle 4 paper improvements)
**Data Version**: V3 (981 tests, 5 GPUs)

---

## Visual Theme Recommendations

| Element | Specification |
|---------|---------------|
| Primary Color | #4472C4 (Vulkan Blue) |
| Secondary Color | #ED7D31 (Orange Accent) |
| Background | White or very light gray |
| Font - Headings | Segoe UI Semibold, 32-44pt |
| Font - Body | Segoe UI, 18-24pt |
| Chart Style | Match data/finalized/charts/ style |

---

## Slide-by-Slide Content

### SLIDE 1: Title Slide
**Layout**: Title centered, author bottom

```
┌────────────────────────────────────────────┐
│                                            │
│   PERFORMANCE COMPARISON OF VULKAN         │
│   RENDERING PIPELINES FOR VOXEL DATA       │
│                                            │
│        [Voxel render image as BG]          │
│                                            │
│         [Author Name]                      │
│         [Institution]                      │
│         December 2025                      │
└────────────────────────────────────────────┘
```

**Notes**: Use cityscape_128_hwrt.png as subtle background (50% opacity)

---

### SLIDE 2: Research Question
**Layout**: Quote box + 3 icons

```
┌────────────────────────────────────────────┐
│  RESEARCH QUESTION                         │
│                                            │
│  ┌────────────────────────────────────┐    │
│  │ "How do different Vulkan ray       │    │
│  │  tracing and ray marching pipeline │    │
│  │  architectures affect rendering    │    │
│  │  performance for voxel data?"      │    │
│  └────────────────────────────────────┘    │
│                                            │
│   [Compute]    [Fragment]    [HW RT]       │
│      🖥️           📊           ⚡          │
└────────────────────────────────────────────┘
```

**Notes**: Use simple icons for each pipeline type

---

### SLIDE 3: Why This Matters
**Layout**: 3-column with icons

```
┌────────────────────────────────────────────┐
│  WHY THIS MATTERS                          │
│                                            │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐   │
│  │  VOXEL   │ │   NO     │ │ VULKAN   │   │
│  │  GAMES   │ │EXISTING  │ │  1.3+    │   │
│  │ GROWING  │ │COMPARISON│ │ RT CORES │   │
│  │          │ │          │ │          │   │
│  │ Minecraft│ │ Gap in   │ │ New HW   │   │
│  │ Teardown │ │ research │ │ features │   │
│  │ etc.     │ │          │ │          │   │
│  └──────────┘ └──────────┘ └──────────┘   │
└────────────────────────────────────────────┘
```

---

### SLIDE 4: Voxel Rendering Approaches
**Layout**: Comparison diagram

```
┌────────────────────────────────────────────┐
│  VOXEL RENDERING APPROACHES                │
│                                            │
│       RASTERIZATION          RAY-BASED    │
│      ┌─────────────┐      ┌─────────────┐ │
│      │   Convert   │      │   Direct    │ │
│      │   to mesh   │      │  traversal  │ │
│      │             │  VS  │             │ │
│      │  Triangles  │      │    Rays     │ │
│      └─────────────┘      └─────────────┘ │
│                                            │
│  "Ray-casting wins for high-resolution     │
│   and sparse voxel datasets" [1]          │
└────────────────────────────────────────────┘
```

**Notes**: Cite Nousiainen [1]

---

### SLIDE 5: Three Pipeline Types
**Layout**: Pipeline architecture diagram

```
┌────────────────────────────────────────────┐
│  VULKAN PIPELINE OPTIONS                   │
│                                            │
│  ┌─────────────────────────────────────┐  │
│  │         COMPUTE SHADER              │  │
│  │  Ray March → Storage Image → Screen │  │
│  └─────────────────────────────────────┘  │
│                                            │
│  ┌─────────────────────────────────────┐  │
│  │         FRAGMENT SHADER             │  │
│  │  Full-screen Quad → Ray March       │  │
│  └─────────────────────────────────────┘  │
│                                            │
│  ┌─────────────────────────────────────┐  │
│  │       HARDWARE RAY TRACING          │  │
│  │  VK_KHR_ray_tracing + BVH/AABB     │  │
│  └─────────────────────────────────────┘  │
└────────────────────────────────────────────┘
```

---

### SLIDE 6: Hypotheses
**Layout**: Numbered list with predicted outcomes

```
┌────────────────────────────────────────────┐
│  HYPOTHESES                                │
│                                            │
│  H1: HW RT superior at high resolutions    │
│      → Prediction: Best at ≥256³           │
│                                            │
│  H2: Compute most consistent across scenes │
│      → Prediction: Lowest CV across density│
│                                            │
│  H3: Fragment highest bandwidth usage      │
│      → Prediction: Most memory traffic     │
│                                            │
│  H4: Hybrid best trade-off (scope cut)     │
│                                            │
│  H5: Compression yields ≥30% improvement   │
│      → Prediction: Block encoding helps    │
└────────────────────────────────────────────┘
```

**Notes**: H4 was cut from scope - acknowledge in limitations

---

### SLIDE 7: Test Framework
**Layout**: Screenshot + specs

```
┌────────────────────────────────────────────┐
│  BENCHMARK METHODOLOGY                     │
│                                            │
│  ┌──────────────────┐  METRICS COLLECTED  │
│  │                  │  • FPS (mean, std)  │
│  │   [Screenshot    │  • Frame time (ms)  │
│  │    of VIXEN      │  • Bandwidth (GB/s) │
│  │    running]      │  • VRAM usage       │
│  │                  │                      │
│  └──────────────────┘  FRAMES/TEST: 300+  │
│                                            │
│  Automated • Reproducible • Per-frame data │
└────────────────────────────────────────────┘
```

---

### SLIDE 8: Test Matrix
**Layout**: Table with coverage

```
┌────────────────────────────────────────────┐
│  TEST MATRIX                               │
│                                            │
│  PIPELINES      RESOLUTIONS    SCENES     │
│  ✓ Compute      ✓ 64³          ✓ Cornell  │
│  ✓ Fragment     ✓ 128³         ✓ Cityscape│
│  ✓ HW RT        ✓ 256³         ✓ Noise    │
│  ✗ Hybrid       ✗ 32³          ✓ Tunnels  │
│                 ✗ 512³                     │
│                                            │
│  TOTAL: 981 tests • 5 GPU platforms       │
│                                            │
│  ⚠️ Single-run measurements (exploratory) │
└────────────────────────────────────────────┘
```

---

### SLIDE 9: Hardware Platform
**Layout**: GPU table

```
┌────────────────────────────────────────────┐
│  HARDWARE TESTED (5 GPUs)                  │
│                                            │
│  ┌─────────────────────────────────────┐  │
│  │ GPU                    │ VRAM │ HW RT │  │
│  ├─────────────────────────────────────│  │
│  │ RTX 4080 Laptop        │ 16GB │  ✓   │  │
│  │ RTX 3080               │ 10GB │  ✓   │  │
│  │ RTX 3060 Laptop        │ 12GB │  ✓   │  │
│  │ AMD Radeon (iGPU)      │ 2GB  │  ✓   │  │
│  │ Intel RaptorLake-S     │ 4GB  │  ✗   │  │
│  └─────────────────────────────────────┘  │
│                                            │
│  Vulkan 1.3 • VK_KHR_ray_tracing_pipeline │
└────────────────────────────────────────────┘
```

**Notes**: Intel iGPU lacks HW RT support - Compute/Fragment only (96 tests)

---

### SLIDE 10: Pipeline Performance (KEY RESULT)
**Layout**: Chart + key finding + caveat

```
┌────────────────────────────────────────────┐
│  PIPELINE PERFORMANCE                      │
│                                            │
│  ┌──────────────────────────────────────┐ │
│  │                                      │ │
│  │        [fps_by_pipeline.png]         │ │
│  │                                      │ │
│  └──────────────────────────────────────┘ │
│                                            │
│  ┌──────────────────────────────────────┐ │
│  │ Pipeline │ Mean FPS │ Median │ P99 FT │ │
│  │ Compute  │   514    │  215   │ 14.6ms │ │
│  │ Fragment │   991    │  783   │  9.3ms │ │
│  │ HW RT    │  1766    │ 1745   │  5.0ms │ │
│  └──────────────────────────────────────┘ │
│                                            │
│  KEY FINDING: Hardware RT is 2.5-3.4x     │
│  faster (conservative, accounting for     │
│  compute instrumentation overhead)        │
└────────────────────────────────────────────┘
```

**Notes**:
- Hero slide - pause for impact
- **CRITICAL**: Note median vs mean for Compute (215 vs 514) - mean inflated by RTX 4080 outlier
- Instrumentation adds 20-40% overhead to compute only

---

### SLIDE 11: Resolution Scaling
**Layout**: Chart + observation

```
┌────────────────────────────────────────────┐
│  RESOLUTION SCALING                        │
│                                            │
│  ┌──────────────────────────────────────┐ │
│  │                                      │ │
│  │       [fps_by_resolution.png]        │ │
│  │                                      │ │
│  └──────────────────────────────────────┘ │
│                                            │
│  OBSERVATION: HW RT maintains performance │
│  better as resolution increases           │
│  (256³ only -10% vs 64³)                  │
└────────────────────────────────────────────┘
```

---

### SLIDE 12: GPU Comparison
**Layout**: Chart + rankings

```
┌────────────────────────────────────────────┐
│  GPU COMPARISON                            │
│                                            │
│  ┌──────────────────────────────────────┐ │
│  │                                      │ │
│  │    [cross_machine_comparison.png]    │ │
│  │                                      │ │
│  └──────────────────────────────────────┘ │
│                                            │
│  RANKINGS:                                 │
│  • HW RT Leader: RTX 3080 (2994 FPS)      │
│  • Compute Leader: RTX 4080 (1373 FPS)    │
│  • iGPU: Surprisingly competitive         │
└────────────────────────────────────────────┘
```

---

### SLIDE 13: Scene Performance
**Layout**: Chart + scene images

```
┌────────────────────────────────────────────┐
│  SCENE PERFORMANCE                         │
│                                            │
│  ┌──────────────────────────────────────┐ │
│  │        [fps_by_scene.png]            │ │
│  └──────────────────────────────────────┘ │
│                                            │
│  [cornell]  [cityscape]  [noise] [tunnels]│
│   (small)    (small)     (small)  (small) │
│                                            │
│  FASTEST: Tunnels scene across all pipes  │
└────────────────────────────────────────────┘
```

**Notes**: Include 4 small scene thumbnails

---

### SLIDE 13b: Scene Characterization (NEW)
**Layout**: Table showing density levels

```
┌────────────────────────────────────────────┐
│  SCENE CHARACTERIZATION                    │
│                                            │
│  ┌─────────────────────────────────────┐  │
│  │ Scene     │ Density  │ Fill Rate   │  │
│  ├─────────────────────────────────────│  │
│  │ Cornell   │ Sparse   │ ~5-15%      │  │
│  │ Cityscape │ Medium   │ ~25-40%     │  │
│  │ Noise     │ Uniform  │ ~50%        │  │
│  │ Tunnels   │ Dense    │ ~85-95%     │  │
│  └─────────────────────────────────────┘  │
│                                            │
│  Density serves as implicit H2 test       │
│  (Cornell sparse → Tunnels dense range)   │
└────────────────────────────────────────────┘
```

**Notes**: This explains why scene performance varies and validates density testing via scene proxy

---

### SLIDE 14: Bandwidth Analysis
**Layout**: Chart + H3 discussion

```
┌────────────────────────────────────────────┐
│  BANDWIDTH ANALYSIS                        │
│                                            │
│  ┌──────────────────────────────────────┐ │
│  │      [bandwidth_comparison.png]      │ │
│  └──────────────────────────────────────┘ │
│                                            │
│  ⚠️ UNEXPECTED RESULT                     │
│                                            │
│  PREDICTED (H3): Fragment highest         │
│  ACTUAL: HW RT highest (203 GB/s)         │
│                                            │
│  → RT cores require more memory traffic   │
└────────────────────────────────────────────┘
```

**Notes**: This contradicts H3 - discuss honestly

---

### SLIDE 14b: Compression Effects (NEW)
**Layout**: Two charts side-by-side + key finding

```
┌────────────────────────────────────────────┐
│  COMPRESSION EFFECTS ON PERFORMANCE        │
│                                            │
│  ┌─────────────────┐ ┌─────────────────┐  │
│  │ [compression_   │ │ [compression_   │  │
│  │  fps_by_        │ │  bandwidth_by_  │  │
│  │  pipeline.png]  │ │  pipeline.png]  │  │
│  └─────────────────┘ └─────────────────┘  │
│                                            │
│  ┌──────────────────────────────────────┐ │
│  │ Pipeline  │ FPS Δ    │ Bandwidth Δ  │ │
│  ├──────────────────────────────────────│ │
│  │ Compute   │ +10.1%   │ +7.9% (MORE) │ │
│  │ Fragment  │ +1.8%    │ +2.4% (MORE) │ │
│  │ HW RT     │ -1.3%    │ -0.4% (same) │ │
│  └──────────────────────────────────────┘ │
│                                            │
│  H5 CONTRADICTED: Compression improves    │
│  FPS but INCREASES bandwidth overhead     │
└────────────────────────────────────────────┘
```

**Charts**: compression_fps_by_pipeline.png, compression_bandwidth_by_pipeline.png
**Notes**: N=261 compute, 257 fragment, 223 HW RT paired tests

---

### SLIDE 15: Hypothesis Evaluation
**Layout**: Summary table

```
┌────────────────────────────────────────────┐
│  HYPOTHESIS EVALUATION                     │
│                                            │
│  ┌─────────────────────────────────────┐  │
│  │ H1: HW RT superior    │ ~ PARTIAL   │  │
│  │     (Need 512³ test to fully confirm) │  │
│  │ H2: Compute consistent│ ✗ CONTRAD.  │  │
│  │     (HW RT most consistent: CV=4%)   │  │
│  │ H3: Fragment highest  │ ✗ CONTRAD.  │  │
│  │     (HW RT highest: 203 GB/s)        │  │
│  │ H4: Hybrid best       │ - NOT IMPL  │  │
│  │ H5: 30% BW reduction  │ ✗ CONTRAD.  │  │
│  │     (BW +8%, FPS +10% compute only)  │  │
│  └─────────────────────────────────────┘  │
│                                            │
│  KEY INSIGHT: 3 of 5 hypotheses contradicted │
│  (H2, H3, H5) - significant scientific value │
└────────────────────────────────────────────┘
```

---

### SLIDE 16: Limitations
**Layout**: Honest assessment - builds credibility

```
┌────────────────────────────────────────────┐
│  LIMITATIONS                               │
│                                            │
│  STATISTICAL:                              │
│  • Single-run measurements (no replication)│
│  • High compute variance (CV=112.6%)      │
│  • Mean ≠ Median for compute (139% diff)  │
│  • Unbalanced GPU representation          │
│                                            │
│  METHODOLOGY:                              │
│  • Compute includes instrumentation (~30%) │
│  • No CPU overhead profiling              │
│  • Windows-only testing                   │
│                                            │
│  SCOPE:                                    │
│  • Missing 32³/512³ resolutions           │
│  • Hybrid pipeline not implemented        │
│  • No power/thermal measurements          │
│                                            │
│  → EXPLORATORY findings, require validation│
└────────────────────────────────────────────┘
```

**Notes**: Being upfront about limitations builds credibility - mention this is exploratory study

---

### SLIDE 17: Practical Implications
**Layout**: Decision flowchart

```
┌────────────────────────────────────────────┐
│  WHEN TO USE EACH PIPELINE                 │
│                                            │
│           ┌─────────────────┐              │
│           │ RT Cores avail? │              │
│           └────────┬────────┘              │
│              YES   │   NO                  │
│           ┌────────┴────────┐              │
│           ▼                 ▼              │
│    ┌────────────┐    ┌────────────┐       │
│    │ HARDWARE RT│    │ FRAGMENT   │       │
│    │ (fastest)  │    │ (fallback) │       │
│    └────────────┘    └────────────┘       │
│                                            │
│  COMPUTE: Use when explicit memory control │
│           is more important than raw speed │
└────────────────────────────────────────────┘
```

---

### SLIDE 18: Key Findings
**Layout**: 4 bullet points, scene background

```
┌────────────────────────────────────────────┐
│  KEY FINDINGS                              │
│                                            │
│  [Scene image as background - 30% opacity] │
│                                            │
│  1. Hardware RT achieves 2.5-3.4x speedup │
│     (conservative estimate)               │
│                                            │
│  2. HW RT most CONSISTENT across density  │
│     (CV=3.8% vs Compute CV=13.7%)         │
│     → Contradicts H2 prediction           │
│                                            │
│  3. Compute shows HIGH VARIANCE           │
│     (median 215 FPS vs mean 514 FPS)      │
│     → Mean inflated by GPU outliers       │
│                                            │
│  4. 3 of 5 hypotheses CONTRADICTED        │
│     → Empirical testing essential         │
│                                            │
└────────────────────────────────────────────┘
```

**Notes**:
- Emphasize hypothesis contradictions as key scientific value
- Median vs mean disparity shows importance of robust statistics

---

### SLIDE 19: Future Work
**Layout**: Roadmap style

```
┌────────────────────────────────────────────┐
│  FUTURE WORK                               │
│                                            │
│  NEAR-TERM:                                │
│  ├─ 512³ resolution testing               │
│  ├─ Hybrid pipeline implementation        │
│  └─ Density variation experiments         │
│                                            │
│  MEDIUM-TERM:                              │
│  ├─ Dynamic scene update benchmarks       │
│  ├─ Secondary ray (shadows, AO) testing   │
│  └─ Multi-GPU comparison expansion        │
│                                            │
│  LONG-TERM:                                │
│  └─ BlockWalk traversal comparison [16]   │
└────────────────────────────────────────────┘
```

---

### SLIDE 20: Questions
**Layout**: Contact + resources

```
┌────────────────────────────────────────────┐
│                                            │
│           QUESTIONS?                       │
│                                            │
│        [Author Photo/Avatar]               │
│                                            │
│        [Email Address]                     │
│                                            │
│  ─────────────────────────────────────    │
│                                            │
│  RESOURCES:                                │
│  • Code: github.com/[repo]                │
│  • Data: [link to benchmark data]         │
│  • Paper: [link to draft]                 │
│                                            │
└────────────────────────────────────────────┘
```

---

## Assets Required

### Charts (from data/finalized/charts/)
- [ ] fps_by_pipeline.png
- [ ] frame_time_by_pipeline.png
- [ ] fps_by_resolution.png
- [ ] fps_by_scene.png
- [ ] cross_machine_comparison.png
- [ ] bandwidth_comparison.png
- [ ] resolution_heatmap.png
- [x] compression_fps_by_pipeline.png (NEW)
- [x] compression_bandwidth_by_pipeline.png (NEW)
- [x] compression_fps_by_resolution.png (NEW)
- [x] compression_raw_vs_compressed.png (NEW)

### Scene Images (from data/finalized/scene_images/)
- [ ] cornell_128_hwrt.png
- [ ] cityscape_128_hwrt.png
- [ ] noise_128_hwrt.png
- [ ] tunnels_128_hwrt.png

### Diagrams to Create
- [ ] Pipeline architecture diagram (3 pipelines)
- [ ] Test matrix coverage visualization
- [ ] Decision flowchart for pipeline selection

---

## Speaker Notes Summary

| Slide | Key Point | Time |
|-------|-----------|------|
| 1-3 | Hook audience with research question | 2 min |
| 4-6 | Background - set the stage | 3 min |
| 7-9 | Methodology - build credibility | 3 min |
| 10-13 | Results - performance data | 4 min |
| 13b-14b | Scene, bandwidth, compression | 3 min |
| 15-17 | Discussion - hypothesis eval + limits | 3 min |
| 18-19 | Conclusion - key findings + future | 2 min |
| 20-21 | Q&A | 2+ min |

**Total: ~22 minutes**

### Critical Talking Points
- **Slide 14b**: Compression INCREASES bandwidth - H5 contradicted (unexpected!)
- **Slide 15**: THREE hypotheses contradicted (H2, H3, H5) - major scientific value
- **Slide 13b**: Density variation tested implicitly via scene types
- **Slide 16**: Be upfront about limitations - builds credibility

---

## Sync Status with Draft Summary

| Element | Status | Notes |
|---------|--------|-------|
| Data stats | ✓ V3 | 981 tests, 5 GPUs (added Intel RaptorLake-S) |
| Performance claims | ✓ Conservative | 2.5-3.4x (not 3.7x) |
| Median FPS | ✓ Added | Critical for compute (215 vs 514 mean) |
| P99 frame times | ✓ Added | Slide 10 table |
| Instrumentation caveat | ✓ Prominent | Slide 10 notes, Slide 16 |
| Hypothesis outcomes | ✓ Synced | H1=PARTIAL, H2=CONTRAD, H3=CONTRAD, H5=CONTRAD |
| Scene characterization | ✓ Synced | Slide 13b with density levels |
| Compression effects | ✓ Synced | Slide 14b: +10% FPS compute |
| Limitations | ✓ Expanded | Statistical, Methodology, Scope categories |
| Key findings | ✓ Updated | 4 points including median/variance |

---

## Cycle 4 Changes Applied

- [x] Updated test count: 741 → 981
- [x] Updated GPU count: 4 → 5 (added Intel RaptorLake-S)
- [x] Updated performance claim: 3.7x → 2.5-3.4x (conservative)
- [x] Added median FPS to Slide 10 table
- [x] Added P99 frame times to Slide 10 table
- [x] Expanded limitations slide with statistical methodology concerns
- [x] Updated key findings with variance discussion
- [x] Added instrumentation caveat notes

---

*Blockout created: December 29, 2025*
*Last updated: December 29, 2025 (synced with Cycle 4 paper improvements)*
*Paper rating: 9.3/10*
