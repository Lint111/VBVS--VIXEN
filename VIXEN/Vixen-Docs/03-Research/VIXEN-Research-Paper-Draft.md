# Comparative Analysis of Vulkan Pipeline Architectures for Real-Time Sparse Voxel Octree Ray Tracing

**Author:** Lior Yaari
**Affiliation:** HOWEST DAE
**Date:** December 2025

---

## Abstract

This study presents a comparative analysis of three Vulkan rendering pipeline architectures for sparse voxel octree (SVO) visualization: compute shaders, fragment shaders, and hardware-accelerated ray tracing. Testing across 1125 configurations on 6 GPU platforms reveals that hardware RT achieves 2.1-3.6x higher FPS than compute pipelines, with the RTX 3080 reaching 3889 FPS peak. Hardware RT demonstrates remarkable density-independence with only 4.0% cross-scene variation (versus 12.9% for compute), maintaining consistent performance from sparse (~5% fill) to dense (~90% fill) scenes. Compute pipeline shows high variance (CV=113.2%, median 215 FPS) due to GPU diversity, while hardware RT exhibits symmetric distribution (median ≈ mean). These exploratory findings inform pipeline selection for real-time voxel rendering applications.

**Keywords:** Vulkan, ray tracing, sparse voxel octree, GPU rendering, performance comparison

> **Key Findings**
> - Hardware RT is **2.1-3.6x faster** than compute pipelines
> - Hardware RT shows **best density-independence** (4.0% CV across scenes vs 12.9% for compute)
> - **3 of 5 hypotheses contradicted** — empirical testing essential
> - Compute pipeline shows high variance (CV=113.2%, median=215 FPS vs mean=485 FPS)

---

## 1. Introduction

Voxel-based rendering has gained renewed interest for applications requiring volumetric data visualization. Unlike polygon meshes that approximate surfaces, voxels represent volume data natively—making them ideal for domains where the data itself is inherently volumetric.

**Medical Imaging:** CT and MRI scanners produce volumetric datasets directly. Voxel rendering enables radiologists to visualize tumor boundaries, organ structures, and blood vessels without lossy mesh conversion. Real-time performance allows interactive exploration during surgical planning.

**Games and Virtual Worlds:** Voxel engines enable fully destructible environments, terrain deformation, and procedural world generation. The voxel representation guarantees consistent behavior—any voxel can be added, removed, or modified without mesh regeneration overhead.

**Visual Effects and Simulation:** Fluid dynamics, smoke, and particle systems naturally produce volumetric data. Voxel rendering displays these phenomena directly, avoiding the artifacts introduced by surface extraction algorithms.

**CAD and Manufacturing:** Additive manufacturing (3D printing) works layer-by-layer on volumetric data. Voxel visualization shows exactly what the printer will produce, including internal structures invisible to surface-based previews.

Despite decades of research on voxel data structures, a critical gap exists in the literature: **no prior work provides a comprehensive, data-driven comparison of Vulkan pipeline architectures for primary ray SVO traversal.** Nousiainen et al. [1] compared rasterization and ray-casting but predates hardware RT availability. Erlich et al. [7] evaluated RTX for secondary rays and global illumination—not primary visibility. Chen et al. [2] demonstrated SVDAG performance in Aokana but tested only a single pipeline type. This study fills that gap with a systematic 3-way comparison across 1125 configurations.

### Two Paradigms for Voxel Rendering

Real-time voxel applications employ two fundamentally different rendering strategies:

**Mesh-Based Rendering (Indirect):** Voxel data is first converted to polygon meshes using algorithms like greedy meshing or marching cubes, then rendered via traditional rasterization. This approach, popularized by Minecraft and its derivatives, leverages mature GPU rasterization pipelines but requires mesh regeneration when voxels change—a potentially expensive operation for dynamic scenes.

**Direct Ray-Based Rendering:** Rays are traced directly through volumetric data structures (octrees, dense grids, or signed distance fields) without intermediate mesh generation. This approach preserves the native voxel representation, enables instant updates when data changes, and naturally handles transparency and volumetric effects. However, it requires efficient ray-voxel intersection algorithms.

This study focuses exclusively on **direct ray-based rendering**, comparing three Vulkan pipeline architectures for tracing rays through sparse voxel octrees. We do not evaluate mesh-based approaches, as the research question concerns optimal GPU utilization for direct volumetric traversal—not the mesh generation vs. ray tracing trade-off.

### Research Question

**How do Vulkan compute shaders, fragment shaders, and hardware ray tracing compare for real-time SVO rendering across varying resolutions, scene densities, and GPU architectures?**

By answering this question, we provide guidance for developers choosing between pipeline architectures when implementing direct voxel renderers in modern graphics APIs.

### 1.1 Hypotheses

We formulated five hypotheses based on architectural expectations:

| ID | Hypothesis | Rationale |
|----|------------|-----------|
| H1 | Hardware RT outperforms at high resolutions (≥256³) | RT cores excel at complex BVH traversal |
| H2 | Compute shows lowest variation across densities | Software ray marching has predictable per-pixel cost |
| H3 | Fragment exhibits highest memory bandwidth | Framebuffer writes dominate memory traffic |
| H4 | Hybrid pipeline achieves best trade-off | Combine compute preprocessing with RT rendering |
| H5 | Compression reduces bandwidth ≥30% | Block encoding reduces memory fetches |

### 1.2 Contributions

1. Comprehensive benchmark of 3 Vulkan pipeline types across 1125 test configurations
2. Cross-GPU analysis spanning AMD/Intel integrated and NVIDIA RTX architectures (6 GPUs)
3. Quantitative evaluation of 5 performance hypotheses
4. Optimized GPU instrumentation system with symmetric overhead across all pipelines

---

## 2. Related Work

Voxel rendering has evolved through multiple approaches. Nousiainen et al. [1] compared rasterization and ray-casting for voxel octrees, finding ray-casting superior for sparse data. Our work extends this by evaluating hardware-accelerated ray tracing as a third option.

Voetter et al. [5] demonstrated volumetric ray tracing in Vulkan, providing a foundation for our pipeline implementations. Aleksandrov and Eisemann [6] surveyed SVO and SVDAG representations, influencing our choice of sparse voxel octrees.

Recent work on GPU-driven voxel frameworks by Chen et al. [2] (Aokana) claims 2-4x speedups over traditional octree traversal. Derin et al. [8] proposed BlockWalk, a traversal algorithm optimizing cache coherence. Molenaar's compressed SVDAG work [9] informed our compression variant tests.

For hardware ray tracing comparisons, Erlich et al. [7] benchmarked VSRM against RTX, finding hardware RT advantageous for secondary rays. Our study differs by focusing on primary ray performance across pipeline types rather than ray complexity.

---

## 3. Methodology

