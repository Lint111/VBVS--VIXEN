---
title: Benchmark Troubleshooting Guide
aliases: [Benchmark Issues, Tester Support]
tags: [profiler, benchmark, troubleshooting, tester-experience]
created: 2025-12-17
---

# Benchmark Troubleshooting Guide

Quick reference for common issues when running VIXEN benchmarks.

---

## Startup Issues

### Benchmark Won't Start

**Symptom:** `vixen_benchmark.exe` fails immediately or shows "Application failed to start"

**Possible Causes:**

1. **Missing DLL Dependencies**
   - Check that `tbb12.dll` / `tbb12_debug.dll` is in the same folder
   - On some systems, Visual C++ Redistributables may be required
   - **Fix:** Run `vc_redist.x64.exe` if included in package

2. **No Vulkan Support**
   - GPU doesn't support Vulkan 1.3
   - Outdated GPU drivers
   - **Fix:** Update GPU drivers to latest version
   - **Check:** Run `vixen_benchmark.exe --list-gpus` to verify detection

3. **Incorrect Working Directory**
   - Benchmark looks for `shaders/` and `benchmark_config.json` relative to executable
   - **Fix:** Always run from the `VixenBenchmark` folder, not a subfolder

### GPU Not Detected

**Symptom:** `--list-gpus` shows no devices or shows integrated GPU only

**Possible Causes:**

1. **Discrete GPU Disabled**
   - Laptop may be in power-saving mode
   - **Fix:** Enable high-performance mode in power settings
   - **Fix:** Set `vixen_benchmark.exe` to prefer high-performance GPU in Windows Graphics Settings

2. **Driver Issue**
   - GPU driver doesn't expose Vulkan API
   - **Fix:** Install latest GPU drivers from manufacturer website (NVIDIA, AMD, Intel)

3. **Optimus/Hybrid Graphics**
   - Laptop with switchable graphics may default to integrated GPU
   - **Fix:** Force discrete GPU via NVIDIA Control Panel or AMD Radeon Settings

---

## Runtime Issues

### Crash During Benchmark

**Symptom:** Application crashes mid-run with no error message

**Possible Causes:**

1. **Out of VRAM**
   - Large grid sizes (512³) exceed GPU memory capacity
   - **Expected:** RTX 3060 (6GB) can handle 256³ but may fail at 512³
   - **Fix:** Use `--quick` mode which tests only 64³-256³ grids
   - **Fix:** Close other GPU-intensive applications

2. **Driver Timeout (TDR)**
   - Long-running compute shaders trigger Windows watchdog
   - **Symptom:** Screen flickers, driver resets, application crashes
   - **Fix:** Use `--headless` mode to reduce GPU load
   - **Workaround:** Increase TDR timeout in registry (advanced users only)

3. **Shader Compilation Failure**
   - Missing `shaders/` folder or corrupted shader cache
   - **Fix:** Ensure `shaders/` folder is present
   - **Fix:** Delete `cache/` folder and let it regenerate

### Freezing/Hanging

**Symptom:** Benchmark appears to freeze with no progress

**Possible Causes:**

1. **Normal Operation**
   - Large grid sizes (256³, 512³) take several seconds per frame
   - Warmup phase (100 frames) can take 30-60 seconds at high resolutions
   - **Not a Bug:** Wait for progress output (shown every 100 frames by default)
   - **Check:** Use `--verbose` to see per-frame progress

2. **Infinite Loop**
   - Rare shader compilation or validation layer issue
   - **Fix:** Press Ctrl+C to abort
   - **Workaround:** Use `--no-validation` to disable validation layers

### Poor Performance

**Symptom:** Benchmark runs but FPS is extremely low (< 10 FPS)

**Possible Causes:**

1. **Integrated GPU Selected**
   - Benchmark defaulted to low-power integrated graphics
   - **Check:** Look for "Intel UHD" or "AMD Radeon Vega" in output
   - **Fix:** Force discrete GPU (see "GPU Not Detected" section)

2. **GPU Thermal Throttling**
   - Laptop GPU overheating during sustained load
   - **Check:** Monitor GPU temperature with third-party tool
   - **Fix:** Improve laptop cooling (elevate, use cooling pad)
   - **Workaround:** Run benchmark in short bursts with cooldown periods

3. **Background GPU Load**
   - Other applications using GPU (games, video encoding, mining)
   - **Fix:** Close other GPU-intensive applications

---

## Data Quality Issues

### Missing or Zero Metrics

**Symptom:** JSON output contains `0.0` or `null` for specific metrics

**Expected Scenarios:**

1. **Pipeline-Specific Metrics**
   - `avg_voxels_per_ray` only valid for compute pipelines (not fragment/HW RT)
   - `blas_build_time_ms` only valid for HW RT pipeline
   - **Not a Bug:** Metrics are pipeline-dependent

