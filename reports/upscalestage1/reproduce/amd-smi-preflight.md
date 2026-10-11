# Native 8060S telemetry preflight

- Windows command found: `C:\Windows\System32\amd-smi.exe`.
- Query tried: `amd-smi.exe metric --clock --power --json`.
- Result: timed out after 15 seconds with empty stdout and stderr; the probe process was terminated. No AMD-SMI clock or power value was available from this query.
- The host has an AMD SMI executable, but this probe does not establish that its driver telemetry is unavailable through every interface. The paired runner also searches Windows Performance Counters for clock/frequency/power metrics and records any matching paths and samples.

AMD documents `amd-smi metric --clock` and `--power` as its GPU frequency and power metric options: [AMD SMI CLI reference](https://rocm.docs.amd.com/projects/amdsmi/en/latest/how-to/amdsmi-cli-tool.html).