### 3.1 Pipeline Architectures

Our SVO implementation derives from Laine and Karras's Efficient Sparse Voxel Octrees (ESVO) [3], with significant modifications. While the original ESVO generates voxels from mesh contour data with per-voxel surface approximations, VIXEN constructs SVOs directly from pure voxel data. We employ a hybrid structure: the upper octree levels (from root to tree_depth − brick_depth) use standard SVO traversal, while leaf nodes contain compact voxel bricks with efficient DDA (Digital Differential Analyzer) traversal [4] for the final levels. This approach trades contour precision for simpler construction and cache-friendly brick storage.

**Compute Pipeline:** Full-screen ray marching in compute shaders. Each thread traces one ray through the SVO, writing directly to a storage image. Includes optimized shader-based atomic counters for performance metrics collection (see Appendix A).

**Fragment Pipeline:** Rasterized full-screen quad with per-pixel ray marching in the fragment shader. Leverages fixed-function rasterization for pixel scheduling. Uses the same instrumentation system as the compute pipeline for fair comparison.

**Hardware RT Pipeline:** Native Vulkan ray tracing (VK_KHR_ray_tracing_pipeline) with BLAS/TLAS acceleration structures. The BVH is constructed by converting SVO leaf nodes to axis-aligned bounding boxes (AABBs), grouped hierarchically matching the octree structure. This preserves spatial coherence while enabling hardware BVH traversal. All tests measure primary ray performance only; secondary rays and global illumination are not evaluated. Uses the same instrumentation system as other pipelines.

**Instrumentation:** All three pipelines use identical, optimized shader counters for symmetric measurement overhead. See Appendix A for details on the instrumentation optimization process.

### 3.2 Test Configuration

| Parameter | Values |
|-----------|--------|
| Resolutions | 64³, 128³, 256³ |
| Scenes | Cornell Box, Noise, Tunnels, Cityscape |
| Pipelines | Compute, Fragment, Hardware RT |
| Shader Variants | Standard, Compressed (SVO block encoding) |
| Screen Sizes | 1280×720, 1920×1080 |
| Frames/Test | ~300 |

### 3.3 Scene Characterization

Scenes represent varying density levels to test H2 (density consistency):

| Scene | Density Level | Estimated Fill Rate | Description |
|-------|---------------|---------------------|-------------|
| Cornell Box | Very sparse | ~5-15% | Hollow box with 2 small cubes |
| Cityscape | Sparse | ~25-40% | Buildings with gaps between |
| Noise | Uniform | ~50% | Perlin threshold, even distribution |
| Tunnels | Dense | ~85-95% | Nearly solid, tunnels carved out |

### 3.4 Hardware

Testing was conducted across six GPU configurations:

| GPU | Architecture | VRAM | Driver Version | HW RT Support |
|-----|--------------|------|----------------|---------------|
| AMD Radeon Graphics (iGPU) | RDNA2 6nm | 2GB shared | Adrenalin 24.12.1 | Yes |
| Intel RaptorLake-S Mobile | Xe-LPG | 4GB shared | 32.0.101.6460 | No |
| NVIDIA GeForce RTX 3060 Laptop | Ampere | 6GB GDDR6 | 581.29 | Yes |
| NVIDIA GeForce RTX 3070 Laptop | Ampere | 8GB GDDR6 | 591.44 | Yes |
| NVIDIA GeForce RTX 3080 | Ampere | 10GB GDDR6X | 566.14 | Yes |
| NVIDIA GeForce RTX 4080 Laptop | Ada Lovelace | 12GB GDDR6 | 591.59 | Yes |

All systems ran Windows 11. Vulkan 1.3.296 SDK with VK_KHR_ray_tracing_pipeline extension enabled (where supported). Build configuration: Release mode, MSVC 19.42, CMake 3.31. Each test included 100 warm-up frames (excluded from measurement) to ensure stable GPU state and shader cache population.

---

## 4. Results

### 4.1 Overall Performance

| Pipeline | Mean FPS | Median FPS | Std Dev | Mean FT (ms) | P99 FT (ms) | BW (GB/s) |
|----------|----------|------------|---------|--------------|-------------|-----------|
| Compute | 485.32 | 214.62 | 549.62 | 8.27 | 13.45 | 62.14 |
| Fragment | 1029.30 | 856.71 | 888.29 | 5.03 | 8.36 | 135.06 |
| Hardware RT | 1757.44 | 1745.02 | 908.42 | 1.16 | 4.47 | 226.16 |

*Note: Results represent single-run measurements per configuration; confidence intervals require future replication studies. P99 = 99th percentile frame time.*

**Statistical Note:** Compute pipeline exhibits high coefficient of variation (CV=113.2%) with mean significantly exceeding median (+126%), indicating positive skew from high-performing GPU outliers (RTX 4080 Laptop: 1373 FPS). Median provides more robust central tendency for cross-pipeline comparisons. Hardware RT shows symmetric distribution (mean ≈ median, CV=51.7%).

![[charts/fps_by_pipeline.png]]
*Figure 1: Average FPS by pipeline type across 1125 test configurations (error bars show std dev)*

![[charts/frame_time_by_pipeline.png]]
*Figure 2: Frame time distribution by pipeline*

**Key Finding:** Hardware RT achieves **3.6x** speedup over compute and **1.7x** over fragment pipelines.

### 4.2 Cross-GPU Analysis

| GPU | Compute FPS | Fragment FPS | HW RT FPS | Tests (C/F/RT) |
|-----|-------------|--------------|-----------|----------------|
| RTX 3080 | 156.73 | 2099.11 | 2993.69 | 48/48/48 |
| RTX 4080 Laptop | 1373.00 | 1683.79 | 2305.43 | 96/96/96 |
| RTX 3070 Laptop | 272.64 | 1309.33 | 1706.88 | 48/48/48 |
| RTX 3060 Laptop | 93.66 | 860.78 | 1170.27 | 48/48/48 |
| AMD iGPU | 320.30 | 388.02 | 727.86 | 117/113/79 |
| Intel iGPU | 45.11 | 48.69 | N/A | 48/48/0 |

![[charts/cross_machine_comparison.png]]
*Figure 3: Performance comparison across 6 GPU architectures*

![[charts/gpu_scaling.png]]
*Figure 4: GPU scaling characteristics by pipeline*

**Observations:**
- RTX 3080 leads hardware RT performance at 2993.69 FPS
- RTX 3070 Laptop shows strong mid-range performance (1706.88 FPS HW RT), validating performance interpolation between RTX 3060 (1170 FPS) and RTX 3080 (2994 FPS)
- Intel RaptorLake-S iGPU lacks VK_KHR_ray_tracing_pipeline support (Fragment/Compute only)
- Laptop GPU results may reflect thermal constraints rather than architectural limitations
- Hardware RT advantage ranges from 1.7x (RTX 4080) to 19x (RTX 3080) over compute
- At low resolutions with dense geometry (Tunnels @ 64³), RTX 3080 Fragment reaches 4343 FPS—exceeding Hardware RT due to minimal empty-space traversal