2. **Disabled Features**
   - GPU utilization requires NVML support (NVIDIA GPUs only)
   - Shader cache hit rate only tracked after second run
   - **Expected:** First run shows 0% cache hit rate

**Unexpected Scenarios:**

1. **All Metrics Zero**
   - Benchmark ran but didn't collect data properly
   - **Check:** Verify `benchmark_results/` folder contains `.json` files
   - **Fix:** Re-run with `--verbose` to diagnose

2. **Invalid Timing Values**
   - Frame time < 0.1ms or > 10 seconds
   - **Possible Cause:** GPU query pool overflow or driver bug
   - **Fix:** Update GPU drivers

### Inconsistent Results

**Symptom:** Same config shows wildly different FPS across runs

**Possible Causes:**

1. **Insufficient Warmup**
   - First frames include shader compilation, pipeline creation overhead
   - **Fix:** Ensure warmup frames = 100 (default in current config)
   - **Verify:** Check `"warmup_frames": 100` in `benchmark_config.json`

2. **GPU Frequency Scaling**
   - GPU clock speed varies due to power management
   - **Fix:** Lock GPU to max performance mode (NVIDIA: prefer maximum performance)
   - **Workaround:** Use `--runs 3` to average multiple runs

3. **Thermal Throttling**
   - GPU performance degrades over time due to heat
   - **Check:** First configs show higher FPS than later configs
   - **Fix:** Allow GPU to cool between runs

---

## Submission Issues

### ZIP Package Not Created

**Symptom:** Benchmark completes but no ZIP file in output folder

**Possible Causes:**

1. **Packaging Disabled**
   - `--no-package` flag was used
   - **Fix:** Run without `--no-package` flag

2. **Write Permission Denied**
   - Output directory is read-only or locked by antivirus
   - **Check:** Verify you have write access to `Downloads/VIXEN_Benchmarks/`
   - **Fix:** Run as administrator or choose different output directory with `--output`

3. **Disk Space Exhausted**
   - No space for ZIP file (typically 50-200 MB)
   - **Fix:** Free up disk space

### Results Folder Doesn't Auto-Open

**Symptom:** Benchmark completes but Explorer doesn't open

**Expected Scenarios:**

