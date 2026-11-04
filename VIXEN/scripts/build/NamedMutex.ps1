# Purpose: wait on a machine-wide named Mutex, treating an ABANDONED mutex as acquired.
#
# When a holder is killed (the WSL queue's cancel path runs `taskkill /T /F` on the native
# process tree), the OS hands the mutex to the next waiter with System.Threading.
# AbandonedMutexException. The waiter DOES own the mutex at that point. Left uncaught, the
# exception aborts the WaitOne() statement, `$acquired` stays unset, the caller reports a lock
# timeout and exits 2, and the mutex is never released -- the next build behind it wedges the
# same way (T-4910).
#
# Usage (dot-source): . "$PSScriptRoot\NamedMutex.ps1"
#   $acquired = Wait-VixenMutex -Mutex $mutex -TimeoutSeconds N -Name 'Global\VixenBuildLock' -Tag '[build]'
# Returns $true when the caller now owns the mutex (including after an abandon), $false on timeout.
function Wait-VixenMutex {
    param(
        [Parameter(Mandatory=$true)][System.Threading.Mutex]$Mutex,
        [Parameter(Mandatory=$true)][int]$TimeoutSeconds,
        [string]$Name = "",
        [string]$Tag = "[lock]"
    )
    try {
        return $Mutex.WaitOne([TimeSpan]::FromSeconds($TimeoutSeconds))
    } catch [System.Threading.AbandonedMutexException] {
        Write-Host "$Tag Previous holder of $Name died without releasing it (cancelled or killed build); taking over the abandoned lock."
        return $true
    }
}