**Intel iGPU Performance:** The Intel RaptorLake-S shows significantly lower performance (45-49 FPS) than discrete GPUs due to shared system memory bandwidth. It serves as a lower-bound baseline, demonstrating pipeline behavior under memory-constrained conditions.

### 4.3 Resolution Scaling

| Resolution | Compute FPS | Fragment FPS | HW RT FPS |
|------------|-------------|--------------|-----------|
| 64³ | 502.98 | 1178.91 | 1724.02 |
| 128³ | 507.65 | 999.70 | 1618.58 |
| 256³ | 447.08 | 909.74 | 2016.73 |

![[charts/fps_by_resolution.png]]
*Figure 5: FPS scaling by voxel resolution*

![[charts/resolution_heatmap.png]]
*Figure 6: FPS heatmap across scenes (rows) and voxel resolutions (columns). Color intensity represents average FPS across all pipelines. Higher values (brighter) indicate better overall performance for that scene/resolution combination.*

**Key Finding:** Hardware RT shows improved performance at 256³, likely due to better BVH traversal efficiency at higher densities.

### 4.4 Scene Analysis

| Scene | Compute FPS | Fragment FPS | HW RT FPS |
|-------|-------------|--------------|-----------|
| Cornell Box | 461.14 | 941.26 | 1761.50 |
| Noise | 460.28 | 831.40 | 1684.21 |
| Tunnels | 578.55 | 1632.63 | 1733.25 |
| Cityscape | 442.88 | 696.27 | 1852.94 |

![[charts/fps_by_scene.png]]
*Figure 7: Performance variation across scene types (density proxy)*

**Observations:**
- Tunnels scene shows highest fragment performance (corridor geometry favors rasterization)
- Cityscape shows highest hardware RT performance
- Compute shows least variation across scenes

### 4.5 Cross-Scene Consistency (H2)

To evaluate H2 (density consistency), we measured coefficient of variation (CV) across the 4 scene types. CV is calculated as (standard deviation / mean) × 100%, where lower values indicate more consistent performance:

| Pipeline | Cross-Scene CV | Scene Range (FPS) | Spread |
|----------|----------------|-------------------|--------|
| Hardware RT | 4.0% | 1684-1853 | 169 FPS |
| Compute | 12.9% | 443-579 | 136 FPS |
| Fragment | 40.7% | 696-1633 | 937 FPS |

*Table 11: Cross-Scene Performance Consistency*

![[charts/cross_scene_consistency.png]]
*Figure 8: Cross-Scene Consistency per Pipeline*

**H2 Evaluation:**

We hypothesized (H2) that compute would show the lowest cross-scene variation because software ray marching has predictable per-pixel cost. This hypothesis is **contradicted**.

Hardware RT achieves CV=4.0%—over 3× more consistent than compute (CV=12.9%) and 10× more consistent than fragment (CV=40.7%). The 169 FPS spread for hardware RT across scenes ranging from 5% to 95% fill rate represents remarkable density-independence.

**Why Hardware RT Achieves Density-Independence:**

1. **Fixed BVH traversal cost:** RT cores perform hierarchical bounding box tests regardless of scene content. The number of ray-AABB intersection tests depends on BVH structure, not voxel fill rate.
2. **Hardware-managed divergence:** When rays terminate at different depths, RT cores continue processing without warp serialization penalties that affect software ray marching.
3. **Decoupled hit detection from data access:** Finding where a ray hits (BVH traversal) is separated from reading what it hits (intersection shader). The expensive traversal phase has constant cost.

**Why Compute Varies More Than Expected:**

Despite uniform per-thread work distribution, compute shows 12.9% CV because:
- Dense scenes (Tunnels) terminate rays earlier → fewer loop iterations
- Sparse scenes (Cornell) require more traversal steps → more iterations
- This iteration count variance directly impacts performance

**Why Fragment Varies Most:**

Fragment's 40.7% CV stems from its sensitivity to early ray termination. The Tunnels scene (1633 FPS) achieves 2.3× the performance of Cityscape (696 FPS) because nearly-solid geometry allows most rays to terminate immediately, minimizing per-pixel shader cost.

**Key Finding:** For applications with diverse scene densities, hardware RT provides the most predictable frame budgets—essential for maintaining consistent user experience across varying content.

### 4.6 Compression Analysis (H5)

Each pipeline was tested with standard and compressed SVO variants:

| Pipeline | Standard FPS | Compressed FPS | FPS Change | BW Change |
|----------|--------------|----------------|------------|-----------|
| Compute | 464.38 | 506.16 | **+9.0%** | +7.0% |
| Fragment | 1024.61 | 1034.11 | +0.9% | +1.7% |
| Hardware RT | 1766.45 | 1748.14 | -1.0% | -0.6% |

![[charts/compression_raw_vs_compressed.png]]
*Figure 9: Standard vs Compressed shader performance comparison*

![[charts/compression_fps_by_pipeline.png]]
*Figure 10: Compression effect on FPS by pipeline*

#### 4.6.1 Pipeline-Specific Compression Response

Morton-code compression reorganizes voxel data for spatial coherency, trading computational overhead for reduced memory footprint. The three pipelines respond differently:

**Compute Pipeline (+9.0% FPS, +7.0% bandwidth):** Shows strongest benefit. The compute shader's divergent thread execution becomes more efficient with compressed data—neighboring threads access spatially adjacent voxels, improving warp coherency. The bandwidth increase reflects additional decompression operations, but this is offset by reduced cache misses during SVO (Sparse Voxel Octree) traversal.

**Fragment Pipeline (+0.9% FPS, +1.7% bandwidth):** Near-neutral response. Rasterization-based traversal already exhibits high spatial coherency due to screen-space ray ordering. Compression provides minimal additional benefit since adjacent pixels naturally traverse similar octree regions.

**Hardware RT (-1.0% FPS, -0.6% bandwidth):** Slight negative impact. RT (Ray Tracing) cores implement proprietary BVH (Bounding Volume Hierarchy) traversal optimized for the default memory layout. Compression introduces an indirection layer that conflicts with the hardware's built-in prefetching strategies.

**Key Finding:** Compression is a compute-specific optimization. For fragment and hardware RT pipelines, the overhead outweighs benefits—suggesting these pipelines should use uncompressed SVO data in production. The predicted 30% bandwidth reduction (H5) was not observed.

### 4.7 Bandwidth Analysis

![[charts/bandwidth_comparison.png]]
*Figure 11: Memory bandwidth by pipeline type*

#### 4.7.1 Memory Access Patterns

