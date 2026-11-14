# Purpose: tell the WSL launcher (tools/vixen-native.sh) which Windows process tree to kill if the
# queue cancels the job (T-4910). The launcher exports VIXEN_NATIVE_PIDFILE (a path forwarded
# through WSLENV); this script walks up from the current process through cmd.exe/powershell.exe
# ancestors to the top of the tree that the launcher's `cmd.exe /c build.bat` started, and writes
# that PID there. The launcher then runs `taskkill /PID <pid> /T /F`, which also takes down cmake,
# ninja and cl.exe and releases the Global\Vixen* mutexes (a following build takes the abandoned
# mutex over, see NamedMutex.ps1).
#
# The same root PID is also written to UT_QUEUE_WINROOT_FILE when the queue wrapper set it (forwarded through WSLENV as a path): that is
# the job's registration with the box-queue host sampler, which measures this Windows tree's CPU for the job's tokens (sn-wincpu1). The
# queue file is written even when the launcher passed no VIXEN_NATIVE_PIDFILE.
#
# Usage (dot-source): . "$PSScriptRoot\Register-NativeRunRoot.ps1"
# No-op when VIXEN_NATIVE_PIDFILE is unset (a hand-run build.bat). Never fails the build.
function Get-NativeRunRootPid {
    $treeNames = @('cmd.exe', 'powershell.exe', 'pwsh.exe')
    $rootPid = $PID
    while ($true) {
        $self = Get-CimInstance Win32_Process -Filter "ProcessId=$rootPid" -ErrorAction SilentlyContinue
        if (-not $self) { break }
        $parent = Get-CimInstance Win32_Process -Filter "ProcessId=$($self.ParentProcessId)" -ErrorAction SilentlyContinue
        if (-not $parent -or $treeNames -notcontains $parent.Name.ToLowerInvariant()) { break }
        $rootPid = $parent.ProcessId
    }
    return $rootPid
}

function Write-NativeRunRoot([string]$Path, [int]$RootPid) {
    $tmp = "$Path.tmp"
    Set-Content -Path $tmp -Value $RootPid -Encoding ascii
    Move-Item -Path $tmp -Destination $Path -Force
}
function Register-NativeRunRoot {
    if (-not $env:VIXEN_NATIVE_PIDFILE -and -not $env:UT_QUEUE_WINROOT_FILE) { return }
    try {
        $rootPid = Get-NativeRunRootPid
        if ($env:VIXEN_NATIVE_PIDFILE) {
            Write-NativeRunRoot $env:VIXEN_NATIVE_PIDFILE $rootPid
            Write-Host "[native] Registered process tree root PID $rootPid for queue cancellation."
        }
        if ($env:UT_QUEUE_WINROOT_FILE) {
            Write-NativeRunRoot $env:UT_QUEUE_WINROOT_FILE $rootPid
            Write-Host "[native] Registered process tree root PID $rootPid for queue CPU attribution."
        }
    } catch {
        Write-Host "[native] WARNING: could not register the process tree ($_). A queue cancel will not stop this build and the queue cannot attribute its CPU; see tools/vixen-native.sh."
    }
}
Register-NativeRunRoot
