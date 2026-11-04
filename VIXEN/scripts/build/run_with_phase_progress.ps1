# Purpose: run ONE long synchronous native command (ctest, a capture, a pipeline-compile proof)
# and tell the WSL queue watchdog the truth about it. The watchdog (box-queue, UT_STALL_IDLE_HARD_S)
# only sees WSL-side output and CPU, so a Windows-native child that prints nothing for minutes looks
# dead even while it compiles pipelines. This wrapper prints a phase marker at start and end, and a
# "[phase:<name>] alive ..." line every -HeartbeatSeconds ONLY while the child's process tree
# accumulated CPU time since the previous sample. When the tree stops using CPU for
# -IdleGraceSeconds the heartbeat is suppressed and one "NO CPU PROGRESS" line is printed, so a
# genuinely hung payload goes quiet and the watchdog reports it (suspect / dead-tree) instead of
# being kept alive by this script. The child's own stdout/stderr pass through untouched.
#
# Usage: powershell -ExecutionPolicy Bypass -File run_with_phase_progress.ps1 -Phase <name>
#          [-HeartbeatSeconds 30] [-IdleGraceSeconds 120] -Exe <exe> [-ArgLine "<args>" | -ArgLineEnv <VAR>]
# The command's arguments travel as ONE string (-ArgLine) or in an environment variable
# (-ArgLineEnv, avoids cmd/PowerShell double quoting): `-File` mode would otherwise parse the
# command's own `--flags` as parameters of this script.
# Exit code: the child's exit code (2 when the command cannot be started).
param(
    [Parameter(Mandatory=$true)][string]$Phase,
    [int]$HeartbeatSeconds = 30,
    [int]$IdleGraceSeconds = 120,
    [Parameter(Mandatory=$true)][string]$Exe,
    [string]$ArgLine = "",
    [string]$ArgLineEnv = ""
)

$ErrorActionPreference = 'Continue'
if ($ArgLineEnv) { $ArgLine = [Environment]::GetEnvironmentVariable($ArgLineEnv) }

# Total CPU seconds of a process and all its descendants, plus how many were alive.
function Get-TreeCpu([int]$rootId) {
    $ids = New-Object System.Collections.Generic.List[int]
    $ids.Add($rootId)
    try {
        $all = Get-CimInstance Win32_Process -Property ProcessId, ParentProcessId -ErrorAction Stop
        $added = $true
        while ($added) {
            $added = $false
            foreach ($p in $all) {
                if ($ids.Contains([int]$p.ParentProcessId) -and -not $ids.Contains([int]$p.ProcessId)) {
                    $ids.Add([int]$p.ProcessId); $added = $true
                }
            }
        }
    } catch { }
    $cpu = 0.0; $alive = 0
    foreach ($id in $ids) {
        $proc = Get-Process -Id $id -ErrorAction SilentlyContinue
        if ($proc) {
            $alive++
            try { $cpu += $proc.TotalProcessorTime.TotalSeconds } catch { }
        }
    }
    return [pscustomobject]@{ Cpu = $cpu; Alive = $alive }
}

$start = Get-Date
Write-Host "[phase:$Phase] begin: $Exe $ArgLine"
try {
    $startArgs = @{ FilePath = $Exe; NoNewWindow = $true; PassThru = $true }
    if ($ArgLine) { $startArgs.ArgumentList = $ArgLine }
    $proc = Start-Process @startArgs
    $null = $proc.Handle   # cache the handle so ExitCode survives process exit
} catch {
    Write-Host "[phase:$Phase] ERROR: could not start '$Exe': $($_.Exception.Message). Check the executable is on PATH or give a full path."
    exit 2
}

$lastCpu = (Get-TreeCpu $proc.Id).Cpu
$lastGrowth = Get-Date
$suppressed = $false
while (-not $proc.WaitForExit($HeartbeatSeconds * 1000)) {
    $now = Get-Date
    $tree = Get-TreeCpu $proc.Id
    $elapsed = [int]($now - $start).TotalSeconds
    if ($tree.Cpu -gt $lastCpu) {
        $lastCpu = $tree.Cpu
        $lastGrowth = $now
        $suppressed = $false
    }
    $idle = [int]($now - $lastGrowth).TotalSeconds
    if ($idle -lt $IdleGraceSeconds) {
        Write-Host ("[phase:{0}] alive elapsed={1}s cpu={2:N1}s procs={3}" -f $Phase, $elapsed, $tree.Cpu, $tree.Alive)
    } elseif (-not $suppressed) {
        $suppressed = $true
        Write-Host ("[phase:{0}] NO CPU PROGRESS for {1}s (pid {2}, {3} process(es), cpu={4:N1}s); heartbeat stopped so the queue watchdog can report this payload as stalled" -f $Phase, $idle, $proc.Id, $tree.Alive, $tree.Cpu)
    }
}
$proc.WaitForExit()
$code = $proc.ExitCode
Write-Host ("[phase:{0}] end: exit={1} elapsed={2}s" -f $Phase, $code, [int]((Get-Date) - $start).TotalSeconds)
exit $code