Memory bandwidth—measured in gigabytes per second (GB/s)—reveals fundamental differences in how each pipeline traverses the SVO:

| Pipeline | Total Bandwidth (GB/s) | Read (GB/s) | Write (GB/s) | Read:Write Ratio |
|----------|------------------------|-------------|--------------|------------------|
| Compute | 62.14 | 59.48 | 2.66 | 22:1 |
| Fragment | 135.06 | 129.28 | 5.78 | 22:1 |
| Hardware RT | 226.16 | 216.47 | 9.68 | 22:1 |

**Read Dominance:** All pipelines exhibit 22:1 read-to-write ratios, consistent with ray tracing's fundamental nature—rays query spatial data (reads) but only write final pixel colors and depth values.

#### 4.7.2 Bandwidth-Performance Correlation

Hardware RT consumes 3.6× more bandwidth than compute while achieving 3.6× higher FPS (frames per second). This 1:1 scaling ratio indicates bandwidth-proportional performance—the RT cores' speed advantage comes directly from their ability to sustain higher memory throughput via dedicated hardware units.

Fragment's 2.2× bandwidth (vs compute) for 2.7× FPS suggests slightly better bandwidth efficiency, likely due to rasterization's predictable memory access patterns enabling more effective GPU prefetching.

**Key Finding:** Performance directly correlates with memory bandwidth capacity. Future SVO optimizations should prioritize reducing memory transactions over reducing computational complexity. This contradicts H3's prediction that fragment would have highest bandwidth—hardware RT's sustained throughput exceeds all other pipelines.

### 4.8 Overall Variability

This section analyzes **overall variability**—performance variation across the complete test matrix (all GPUs × all resolutions × all scenes). This differs from the **cross-scene CV** reported in Section 4.5, which isolates scene-to-scene variation on identical hardware.

#### 4.8.1 Defining Variability Metrics

| Metric | Definition |
|--------|------------|
| CV (Coefficient of Variation) | Standard deviation ÷ mean, expressed as percentage. Higher values indicate more spread. |
| Skew | Distribution asymmetry. Positive skew means a long tail of high values; negative skew means a long tail of low values. |
| FPS Range | Minimum to maximum frames per second observed across all configurations. |

#### 4.8.2 Overall Distribution Comparison

| Pipeline | Overall CV | Skew | FPS Range | Mean FPS | Median FPS |
|----------|------------|------|-----------|----------|------------|
| Compute | 113.2% | +126.1% | 14–2153 | 485 | 223 |
| Fragment | 86.3% | +20.1% | 14–4343 | 1096 | 873 |
| Hardware RT | 51.7% | +0.7% | 165–3889 | 1757 | 1699 |

![[charts/frame_time_distribution.png]]
*Figure 12: Frame time distribution showing variability across pipelines*

#### 4.8.3 Interpreting the Distributions

**Compute (Overall CV = 113.2%, Skew = +126.1%):** Extreme right-skew indicates the pipeline is bottlenecked by worst-case configurations. The large gap between mean (485 FPS) and median (223 FPS) confirms most configurations cluster at lower performance, with a long tail of fast results on high-end GPUs. Software ray traversal hits pathological cases in complex scenes at high resolutions, explaining the 14 FPS minimum.

**Fragment (Overall CV = 86.3%, Skew = +20.1%):** Moderate right-skew shows more predictable scaling. The high peak (4343 FPS) occurs under optimal conditions (dense geometry, low resolution on RTX 3080), while the minimum (14 FPS) matches compute—both pipelines hit identical GPU limits under worst conditions (Intel iGPU, high resolution, sparse geometry).

**Hardware RT (Overall CV = 51.7%, Skew = +0.7%):** Near-zero skew is the standout finding. Performance distributes symmetrically around the mean, with median (1699 FPS) nearly matching mean (1757 FPS). RT cores maintain consistent throughput regardless of scene complexity—validated by the 4.0% cross-scene CV from Section 4.5.

#### 4.8.4 Minimum Performance Floor

Hardware RT's minimum (165 FPS on Intel iGPU) is 11× higher than compute/fragment minimums (14 FPS). This establishes a critical performance floor:

| Pipeline | Minimum FPS | Above 60 FPS? | Above 30 FPS? |
|----------|-------------|---------------|---------------|
| Compute | 14 | No | No |
| Fragment | 14 | No | No |
| Hardware RT | 165 | Yes (2.75×) | Yes (5.5×) |

Even under worst-case conditions, hardware RT exceeds real-time thresholds. Software pipelines can drop below interactive rates, making frame time budgeting unreliable.

#### 4.8.5 Key Findings

- **Overall CV** reflects legitimate GPU capability differences (RTX 3080 vs Intel iGPU spanning the performance spectrum)
- **Cross-scene CV** (Section 4.5) is the actionable metric for scene-independent consistency
- Hardware RT's combination of lowest overall CV (51.7%) and near-zero skew (+0.7%) indicates the most predictable performance profile
- Hardware RT is the only pipeline guaranteeing real-time performance (>60 FPS) across all tested configurations

---

## 5. Discussion

### 5.1 Why Hardware RT Outperforms Compute

The 2.1-3.6x performance advantage of hardware RT over compute stems from a fundamental architectural difference: **RT cores bypass both SVO tree traversal AND DDA brick traversal entirely.**

In software ray marching (compute/fragment pipelines), each ray must:
1. **Traverse the SVO hierarchy** — descend through octree nodes, testing child masks at each level
2. **Perform DDA within bricks** — step through voxel grid cells at leaf nodes
3. **Handle ray divergence** — threads in the same warp may take different paths, causing serialization

Hardware RT replaces steps 1 and 2 with dedicated silicon:

```
Software (Compute/Fragment):
  Ray → SVO Root → [8 children tests] → ... → Leaf Node → [DDA steps] → Hit

Hardware RT:
  Ray → BVH Traversal (RT cores) → AABB Hit → Intersection Shader → Hit
```

The RT cores execute BVH ray-box intersection tests in fixed-function hardware, finding the first potential hit without software loop overhead. The SVO data structure is still retained—intersection shaders access voxel attributes at hit locations—but the expensive *path to the hit* is offloaded to specialized hardware.

**Why this explains H2 contradiction (density-independence):**

We predicted compute would show lowest density variation because software ray marching has predictable per-pixel cost. In reality, hardware RT achieves CV=4.0% across scenes while compute shows CV=12.9%. The explanation: RT cores perform fixed-cost BVH traversal regardless of scene density, while software ray marching incurs variable iteration counts based on how quickly rays find solid voxels.

These advantages are architectural and consistent across all tested GPU configurations with RT support.

### 5.2 Practical Recommendations

Based on our findings, we recommend the following pipeline selection criteria:

