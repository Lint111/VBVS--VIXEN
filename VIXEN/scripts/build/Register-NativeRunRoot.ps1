# Purpose: tell the WSL launcher (tools/vixen-native.sh) which Windows process tree to kill if the
# queue cancels the job (T-4910). The launcher exports VIXEN_NATIVE_PIDFILE (a path forwarded
# through WSLENV); this script walks up from the current process through cmd.exe/powershell.exe
# ancestors to the top of the tree that the launcher's `cmd.exe /c build.bat` started, and writes
# that PID there. The launcher then runs `taskkill /PID <pid> /T /F`, which also takes down cmake,
# ninja and cl.exe and releases the Global\Vixen* mutexes (a following build takes the abandoned
# mutex over, see NamedMutex.ps1).
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

function Register-NativeRunRoot {
    if (-not $env:VIXEN_NATIVE_PIDFILE) { return }
    try {
        $rootPid = Get-NativeRunRootPid
        $tmp = "$($env:VIXEN_NATIVE_PIDFILE).tmp"
        Set-Content -Path $tmp -Value $rootPid -Encoding ascii
        Move-Item -Path $tmp -Destination $env:VIXEN_NATIVE_PIDFILE -Force
        Write-Host "[native] Registered process tree root PID $rootPid for queue cancellation."
    } catch {
        Write-Host "[native] WARNING: could not register the process tree for cancellation ($_). A queue cancel will not stop this build; see tools/vixen-native.sh."
    }
}
Register-NativeRunRoot
