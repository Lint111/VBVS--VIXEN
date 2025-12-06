---
title: Voxel Research Papers
aliases: [Papers, Bibliography, Research Papers]
tags: [research, papers, bibliography, academic]
created: 2025-12-06
related:
  - "[[Overview]]"
  - "[[ESVO-Algorithm]]"
  - "[[Pipeline-Comparison]]"
---

# Voxel Research Papers

Bibliography of 24+ research papers covering voxel rendering, ray tracing, sparse voxel octrees, and GPU optimization.

---

## 1. Core Algorithms

### 1.1 Sparse Voxel Octrees

| Paper | Authors | Year | Key Contribution |
|-------|---------|------|------------------|
| **Efficient Sparse Voxel Octrees** | Laine, Karras | 2010 | Parametric ESVO traversal |
| GigaVoxels: Ray-Guided Streaming | Crassin et al. | 2009 | Out-of-core rendering |
| Efficient GPU Screen-Space Ray Tracing | Uralsky et al. | 2015 | Hierarchical traversal |
| Real-Time Voxel Cone Tracing | Crassin et al. | 2011 | Global illumination |

### 1.2 Ray Traversal

| Paper | Authors | Year | Key Contribution |
|-------|---------|------|------------------|
| **A Fast Voxel Traversal Algorithm** | Amanatides, Woo | 1987 | DDA algorithm |
| Octree Traversal for Ray Tracing | Revelles et al. | 2000 | Parametric octree |
| Stackless KD-Tree Traversal | Popov et al. | 2007 | GPU-friendly traversal |
| Understanding KD-Tree Ray Traversal | Wald et al. | 2006 | Optimization techniques |

---

## 2. GPU Rendering

### 2.1 Vulkan/GPU Pipeline

| Paper | Authors | Year | Key Contribution |
|-------|---------|------|------------------|
| Real-Time Ray Tracing | Shirley et al. | 2009 | RT fundamentals |
| Ray Tracing Deformable Scenes | Wald et al. | 2007 | Dynamic BVH |
| Fast Parallel Construction of BVHs | Lauterbach et al. | 2009 | GPU BVH build |
| RTX: Best Practices | NVIDIA | 2020 | Hardware RT optimization |

### 2.2 Memory Optimization

| Paper | Authors | Year | Key Contribution |
|-------|---------|------|------------------|
| Perfect Spatial Hashing | Lefebvre, Hoppe | 2006 | Compact storage |
| Symmetric BVH | Dammertz et al. | 2008 | Memory layout |
| Compact Normal Storage | Meyer et al. | 2010 | Normal compression |
| High-Performance SVO | Kamblers et al. | 2013 | GPU SVO optimization |

---

## 3. Compression

### 3.1 Texture Compression

| Paper | Authors | Year | Key Contribution |
|-------|---------|------|------------------|
| Real-Time DXT Compression | Van Waveren | 2006 | S3TC implementation |
| Block Compression | Microsoft | 2014 | BC1-BC7 formats |
| ASTC: Adaptive Compression | ARM | 2012 | Variable bit-rate |

### 3.2 Voxel-Specific Compression

| Paper | Authors | Year | Key Contribution |
|-------|---------|------|------------------|
| Geometry Images | Gu et al. | 2002 | Mesh to image |
| Geometry Clipmaps | Losasso, Hoppe | 2004 | LOD terrain |
| Real-Time Deformable Terrain | Laine | 2006 | Dynamic voxels |

---

## 4. Global Illumination

### 4.1 Indirect Lighting

| Paper | Authors | Year | Key Contribution |
|-------|---------|------|------------------|
| Light Propagation Volumes | Kaplanyan | 2009 | LPV technique |
| **Voxel Cone Tracing** | Crassin et al. | 2011 | SVO-based GI |
| SSAO | Mittring | 2007 | Screen-space AO |
| GTAO | Activision | 2016 | Ground truth AO |

### 4.2 Path Tracing

| Paper | Authors | Year | Key Contribution |
|-------|---------|------|------------------|
| Physically Based Rendering | Pharr et al. | 2016 | PBRT reference |
| Production Path Tracing | Christensen | 2018 | Practical techniques |
| ReSTIR | Bitterli et al. | 2020 | Spatiotemporal resampling |

---

## 5. Hybrid Approaches

| Paper | Authors | Year | Key Contribution |
|-------|---------|------|------------------|
| Hybrid Rendering | Epic Games | 2018 | UE4 approach |
| Surface-Volume Hybrid | Kamblers | 2020 | Voxel + mesh |
| Adaptive Rendering | Adobe | 2019 | Quality/perf tradeoff |

---

## 6. Reference Documents

### 6.1 Primary Bibliography

**Location:** `C:\Users\liory\Docs\Performance comparison on rendering methods for voxel data.pdf`

Contains 24 curated papers with annotations relevant to VIXEN's research goals.

### 6.2 ESVO Reference Implementation

**Location:** `C:\Users\liory\Downloads\source-archive\efficient-sparse-voxel-octrees\`

| File | Content |
|------|---------|
| `trunk/src/octree/Octree.cpp` | `castRay()` implementation |
| `trunk/src/octree/build/Builder.cpp` | Octree construction |
| License | BSD 3-Clause (NVIDIA 2009-2011) |

---

## 7. VIXEN-Specific Papers

Papers most relevant to current development:

### 7.1 Critical Path

1. **Laine & Karras (2010)** - ESVO core algorithm
2. **Amanatides & Woo (1987)** - Brick DDA
3. **Van Waveren (2006)** - DXT compression
4. **Crassin et al. (2009)** - GigaVoxels streaming

### 7.2 Future Work

1. **Crassin et al. (2011)** - Voxel cone tracing for GI
2. **NVIDIA RTX (2020)** - Hardware RT optimization
3. **ReSTIR (2020)** - Advanced sampling

---

## 8. Citation Format

```bibtex
@article{laine2010esvo,
  title={Efficient Sparse Voxel Octrees},
  author={Laine, Samuli and Karras, Tero},
  journal={IEEE Transactions on Visualization and Computer Graphics},
  volume={17},
  number={8},
  pages={1048--1059},
  year={2010},
  publisher={IEEE}
}

@article{amanatides1987dda,
  title={A Fast Voxel Traversal Algorithm for Ray Tracing},
  author={Amanatides, John and Woo, Andrew},
  journal={Eurographics},
  volume={87},
  number={3},
  pages={3--10},
  year={1987}
}
```

---

## 9. Key Insights

### 9.1 Performance Patterns

| Pattern | Source | Application |
|---------|--------|-------------|
| Parametric traversal | ESVO | Avoid per-step division |
| Octant mirroring | ESVO | Uniform positive ray |
| Stack-free traversal | Popov | GPU coherence |
| Block compression | S3TC | Memory bandwidth |

### 9.2 Architectural Lessons

| Lesson | Source | VIXEN Application |
|--------|--------|-------------------|
| Cache-line aware | NVIDIA RTX | Child descriptor grouping |
| Warp coherence | CUDA guides | Uniform branching |
| Memory aliasing | Vulkan spec | Transient resources |

---

## 10. Related Pages

- [[ESVO-Algorithm]] - Core algorithm implementation
- [[Pipeline-Comparison]] - Research methodology
- [[Overview]] - Research goals
- [[../02-Implementation/SVO-System|SVO System]] - Code implementation
