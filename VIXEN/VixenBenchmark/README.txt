================================================================================
                         VIXEN BENCHMARK TOOL
                    GPU Ray Tracing Performance Test
================================================================================

Thank you for helping test VIXEN! This tool measures your GPU's ray tracing
performance across different rendering techniques.

================================================================================
                            QUICK START
================================================================================

STEP 1: Run the benchmark
   - Double-click "vixen_benchmark.exe"
   - A window will open showing the benchmark running
   - Wait for it to complete (typically 5-15 minutes)

STEP 2: Find your results
   - Results automatically open in your Downloads folder when done
   - Look for: Downloads/VIXEN_Benchmarks/VIXEN_benchmark_*.zip

STEP 3: Send the ZIP file
   - Email the ZIP file to the benchmark coordinator
   - That's it! The ZIP contains all the data we need

================================================================================
                         SYSTEM REQUIREMENTS
================================================================================

MINIMUM:
- Windows 10 or Windows 11
- GPU with Vulkan support (any modern NVIDIA, AMD, or Intel GPU)
- Updated graphics drivers (less than 6 months old recommended)

RECOMMENDED:
- NVIDIA RTX 20-series or newer (for hardware ray tracing tests)
- AMD RX 6000-series or newer (for hardware ray tracing tests)
- 8GB+ system RAM

NOTE: You do NOT need to install the Vulkan SDK. The benchmark uses Vulkan
      which is already included with your graphics drivers.

================================================================================
                        COMMAND LINE OPTIONS
================================================================================

For advanced users, you can run from command prompt:

  vixen_benchmark.exe                  # Run full benchmark suite
  vixen_benchmark.exe --quick          # Quick test (3 configurations only)
  vixen_benchmark.exe --tester "Name"  # Add your name to results
  vixen_benchmark.exe --headless       # Run without preview window
  vixen_benchmark.exe --help           # Show all options

================================================================================
                          WHAT GETS TESTED
================================================================================

The benchmark runs multiple rendering configurations:
1. Compute shader ray marching (software ray tracing)
2. Fragment shader ray marching (traditional graphics pipeline)
3. Hardware ray tracing (if your GPU supports it)

Each test uses different:
- Voxel resolutions (64, 128, 256)
- Screen resolutions (720p, 1080p)
- Scene types (cornell box, noise, tunnels, cityscape)

================================================================================
                         TROUBLESHOOTING
================================================================================

PROBLEM: "Vulkan not found" or similar error
SOLUTION: Update your graphics drivers from:
  - NVIDIA: https://www.nvidia.com/drivers
  - AMD: https://www.amd.com/support
  - Intel: https://www.intel.com/content/www/us/en/download-center

PROBLEM: Benchmark window is black or frozen
SOLUTION: Try running with --headless flag:
  vixen_benchmark.exe --headless

PROBLEM: "Ray tracing not supported" message
SOLUTION: This is normal! Hardware RT tests will be skipped. The benchmark
          will still run compute and fragment shader tests.

PROBLEM: Benchmark crashes on startup
SOLUTION:
  1. Make sure all DLL files are in the same folder as the .exe
  2. Try running as Administrator
  3. Temporarily disable antivirus software

PROBLEM: Results folder doesn't open
SOLUTION: Manually check: Downloads/VIXEN_Benchmarks/
          The ZIP file should be there.

================================================================================
                          OUTPUT FILES
================================================================================

After running, you'll find a ZIP file in your Downloads folder:
  VIXEN_benchmark_YYYYMMDD_HHMMSS_<GPU>.zip

The ZIP contains:
  - benchmark_results.json  = Detailed per-frame performance data
  - system_info.json        = Your GPU and system information
  - debug_images/           = Screenshots (if any were captured)

================================================================================
                             PRIVACY
================================================================================

The benchmark collects:
  - GPU model and driver version
  - Vulkan capabilities
  - Performance metrics (frame times, memory usage)
  - Computer hostname (for identifying duplicate submissions)

It does NOT collect:
  - Personal information
  - Files from your computer
  - Network information
  - Any data outside the benchmark

================================================================================
                              SUPPORT
================================================================================

Questions or issues?
  - GitHub: https://github.com/VIXEN-Engine/VIXEN/issues
  - Contact the benchmark coordinator who sent you this package

================================================================================