1. **Headless Mode**
   - `--headless` disables auto-open by design
   - **Manual:** Navigate to `C:\Users\<You>\Downloads\VIXEN_Benchmarks\`

2. **Auto-Open Disabled**
   - `--no-open` flag was used
   - **Manual:** Check completion banner for exact path

**Unexpected Scenarios:**

1. **Permission Issue**
   - Explorer failed to launch (rare)
   - **Workaround:** Open folder manually from completion banner path

### Invalid Tester Name

**Symptom:** `--tester` flag rejected with error message

**Possible Causes:**

1. **Name Too Short**
   - Minimum 2 characters required
   - **Fix:** Use real name or alias (e.g., `--tester "JD"`)

2. **Special Characters**
   - Some characters may be rejected for filesystem safety
   - **Fix:** Use alphanumeric characters only (A-Z, 0-9, spaces, hyphens)

---

## Validation Layer Warnings

**Symptom:** Console flooded with Vulkan validation layer messages

**Expected Scenarios:**

1. **Info/Performance Messages**
   - Warnings about suboptimal usage are informational only
   - **Ignore:** These don't affect correctness or results

**Unexpected Scenarios:**

1. **Error Messages**
   - Red "ERROR" messages indicate real Vulkan API violations
   - **Report:** Share full console output with developers
   - **Workaround:** Use `--no-validation` to disable (may hide real bugs)

2. **Excessive Warnings**
   - Validation layer overhead causes severe performance degradation
   - **Fix:** Use `--no-validation` for production benchmark runs
   - **Note:** Validation layers are enabled by default for debugging

---

## Vulkan Initialization Errors

### Invalid Device Handle Error

**Symptom:** `[Vulkan Loader] ERROR: vkCreateCommandPool: Invalid device [VUID-vkCreateCommandPool-device-parameter]`

**Cause:** Device creation failed silently during graph compilation, but logging was disabled

**Analysis:**
- Error occurs during `CommandPoolNode::CompileImpl` when calling `vkCreateCommandPool`
- The `VulkanDevice::device` handle is `VK_NULL_HANDLE` or invalid
- `DeviceNode::CreateLogicalDevice()` likely failed but error wasn't visible because node logging was disabled
- Shader compilation succeeds (before device creation error), but command pool creation fails

**Root Causes:**

1. **DeviceNode Logging Disabled**
   - DeviceNode errors during `CreateLogicalDevice()` not visible
   - Common causes: Missing Vulkan layers, extension conflicts, driver issues
   - **Check:** Review `DeviceNode::CompileImpl` error handling (returns early on failure)

2. **VkDevice Creation Failure**
   - `vkCreateDevice()` may have failed due to:
     - Missing required extensions
     - Validation layer conflicts
     - Outdated drivers
     - Insufficient GPU capabilities
   - **Check:** Enable DeviceNode logging to see actual error

3. **InstanceNode Initialization Failure**
   - If VkInstance creation fails, DeviceNode receives `VK_NULL_HANDLE` instance
   - This cascades to device creation failure
   - **Check:** Validation layers enabled but not installed

**Diagnostic Steps:**

1. **Enable Node Logging** (requires source access):
   ```cpp
   // In BenchmarkGraphFactory::BuildInfrastructure or BenchmarkRunner
   auto* deviceNode = graph->GetNode<DeviceNode>("benchmark_device");
   if (deviceNode && deviceNode->GetLogger()) {
       deviceNode->GetLogger()->SetEnabled(true);
       deviceNode->GetLogger()->SetTerminalOutput(true);
   }
   ```

2. **Check Validation Layer Availability**:
   - Verify Vulkan SDK is properly installed
   - Check `VK_LAYER_PATH` environment variable
   - Test with `--no-validation` flag to bypass layer requirements

3. **Verify GPU Driver Support**:
   - Update to latest GPU drivers
   - Verify Vulkan 1.3 support via `vulkaninfo` or `vkvia`
   - Check for extension compatibility issues

4. **Test with Minimal Configuration**:
   - Disable RTX extensions if present
   - Run with `--no-validation`
   - Test on different GPU if available

**Immediate Workarounds:**

1. **Disable Validation Layers**:
   ```
   vixen_benchmark.exe --no-validation
   ```
   This bypasses validation layer loading which may be causing the failure

2. **Update Vulkan SDK**:
   - Install latest Vulkan SDK from LunarG
   - Ensure validation layers are properly installed
   - Reboot after installation

3. **Check Driver Compatibility**:
   - NVIDIA: GeForce driver 520+ for Vulkan 1.3
   - AMD: Adrenalin 22.3+ for Vulkan 1.3
   - Intel: Graphics driver 30.0.101.1960+ for Vulkan 1.3

**Developer Fix:**

The benchmark should enable DeviceNode logging by default to surface these errors. Add to `BenchmarkGraphFactory::BuildInfrastructure`:

```cpp
// Enable infrastructure node logging for debugging
if (auto* deviceNode = static_cast<DeviceNode*>(graph->GetInstance(nodes.device))) {
    auto logger = deviceNode->GetLogger();
    logger->SetEnabled(true);
    logger->SetTerminalOutput(true);
}
if (auto* instanceNode = static_cast<InstanceNode*>(graph->GetInstance(nodes.instance))) {
    auto logger = instanceNode->GetLogger();
    logger->SetEnabled(true);
    logger->SetTerminalOutput(true);
}
```

This ensures device/instance creation errors are visible to testers.

---

## Emergency Recovery

### Complete System Hang

**Symptom:** Entire system freezes, mouse/keyboard unresponsive

**Cause:** GPU driver crash (TDR failure)

**Recovery:**
1. Wait 10-15 seconds for automatic recovery
2. If no recovery, press `Ctrl+Alt+Delete` to open Task Manager
3. End `vixen_benchmark.exe` process
4. If still frozen, hard reboot (hold power button)

**Prevention:**
- Update GPU drivers
- Use `--quick` mode for initial testing
- Avoid 512³ grids on GPUs with < 8GB VRAM

### Results Corrupted

**Symptom:** JSON files contain garbage data or are truncated

**Cause:** Benchmark crashed during file write

**Recovery:**
1. Delete incomplete files from `benchmark_results/`
2. Re-run benchmark
3. Check disk health if problem persists

---

## Getting Help

### Before Reporting Issues

Collect the following information:

1. **System Info**
   - GPU model and VRAM size
   - Driver version
   - Windows version

2. **Benchmark Command**
   - Exact command used (e.g., `vixen_benchmark.exe --quick --render`)

3. **Console Output**
   - Copy full console output including error messages
   - Use `--verbose` for detailed logging

4. **Results Files**
   - If crash occurred, check for partial output in `benchmark_results/`

### Known Limitations

1. **512³ Grid Support**
   - Requires > 6GB VRAM
   - Not supported on RTX 3060 (6GB) and similar GPUs
   - **Expected Behavior:** Crash or out-of-memory error

2. **Windows Only**
   - Benchmark package is Windows-specific
   - Linux support requires source build

3. **NVIDIA-Specific Features**
   - GPU utilization monitoring (NVML) only available on NVIDIA GPUs
   - AMD/Intel GPUs show `null` for NVML metrics

---

## Related Documentation

- [[TesterPackage-Workflow|TesterPackage Workflow]] - Normal usage instructions
- [[Profiling|Profiling Guide]] - Performance analysis
- [[../Libraries/Profiler|Profiler Library]] - Implementation details
- [[Data-Visualization-Pipeline|Data Pipeline]] - Result processing

---

## Changelog

| Date | Change |
|------|--------|
| 2025-12-17 | Initial creation for Sprint 2 completion |
