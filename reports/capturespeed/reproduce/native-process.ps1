# Run a native process with a liveness heartbeat and a Windows job object that owns its descendants.
#
# Heartbeat (T-4916): a driver call such as pipeline creation can be silent and nearly CPU-free for
# minutes, which the queue's idle watchdog reads as a stall while CTest's own timeout is far longer.
# A periodic line says what is running and for how long, without touching the CTest timeout.
#
# Job object (T-4917): the process and everything it spawns join a job that kills them when the job
# handle closes, i.e. when this script exits or is itself terminated. A cancelled queue job therefore
# cannot leave vixen_lookdev_capture.exe running (and holding the GPU) after the wrapper is gone.
if (-not ('VixenNativeJob' -as [type])) {
  Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class VixenNativeJob {
  [StructLayout(LayoutKind.Sequential)] struct BasicLimit {
    public long PerProcessUserTimeLimit, PerJobUserTimeLimit; public uint LimitFlags;
    public UIntPtr MinimumWorkingSetSize, MaximumWorkingSetSize; public uint ActiveProcessLimit;
    public UIntPtr Affinity; public uint PriorityClass, SchedulingClass;
  }
  [StructLayout(LayoutKind.Sequential)] struct IoCounters {
    public ulong ReadOps, WriteOps, OtherOps, ReadBytes, WriteBytes, OtherBytes;
  }
  [StructLayout(LayoutKind.Sequential)] struct ExtendedLimit {
    public BasicLimit Basic; public IoCounters Io;
    public UIntPtr ProcessMemoryLimit, JobMemoryLimit, PeakProcessMemoryUsed, PeakJobMemoryUsed;
  }
  [DllImport("kernel32.dll", SetLastError = true)] static extern IntPtr CreateJobObject(IntPtr attrs, string name);
  [DllImport("kernel32.dll", SetLastError = true)] static extern bool SetInformationJobObject(IntPtr job, int infoClass, ref ExtendedLimit info, int size);
  [DllImport("kernel32.dll", SetLastError = true)] static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);
  [DllImport("kernel32.dll", SetLastError = true)] static extern bool TerminateJobObject(IntPtr job, uint exitCode);
  public static IntPtr Create() {
    IntPtr job = CreateJobObject(IntPtr.Zero, null);
    if (job == IntPtr.Zero) throw new System.ComponentModel.Win32Exception();
    ExtendedLimit info = new ExtendedLimit();
    info.Basic.LimitFlags = 0x2000; // JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
    if (!SetInformationJobObject(job, 9, ref info, Marshal.SizeOf(typeof(ExtendedLimit))))
      throw new System.ComponentModel.Win32Exception();
    return job;
  }
  public static void Assign(IntPtr job, IntPtr process) {
    if (!AssignProcessToJobObject(job, process)) throw new System.ComponentModel.Win32Exception();
  }
  public static void Terminate(IntPtr job) { TerminateJobObject(job, 1); }
}
'@
}

# Returns the process exit code. Output is echoed and appended to $LogPath (replaced when it exists).
function Invoke-NativeJobProcess {
  param(
    [Parameter(Mandatory = $true)][string]$FilePath,
    [string[]]$ArgumentList = @(),
    [Parameter(Mandatory = $true)][string]$LogPath,
    [string]$Description = $FilePath,
    [int]$HeartbeatSeconds = 30
  )
  $startInfo = [Diagnostics.ProcessStartInfo]::new()
  $startInfo.FileName = $FilePath
  $startInfo.Arguments = ($ArgumentList | ForEach-Object {
    if ($_ -match '[\s"]') { '"' + ($_ -replace '"', '\"') + '"' } else { $_ }
  }) -join ' '
  $startInfo.UseShellExecute = $false
  $startInfo.RedirectStandardOutput = $true
  $startInfo.RedirectStandardError = $true
  $job = [VixenNativeJob]::Create()
  $process = [Diagnostics.Process]::new()
  $process.StartInfo = $startInfo
  try {
    if (-not $process.Start()) { throw "what: Windows did not start $Description; where: $FilePath; fix: check the path and the Windows error mode" }
    [VixenNativeJob]::Assign($job, $process.Handle)
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $lastOutput = $watch.Elapsed
    $nextBeat = $HeartbeatSeconds
    $readers = @($process.StandardOutput.ReadLineAsync(), $process.StandardError.ReadLineAsync())
    $streams = @($process.StandardOutput, $process.StandardError)
    $open = 2
    $exitedAt = $null
    $emit = {
      param([string]$line)
      Add-Content -LiteralPath $LogPath -Value $line
      [Console]::Out.WriteLine($line)
      [Console]::Out.Flush()
    }
    while ($open -gt 0) {
      $done = [Threading.Tasks.Task]::WaitAny([Threading.Tasks.Task[]]$readers, 1000)
      if ($done -ge 0) {
        $line = $readers[$done].Result
        if ($null -eq $line) {
          $open--
          # A finished stream gets a task that never completes, so WaitAny keeps waiting on the other.
          $readers[$done] = [Threading.Tasks.TaskCompletionSource[string]]::new().Task
        } else {
          & $emit $line
          $lastOutput = $watch.Elapsed
          $readers[$done] = $streams[$done].ReadLineAsync()
        }
      }
      # A descendant that inherited the pipes can hold them open after the process exited; give the
      # output a moment to drain, then stop waiting. The job object ends such a descendant at exit.
      if ($process.HasExited) {
        if ($null -eq $exitedAt) { $exitedAt = $watch.Elapsed }
        elseif (($watch.Elapsed - $exitedAt).TotalSeconds -ge 2) { break }
      }
      if ($watch.Elapsed.TotalSeconds -ge $nextBeat) {
        $quiet = [int]($watch.Elapsed - $lastOutput).TotalSeconds
        & $emit "[NATIVE_HEARTBEAT] $Description pid=$($process.Id) running $([int]$watch.Elapsed.TotalSeconds)s, no output for ${quiet}s"
        $nextBeat = [int]$watch.Elapsed.TotalSeconds + $HeartbeatSeconds
      }
    }
    $process.WaitForExit()
    return $process.ExitCode
  } catch {
    # Output pipe closed (the WSL wrapper was cancelled) or any other failure: do not leave the tree running.
    [VixenNativeJob]::Terminate($job)
    throw
  } finally {
    if ($process) { $process.Dispose() }
  }
}