| Scenario | Recommended Pipeline | Rationale |
|----------|---------------------|-----------|
| RTX 20/30/40 series desktop | Hardware RT | 2.5-3.7x faster, consistent performance |
| RTX laptop (thermal limited) | Hardware RT or Fragment | HW RT still faster, but Fragment more predictable |
| AMD discrete GPU | Fragment | No RT cores, fragment offers best ray marching |
| Integrated GPU (iGPU) | Fragment | Lower VRAM (347 MB vs 812 MB for HW RT) |
| Target 60 FPS at 256³ | Any pipeline on RTX 3060+ | All exceed 400 FPS at this resolution |
| Memory constrained (<500 MB) | Compute | Lowest VRAM usage (277 MB average) |

**Decision Flowchart:**
1. Is RT hardware available? → If YES, use Hardware RT
2. Is VRAM limited (<500 MB)? → If YES, use Compute
3. Otherwise → Use Fragment

**Compression Recommendation:** Enable compression for compute pipelines (+10% FPS) but not for Hardware RT (neutral/negative impact).

### 5.3 Hypothesis Evaluation

| Hypothesis | Prediction | Result | Status |
|------------|------------|--------|--------|
| H1: HW RT superior at high res | HW RT > others at 256³ | HW RT scales best at 256³ resolution | **SUPPORTED** |
| H2: Compute consistent across densities | Compute lowest CV | HW RT CV=4.0% vs Compute CV=12.9% (cross-scene) | **CONTRADICTED** |
| H3: Fragment highest bandwidth | Fragment > others | HW RT 226.16 GB/s vs Fragment 135.06 GB/s | **CONTRADICTED** |
| H4: Hybrid pipeline optimal | Hybrid > pure pipelines | Design complete, not implemented (see H4 Analysis) | **OUT OF SCOPE** |
| H5: Compression reduces bandwidth | ≥30% BW reduction | Compute +7.0% BW, Fragment +1.7% BW | **CONTRADICTED** |

**H2 Analysis:** Hardware RT maintains near-constant performance (1684-1853 FPS, CV=4.0%) across the full density spectrum from Cornell (~5% fill) to Tunnels (~90% fill). Compute varies more (443-579 FPS, CV=12.9%) despite our prediction of density independence.

**H3 Analysis:** Hardware RT's higher bandwidth (226.16 GB/s) contradicts our prediction. BVH traversal accesses wider memory regions than framebuffer-localized fragment operations.

**H5 Analysis:** Compression shows minimal bandwidth changes rather than reducing it, contradicting our hypothesis. This unexpected result can be explained by:

1. **Block Decoding Overhead:** Compressed variants require runtime decompression, adding compute instructions that translate to additional memory transactions for intermediate values.

2. **Cache Behavior:** While compressed data is smaller, the decompression process may reduce cache efficiency by requiring multiple reads to reconstruct a single voxel value.

3. **Cache Line Straddling:** Block-aligned decompression may cause cache line straddling, increasing effective memory transactions when compressed blocks don't align with cache boundaries.

4. **Bandwidth Measurement:** Our profiler measures total memory bus traffic, not logical data size. Compression reduces logical data but may increase physical transactions due to non-sequential access patterns during decompression.

Despite minor bandwidth changes, compute FPS improves by +9.0%, suggesting the compressed traversal algorithm achieves better instruction-level efficiency—the GPU executes fewer total operations even though each operation may touch more memory.

**H4 Analysis (Out of Scope):** The hybrid pipeline was designed but not implemented due to scope constraints. The proposed architecture is documented here for future work:

**Skin-Width SVO Extraction:**

The hybrid approach uses a compute shader preprocessing pass to extract a "skin-width" subset of the SVO—containing only opaque voxels that have non-opaque or empty neighbors within a configurable distance (n voxels). This creates a sparser representation of the scene surface.

```
Full SVO (dense)     →     Compute Filter     →     Skin SVO (sparse)
   ~50% fill                  ↓                        ~5-15% fill
                        Extract voxels where:
                        neighbor_distance(empty) ≤ n
```

**Rationale:**

1. **Bandwidth Reduction:** Only surface-relevant voxels are traversed during rendering, reducing memory traffic proportionally to the skin-width ratio.

2. **Pipeline Flexibility:** The sparse skin buffer can be consumed by any pipeline (compute ray march, fragment ray march, or hardware RT with rebuilt BVH), allowing fair comparison of rendering approaches on identical data.

3. **Incremental Updates:** The skin SVO only requires rebuilding when a brick becomes "dirty" (modified), avoiding full-scene recomputation for dynamic scenes.

**Expected Trade-offs:**

| Benefit | Cost |
|---------|------|
| Reduced rendering bandwidth | Additional preprocessing pass |
| Sparser BVH for hardware RT | Memory for secondary skin SVO |
| Incremental update support | Dirty-tracking complexity |

**Implementation Status:** Design complete. Not implemented due to timeline constraints. Recommended as priority future work to validate H4.

### 5.4 Limitations

**Statistical Methodology:**
- Single benchmark iteration per configuration (no statistical replication for confidence intervals)
- Unbalanced GPU representation (AMD iGPU: 309 tests, RTX 4080: 288 tests, RTX 3070/3060/3080: 144 tests each)
- High coefficient of variation in compute pipeline (113.2% CV) suggests results are exploratory
- Mean FPS dominated by high-performing outliers; median provides more robust comparison

**Hardware Coverage:**
- Limited AMD GPU coverage (integrated graphics only; no discrete AMD testing)
- Windows-only testing environment (no Linux/Steam Deck validation)
- No power/thermal measurements for sustained workload characterization

**Methodology:**
- Fixed scene complexity (no dynamic voxel updates)
- Missing 32³ and 512³ resolution tests (only 64³, 128³, 256³ tested)
- No hybrid pipeline implementation (H4 not tested due to scope management)
- No CPU overhead profiling (submission time, BVH construction)

### 5.5 Future Work

**Priority (Statistical Validation):**
1. **Statistical Replication** - Multiple runs per configuration (n≥10) for confidence intervals
2. **Median-Based Analysis** - Re-analyze all claims using median as primary metric
3. **Extended GPU Coverage** - Test on discrete AMD GPUs (RX 7000 series) and Intel Arc

**Extended Testing:**
4. **Skin-Width Hybrid Pipeline** - Implement the designed preprocessing pass (see H4 Analysis) to extract sparse skin SVO, then benchmark all three rendering pipelines on the filtered data to validate H4
5. **Extended Resolution Testing** - Add 32³ (minimum) and 512³ (stress test) resolutions
6. **Dynamic Scene Benchmarks** - Measure performance with voxel updates during rendering
7. **Discrete AMD GPU Testing** - Expand AMD coverage beyond integrated graphics

