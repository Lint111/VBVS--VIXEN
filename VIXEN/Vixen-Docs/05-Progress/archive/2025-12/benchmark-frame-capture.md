---
tags: [feature, complete]
created: 2025-12-09
status: complete
priority: medium
---

# Feature: Benchmark Frame Capture

## Overview

**Objective:** Capture rendered frames during benchmark tests for debugging artifacts and validating output.

**Requester:** User

**Branch:** `claude/phase-k-hardware-rt`

---

## Requirements

1. **Automatic low-res capture** at middle frame of each test (quarter resolution)
2. **Manual high-res capture** on 'C' keypress (full resolution)  
3. **Storage:** `{output_dir}/debug_images/{test_name}_{frame_number}.png`

---

## Discovery Findings

### Related Code

| File | Relevance | Notes |
|------|-----------|-------|
| `libraries/Profiler/src/BenchmarkRunner.cpp:1220-1310` | Direct | Frame loop - capture hook point |
| `libraries/VulkanResources/include/VulkanSwapChain.h:43-102` | Direct | SwapChainPublicVariables provides VkImage access |
| `libraries/EventBus/include/InputEvents.h:14-53` | Direct | KeyCode enum - add 'C' key |
| `libraries/RenderGraph/src/Nodes/InputNode.cpp` | Direct | Key tracking logic |
| `libraries/Profiler/src/MetricsExporter.cpp` | Pattern | File I/O pattern reference |

### Affected Subsystems

- [x] Profiler
- [x] RenderGraph (InputNode)
- [x] EventBus (KeyCode)
- [ ] SVO
- [ ] CashSystem
- [ ] VulkanResources

### Complexity Assessment

- **Estimated effort:** Medium
- **Risk level:** Low
- **Breaking changes:** No

---

## Design Decisions

### Decision 1: FrameCapture Location

**Context:** Where should the new FrameCapture class live?

**Options Considered:**
1. **Profiler library** - Benchmark-specific, already handles file I/O
2. **RenderGraph** - More general-purpose location

**Chosen:** Profiler library

**Rationale:** Frame capture is benchmark-specific functionality; Profiler already manages output directories and file exports.

### Decision 2: Synchronous vs Async Capture

**Context:** Should capture block or use async fence wait?

**Chosen:** Synchronous with fence wait

**Rationale:** 
- Automatic capture is once per test (outside measurement window)
- Manual capture is user-triggered (blocking acceptable)
- Async adds complexity for minimal benefit

### Decision 3: Downsampling Approach

**Context:** How to produce quarter-resolution captures?

**Chosen:** CPU-side resize with stb_image_resize2 after readback

**Rationale:** GPU blit to intermediate image adds complexity; stb resize is simple and fast enough for single captures.

---

## Implementation Plan

### Phase 1: Core FrameCapture Class
- [x] Task 1.1: Create FrameCapture.h header
- [x] Task 1.2: Implement FrameCapture.cpp with Vulkan readback

### Phase 2: Input Enhancement  
- [x] Task 2.1: Add KeyCode::C to InputEvents.h
- [x] Task 2.2: Track 'C' key in InputNode.cpp

### Phase 3: BenchmarkRunner Integration
- [x] Task 3.1: Add FrameCapture member and initialization
- [x] Task 3.2: Implement automatic mid-frame capture
- [x] Task 3.3: Implement manual 'C' key capture

### Phase 4: Build & Test
- [x] Task 4.1: Update CMakeLists.txt
- [x] Task 4.2: Build verification
- [ ] Task 4.3: Runtime test

---

## Progress Log

### 2025-12-09 - Planning Complete

- Explored benchmark system architecture
- Identified integration points in BenchmarkRunner frame loop
- Designed FrameCapture class with Vulkan readback
- Plan approved, ready for implementation

### 2025-12-09 - Implementation Complete

- Created FrameCapture.h/cpp with Vulkan image readback
- Added KeyCode::C and InputNode tracking
- Integrated capture logic in BenchmarkRunner frame loop
- Added GetInputState() public accessor to InputNode
- Profiler library builds successfully
- Pending: Runtime test (benchmark exe was running during session)

---

## Files Changed

| File | Change Type | Description |
|------|-------------|-------------|
| `libraries/Profiler/include/Profiler/FrameCapture.h` | New | Frame capture class header |
| `libraries/Profiler/src/FrameCapture.cpp` | New | Vulkan readback implementation |
| `libraries/EventBus/include/InputEvents.h` | Modified | Add KeyCode::C |
| `libraries/RenderGraph/src/Nodes/InputNode.cpp` | Modified | Track 'C' key |
| `libraries/Profiler/include/Profiler/BenchmarkRunner.h` | Modified | Add FrameCapture member |
| `libraries/Profiler/src/BenchmarkRunner.cpp` | Modified | Capture integration in frame loop |
| `libraries/Profiler/CMakeLists.txt` | Modified | Add new source file |
