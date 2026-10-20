# Tests Save-VerifiedDownload from provision-windows-native.ps1 without running the provisioner.
# The function is pulled out of the script's AST so the top-level provisioning steps do not run.
# Run with: powershell.exe -NoProfile -ExecutionPolicy Bypass -File <this file>

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$ProvisionScript = Join-Path $PSScriptRoot 'provision-windows-native.ps1'
$tokens = $null
$parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile($ProvisionScript, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors) { throw "Parse errors in ${ProvisionScript}: $($parseErrors[0].Message)" }
$definition = $ast.Find({
    param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Save-VerifiedDownload'
}, $true)
if (-not $definition) { throw 'Save-VerifiedDownload was not found in the provisioning script' }
. ([scriptblock]::Create($definition.Extent.Text))

$Work = Join-Path ([System.IO.Path]::GetTempPath()) ("vprov-test-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $Work | Out-Null
try {
    $Final = Join-Path $Work 'vulkan_sdk.exe'
    $Missing = Join-Path $Work 'no-such-source.exe'
    $Url = ([System.Uri]$Missing).AbsoluteUri

    $failed = $false
    try {
        Save-VerifiedDownload -Url $Url -Path $Final -ExpectedSha ('0' * 64)
    } catch {
        $failed = $true
        Write-Host "expected failure: $($_.Exception.Message)"
    }
    if (-not $failed) { throw 'FAIL: download from a missing source did not throw' }
    if (Test-Path -LiteralPath $Final) { throw 'FAIL: a failed download left a file at the final name' }

    $partials = @(Get-ChildItem -LiteralPath $Work -Filter '*.partial' -Force)
    if ($partials.Count -gt 0) { throw "FAIL: partial file left behind: $($partials[0].Name)" }

    $lockProbe = [System.IO.File]::Open("$Final.lock", 'OpenOrCreate', 'ReadWrite', 'None')
    $lockProbe.Dispose()

    # An interrupted transfer is the defect case: the server promises 1000 bytes, sends 100, and closes.
    $Port = Get-Random -Minimum 20000 -Maximum 60000
    $server = Start-Job -ArgumentList $Port -ScriptBlock {
        param($Port)
        $listener = [System.Net.HttpListener]::new()
        $listener.Prefixes.Add("http://localhost:$Port/")
        $listener.Start()
        'ready'
        $context = $listener.GetContext()
        $context.Response.ContentLength64 = 1000
        $bytes = [byte[]]::new(100)
        $context.Response.OutputStream.Write($bytes, 0, $bytes.Length)
        $context.Response.OutputStream.Close()
        $listener.Stop()
    }
    $ready = $false
    $readyDeadline = (Get-Date).AddSeconds(60)
    while (-not $ready -and (Get-Date) -lt $readyDeadline) {
        Start-Sleep -Milliseconds 500
        $ready = @(Receive-Job $server -Keep) -contains 'ready'
    }
    if (-not $ready) { Remove-Job $server -Force; throw 'FAIL: local test server never started listening' }
    $TruncatedFinal = Join-Path $Work 'truncated_sdk.exe'
    $truncatedFailed = $false
    try {
        Save-VerifiedDownload -Url "http://localhost:$Port/sdk.exe" -Path $TruncatedFinal -ExpectedSha ('0' * 64)
    } catch {
        $truncatedFailed = $true
        Write-Host "expected interrupted-download failure: $($_.Exception.Message)"
    } finally {
        Wait-Job $server -Timeout 30 | Out-Null
        Remove-Job $server -Force
    }
    if (-not $truncatedFailed) { throw 'FAIL: truncated download did not throw' }
    if (Test-Path -LiteralPath $TruncatedFinal) { throw 'FAIL: interrupted download left a partial file at the final name' }
    $partials = @(Get-ChildItem -LiteralPath $Work -Filter '*.partial' -Force)
    if ($partials.Count -gt 0) { throw "FAIL: partial file left behind after interruption: $($partials[0].Name)" }

    Write-Host 'PASS: failed and interrupted downloads left nothing at the final name and released the lock'
} finally {
    Remove-Item -LiteralPath $Work -Recurse -Force -ErrorAction SilentlyContinue
}