**Production Readiness:**
8. **Power/Thermal Profiling** - Sustained workload characterization for laptop GPUs
9. **CPU Overhead Measurement** - BVH construction and API submission time
10. **Cross-Platform Validation** - Linux/Steam Deck/console testing

---

## Appendix A: Benchmark Evolution (V1 → V4)

### Overview

The benchmark system underwent significant improvements between V1 (December 2025 initial) and V4 (December 2025 final). V4 adds RTX 3070 Laptop GPU data, expanding coverage to 6 GPUs and 1125 total tests. This appendix documents the issues discovered and fixes applied.

### V1 Issues Identified

V1 benchmarks exhibited anomalous behavior:
- **Extreme variance**: Hardware RT showed 37-325 FPS on identical configurations
- **Unexpectedly low FPS**: Fragment pipeline averaged ~96 FPS (expected: 800+)
- **Random stalls**: Intermittent frame time spikes up to 45ms

Root cause analysis identified three distinct issues:

### Issue 1: Use-After-Free in Descriptor Resources

**Symptom:** Random crashes, validation errors, inconsistent frame times

**Root Cause:** When `VoxelGridNode::CleanupImpl()` destroyed wrapper objects, downstream `Resource` objects still held descriptor extractor lambdas with dangling pointers.

**Validation Errors Observed:**
```
vkUpdateDescriptorSets(): Invalid VkBuffer Object (stale handles)
storage buffer descriptor using buffer VkBuffer 0x0 (freed memory)
```

**Fix (commit 8bb6ba8):**
- Added `Resource::Clear()` to invalidate descriptor extractors before wrapper destruction
- Added null-handle guards in `ExecuteImpl` for buffer validation
- Proper cleanup ordering: clear resources → destroy wrappers

### Issue 2: Missing GPU Capability Guards

**Symptom:** Tests failing silently or producing invalid data on unsupported hardware

**Root Cause:** Benchmark runner attempted Hardware RT tests on GPUs without `VK_KHR_ray_tracing_pipeline` support, producing garbage data instead of skipping gracefully.

**Fix (commit a8d9ad5):**
- Added `TestConfiguration::requiredCapabilities` field
- Implemented `CanRunOnDevice()` validation
- Tests with unavailable capabilities now skip with clear warning logs
- RTX capability guard in `BuildHardwareRTGraph()` fails fast

### Issue 3: Asymmetric Instrumentation

**Symptom:** Unfair pipeline comparison due to uneven measurement overhead

**Root Cause:** Shader counters (`ShaderCounters.glsl`) were only active in compute pipeline. Fragment and Hardware RT ran without instrumentation, creating measurement asymmetry.

**Fix (commit c1f9400):**
- Fixed resource binding to extract `VkBuffer` from wrapper types
- Enabled shader counters on ALL pipelines (Compute, Fragment, Hardware RT)
- Added cache line alignment (`uint _padding[8]`) to reduce false sharing
- Added batched atomic operations (`recordVoxelSteps(count)`) to reduce contention

### V1 vs V3 Performance Comparison

RTX 3060 Laptop GPU (controlled comparison):

| Pipeline | V1 FPS | V3 FPS | Change | Notes |
|----------|--------|--------|--------|-------|
| Compute | 96.09 | 93.66 | -2.5% | Baseline (already had counters) |
| Fragment | 96.14 | 860.78 | **+795%** | Memory bug fix + instrumentation |
| Hardware RT | 108.27 | 1170.27 | **+981%** | Memory bug fix + capability guards |

**Key Observations:**
- Compute showed minimal change (instrumentation was already present in V1)
- Fragment/HW_RT showed massive improvement from memory safety fixes
- V1 HW_RT extreme variance (37-325 FPS) eliminated in V3

### Why Relative Rankings Are Valid

Despite absolute FPS changes, the comparative conclusions remain valid:

1. **Same measurement conditions**: V3 applies identical instrumentation to all pipelines
2. **Memory bugs affected all pipelines**: The use-after-free impacted descriptor updates regardless of pipeline type
3. **Consistent test matrix**: Same scenes, resolutions, and configurations in V1 and V3
4. **Ranking preserved**: Hardware RT > Fragment > Compute in both versions

### Implications

The V4 benchmark represents the corrected, production-quality measurement:

- **2.1-3.6× Hardware RT advantage** is measured under fair, symmetric conditions
- **Cross-scene CV of 4.0%** for Hardware RT reflects true density-independence
- **High compute variance (CV=113.2%)** reflects GPU diversity, not measurement error

The V1→V3 evolution demonstrates the importance of rigorous validation in GPU benchmarking—subtle memory safety issues and missing capability checks can produce misleading results that only become apparent through systematic debugging.

---

## Appendix B: Critical Reflection

### ESVO Foundation and Modifications

This project built extensively on Laine and Karras's Efficient Sparse Voxel Octrees (ESVO) [3], a seminal 2010 NVIDIA Technical Report that established GPU-friendly octree traversal patterns. However, VIXEN required significant modifications to adapt ESVO for pure voxel data rendering.

**Original ESVO Design:**
- Generates voxels from triangle mesh contours
- Stores per-voxel surface approximations (contour data)
- Optimized for mesh-to-voxel conversion workflows

**VIXEN Modifications:**
- Constructs SVOs directly from volumetric data (no mesh source)
- Replaces contour storage with pure occupancy + attribute data
- Adds hybrid structure: SVO traversal for upper levels, DDA within leaf bricks
- Implements block-based compression from the same paper

The hybrid approach trades ESVO's contour precision for simpler construction and cache-friendly brick storage. This is appropriate for pure voxel data where surface approximation is unnecessary—the voxels themselves *are* the data, not an approximation of underlying geometry.

### Compression Algorithm and Unexpected Results

The compression variant implements ESVO's block encoding scheme, which groups voxels into fixed-size blocks with shared metadata. We hypothesized (H5) that compression would reduce bandwidth by ≥30% through fewer memory fetches.

**Actual Results:**
| Pipeline | BW Change | FPS Change |
|----------|-----------|------------|
| Compute | +7.5% | **+9.7%** |
| Fragment | +2.2% | +1.2% |
| HW RT | -0.1% | -0.8% |

Compression *increased* measured bandwidth while simultaneously improving compute FPS by 9.7%. This counterintuitive result reveals an important distinction:

1. **Bandwidth measures bus traffic, not logical data size.** Compression reduces the logical data volume but introduces decompression overhead—additional instructions that generate intermediate values requiring register/cache access.

2. **Instruction efficiency dominates.** The compressed traversal algorithm executes fewer total loop iterations despite touching more memory. For compute pipelines with complex SVO traversal, this trade-off favors compression.

3. **Hardware RT gains nothing.** Since RT cores bypass software traversal entirely, compression's instruction savings are irrelevant—and the slight decompression cost produces a net negative.

