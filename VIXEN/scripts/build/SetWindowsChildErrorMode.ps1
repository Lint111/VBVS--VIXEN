if ($env:OS -eq 'Windows_NT') {
  if (-not ('VixenNativeErrorMode' -as [type])) {
    Add-Type -TypeDefinition @'
using System.Runtime.InteropServices;
public static class VixenNativeErrorMode {
  [DllImport("kernel32.dll", SetLastError = true)]
  public static extern uint SetErrorMode(uint mode);
}
'@
  }

  $previousErrorMode = [VixenNativeErrorMode]::SetErrorMode(0x0001 -bor 0x8000)
  Write-Output ("[NATIVE_ERROR_MODE] SEM_FAILCRITICALERRORS|SEM_NOOPENFILEERRORBOX enabled; previous=0x{0:X8}" -f $previousErrorMode)
}