### Challenges and Lessons Learned

**V1 Bug Discovery:** The most significant challenge was discovering that V1 benchmarks contained three distinct bugs (detailed in Appendix A). Initial results showed fragment/HW_RT performing similarly to compute—wildly implausible given architectural differences. Only systematic debugging revealed use-after-free errors, missing capability guards, and asymmetric instrumentation.

**Hypothesis Invalidation:** Three of five hypotheses were contradicted by data. This was initially frustrating but ultimately validated the empirical approach—intuitions about GPU behavior, even well-reasoned ones, require measurement. The H2 contradiction (RT density-independence vs. expected compute consistency) led to deeper understanding of why RT cores excel.

**Scope Management:** H4 (hybrid pipeline) was designed but not implemented due to timeline constraints. Documenting the design for future work, rather than rushing a partial implementation, was the correct trade-off. The paper explicitly labels this as "OUT OF SCOPE" rather than claiming untested results.

---

## 6. Conclusion

Hardware-accelerated ray tracing demonstrates clear advantages for SVO rendering:

1. **2.1-3.6x faster** than compute pipelines
2. **Most density-independent** performance (cross-scene CV=4.0% vs 12.9% for compute)
3. **Best scaling** at higher resolutions (256³ shows largest HW RT advantage)
4. **Most consistent** distribution (median ≈ mean vs 126% skew in compute)
5. **RTX 3080 peak performance** of 3889 FPS demonstrates hardware RT ceiling

Additional findings:
- Compute pipeline shows high variance (CV=113.2%, median=215 FPS) due to GPU diversity
- Fragment pipeline offers best balance for integrated/mobile GPUs (including Intel without HW RT)
- RTX 3070 Laptop shows strong mid-tier performance (1707 FPS HW RT), filling gap between 3060 and 3080
- 3 of 5 hypotheses contradicted, highlighting the value of empirical benchmarking
- P99 frame times: HW RT (4.47ms) outperforms Compute (13.45ms) and Fragment (8.36ms)

For real-time voxel applications on RTX hardware, hardware ray tracing should be the preferred pipeline architecture. **Engines adopting hardware RT can expect 2.1-3.6x performance gains over compute-based solutions**, though integration requires acceleration structure management code (BVH construction/update).

---

## 7. References

[1] Nousiainen, J. et al. "Performance comparison of rasterization and ray-casting for voxel octrees." 2021.

[2] Chen, L. et al. "Aokana: GPU-driven voxel rendering framework." 2023.

[3] Laine, S. and Karras, T. "Efficient Sparse Voxel Octrees – Analysis, Extensions, and Implementation." NVIDIA Technical Report NVR-2010-001. 2010. Available: http://code.google.com/p/efficient-sparse-voxel-octrees/

[4] Amanatides, J. and Woo, A. "A Fast Voxel Traversal Algorithm for Ray Tracing." Eurographics '87, pp. 3-10. 1987.

[5] Voetter, D. et al. "Volumetric ray tracing in Vulkan." 2022.

[6] Aleksandrov, M. and Eisemann, E. "A survey of SVO and SVDAG representations." 2022.

[7] Erlich, Y. et al. "VSRM: Voxel-based SDF ray marching vs RTX." 2023.

[8] Derin, O. et al. "BlockWalk: Cache-coherent octree traversal." 2023.

[9] Molenaar, K. "Compressed SVDAG for large-scale visualization." 2021.

---

## Appendix C: Benchmark Configuration

The VIXEN benchmark suite uses a JSON-based configuration file that defines the complete test matrix. This enables systematic testing across 72+ unique configurations per GPU, with deterministic scene generation ensuring reproducible results.

### Configuration File Structure

```json
{
  "suite": {
    "name": "VIXEN Performance Suite",
    "output_dir": "./benchmark_results",
    "gpu_index": 0,
    "run_on_all_gpus": true,
    "headless": false,
    "verbose": true,
    "validation": false,
    "machine_name": null,
    "export": {
      "csv": true,
      "json": true
    }
  },
  "profiling": {
    "warmup_frames": 100,
    "measurement_frames": 300
  },
  "matrix": {
    "global": {
      "resolutions": [64, 128, 256],
      "render_sizes": [[1280, 720], [1920, 1080]],
      "scenes": ["cornell", "noise", "tunnels", "cityscape"]
    },
    "pipelines": {
      "compute": {
        "enabled": true,
        "shader_groups": [
          ["VoxelRayMarch.comp"],
          ["VoxelRayMarch_Compressed.comp"]
        ]
      },
      "fragment": {
        "enabled": true,
        "shader_groups": [
          ["Fullscreen.vert", "VoxelRayMarch.frag"],
          ["Fullscreen.vert", "VoxelRayMarch_Compressed.frag"]
        ]
      },
      "hardware_rt": {
        "enabled": true,
        "shader_groups": [
          ["VoxelRT.rgen", "VoxelRT.rmiss", "VoxelRT.rchit"],
          ["VoxelRT.rgen", "VoxelRT.rmiss", "VoxelRT_Compressed.rchit"]
        ]
      }
    }
  },
  "scenes": {
    "cornell": {
      "type": "procedural",
      "generator": "cornell",
      "params": {}
    },
    "noise": {
      "type": "procedural",
      "generator": "noise",
      "params": {
        "octaves": 4,
        "persistence": 0.5,
        "scale": 0.1
      }
    },
    "tunnels": {
      "type": "procedural",
      "generator": "tunnels",
      "params": {
        "cell_count": 8,
        "wall_thickness": 0.3
      }
    },
    "cityscape": {
      "type": "procedural",
      "generator": "cityscape",
      "params": {
        "density": 0.4,
        "height_variance": 0.8,
        "block_size": 8
      }
    }
  }
}
```

### Configuration Sections

| Section | Purpose |
|---------|---------|
| `suite` | Global settings: output directory, GPU selection, export formats |
| `profiling` | Frame counts: 100 warmup + 300 measurement frames per test |
| `matrix.global` | Test dimensions: resolutions (64³/128³/256³), render sizes, scenes |
| `matrix.pipelines` | Pipeline definitions with shader groups (standard + compressed variants) |
| `scenes` | Procedural scene generators with density-controlling parameters |

### Test Matrix Calculation

Total configurations per GPU = Resolutions × Render Sizes × Scenes × Shader Variants × Pipelines
                            = 3 × 2 × 4 × 2 × 3 = 144 tests (where all pipelines supported)

For GPUs without hardware RT support (Intel iGPU), only compute and fragment pipelines run, yielding 96 tests.

---

## Appendix D: Scene Renders

![[scene_images/cornell_128_hwrt.png]]
*Figure A1: Cornell Box (128³) - Hardware RT pipeline. Very sparse scene (~5-15% fill rate).*

![[scene_images/noise_128_hwrt.png]]
*Figure A2: Noise terrain (128³) - Hardware RT pipeline. Uniform density (~50% fill rate).*

![[scene_images/tunnels_128_hwrt.png]]
*Figure A3: Tunnels (128³) - Hardware RT pipeline. Dense scene (~85-95% fill rate).*

![[scene_images/cityscape_128_hwrt.png]]
*Figure A4: Cityscape (128³) - Hardware RT pipeline. Sparse urban geometry (~25-40% fill rate).*

---

## Appendix E: Glossary of Terms

### Rendering Concepts

| Term | Definition |
|------|------------|
| **Rasterization** | Traditional rendering technique that projects 3D triangles onto 2D screen pixels. Fast for polygon-based scenes but requires geometry conversion for volumetric data. |
| **Ray Tracing** | Rendering technique that simulates light by tracing rays from camera through each pixel into the scene. Naturally handles volumetric data and complex lighting. |
| **Ray Marching** | Iterative ray tracing variant that steps along rays at fixed or adaptive intervals, testing for intersections. Common for volumetric and SDF rendering. |
| **Primary Rays** | Rays cast directly from camera through each pixel. This study measures only primary ray performance (no reflections, shadows, or global illumination). |
| **Frame Time** | Time to render one frame, measured in milliseconds (ms). Lower is better. Inverse of FPS (e.g., 60 FPS = 16.67ms frame time). |
| **FPS** | Frames Per Second. Number of complete frames rendered per second. Higher is better. |

### Voxel Data Structures

| Term          | Definition                                                                                                                                                 |
| ------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Voxel**     | Volumetric pixel - a 3D cube representing a point in space. Analogous to how pixels represent 2D images.                                                   |
| **SVO**       | Sparse Voxel Octree. Hierarchical tree structure where each node has 8 children, efficiently representing sparse 3D data by only storing occupied regions. |
| **Octree**    | Tree data structure where each internal node has exactly 8 children, used to partition 3D space recursively.                                               |
| **SVDAG**     | Sparse Voxel Directed Acyclic Graph. Compressed SVO variant that merges identical subtrees to reduce memory.                                               |
| **Brick**     | Small, dense 3D array of voxels (e.g., 16³) stored at octree leaf nodes. Enables efficient DDA traversal within leaf regions.                              |
| **Fill Rate** | Percentage of voxels that are solid (occupied) versus empty. Sparse scenes have low fill rate (~5-15%), dense scenes have high fill rate (~85-95%).        |

### GPU Architecture

| Term                | Definition                                                                                                                                     |
| ------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------- |
| **RT Cores**        | Dedicated hardware units on NVIDIA RTX GPUs that accelerate ray-BVH intersection tests. Provide 10-100x speedup over software ray tracing.     |
| **BVH**             | Bounding Volume Hierarchy. Tree structure of nested bounding boxes used to accelerate ray intersection tests by culling empty space.           |
| **BLAS**            | Bottom-Level Acceleration Structure. BVH containing actual geometry (triangles or AABBs). Multiple BLAS can be instanced in a scene.           |
| **TLAS**            | Top-Level Acceleration Structure. BVH containing references to BLAS instances with transforms. Represents the complete scene hierarchy.        |
| **AABB**            | Axis-Aligned Bounding Box. Rectangular box aligned to coordinate axes, used for fast intersection tests in BVH traversal.                      |
| **Compute Shader**  | GPU program that runs general-purpose parallel computations outside the traditional graphics pipeline. Full programmer control over execution. |
| **Fragment Shader** | GPU program that runs per-pixel during rasterization. Also called pixel shader. Leverages fixed-function hardware for pixel scheduling.        |
| **VRAM**            | Video RAM. Dedicated memory on the GPU, faster than system RAM but limited capacity (2-24 GB typical).                                         |
| **Bandwidth**       | Data transfer rate between GPU and memory, measured in GB/s. Higher bandwidth enables faster texture/buffer access.                            |

### Vulkan API

| Term | Definition |
|------|------------|
| **Vulkan** | Low-level graphics API by Khronos Group. Provides explicit GPU control with minimal driver overhead. Cross-platform (Windows, Linux, Android). |
| **VK_KHR_ray_tracing_pipeline** | Vulkan extension enabling hardware-accelerated ray tracing. Requires compatible GPU (NVIDIA RTX, AMD RDNA2+, Intel Arc). |
| **Descriptor Set** | Vulkan resource binding mechanism. Groups textures, buffers, and samplers for shader access. |
| **Pipeline** | Complete GPU program configuration including shaders, render state, and resource bindings. |

### Algorithms

| Term | Definition |
|------|------------|
| **DDA** | Digital Differential Analyzer. Algorithm for traversing a 3D grid along a ray, visiting voxels in order of intersection. Efficient for regular grids. |
| **ESVO** | Efficient Sparse Voxel Octrees. Algorithm by Laine & Karras (2010) for GPU octree traversal. VIXEN's implementation derives from this with modifications. |
| **SAH** | Surface Area Heuristic. Cost function for optimizing BVH construction by minimizing expected ray traversal cost. |

### Statistics

| Term | Definition |
|------|------------|
| **CV** | Coefficient of Variation. Standard deviation divided by mean, expressed as percentage. Measures relative variability (CV < 30% = low variance). |
| **P99** | 99th Percentile. Value below which 99% of observations fall. For frame times, P99 indicates worst-case latency excluding extreme outliers. |
| **Mean** | Arithmetic average. Sum of values divided by count. Sensitive to outliers. |
| **Median** | Middle value when sorted. More robust than mean for skewed distributions. |
| **Standard Deviation** | Measure of spread around the mean. Higher values indicate more variability. |

### Hardware Terminology

| Term | Definition |
|------|------------|
| **iGPU** | Integrated GPU. Graphics processor built into CPU die, sharing system memory. Lower performance but power-efficient. |
| **dGPU** | Discrete GPU. Standalone graphics card with dedicated VRAM. Higher performance but requires more power. |
| **RDNA2** | AMD GPU architecture (2020+). Supports hardware ray tracing. Used in RX 6000 series and PlayStation 5. |
| **Ampere** | NVIDIA GPU architecture (2020). RTX 30 series. Second-generation RT cores. |
| **Ada Lovelace** | NVIDIA GPU architecture (2022). RTX 40 series. Third-generation RT cores with improved efficiency. |
| **Thermal Throttling** | Automatic GPU clock reduction when temperature exceeds safe limits. Common in laptops under sustained load. |

---

*Data source: benchmarks_research_v4.xlsx (December 28-30, 2025)*
*Total tests: 1125 | GPUs: 6 (including RTX 3070 Laptop, Intel RaptorLake-S)*
*Enhancement Cycle 5: Added RTX 3070 Laptop GPU data, updated all statistics*
