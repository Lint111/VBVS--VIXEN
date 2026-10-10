# Provision the Windows-native toolchain before CMake is available. This is called by
# the tracked repository build.bat entry point so native provisioning stays in VIXEN's
# existing build/provisioning flow.

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$VixenRoot = Split-Path -Parent $PSScriptRoot
$RepoRoot = Split-Path -Parent $VixenRoot
$ToolRoot = Join-Path $VixenRoot '.win-native-toolchain'
$DownloadRoot = Join-Path $ToolRoot 'downloads'
$LogRoot = Join-Path $ToolRoot 'logs'
$NinjaVersion = '1.13.1'
$CMakeVersion = '4.2.3'
$VisualStudioPackageVersion = '17.14.41'
$WindowsSdkVersion = '10.0.26100.0'
$GitVersion = '2.55.0.5'
$PythonVersion = '3.13.15'
$NinjaInstall = Join-Path $ToolRoot "ninja\$NinjaVersion"
$VulkanSettings = Join-Path $VixenRoot 'cmake\vulkan-sdk-settings.env'
if (-not (Test-Path $VulkanSettings)) {
    throw "Could not read the shared Vulkan settings file: $VulkanSettings"
}
$VulkanSettingsText = Get-Content -Raw $VulkanSettings
$WindowsDefaultMatch = [regex]::Match($VulkanSettingsText, '(?m)^VIXEN_VULKAN_SDK_VERSION_WINDOWS_DEFAULT=([0-9.]+)$')
if (-not $WindowsDefaultMatch.Success) {
    throw "Could not read the Windows Vulkan SDK default from $VulkanSettings"
}
$VulkanDefaultVersion = $WindowsDefaultMatch.Groups[1].Value
$MinimumApiMatch = [regex]::Match($VulkanSettingsText, '(?m)^VIXEN_VULKAN_MIN_API_VERSION=([0-9.]+)$')
$Synchronization2CoreMatch = [regex]::Match($VulkanSettingsText, '(?m)^VIXEN_VULKAN_SYNCHRONIZATION2_CORE_VERSION=([0-9.]+)$')
$MinimumSpirvMatch = [regex]::Match($VulkanSettingsText, '(?m)^VIXEN_VULKAN_MIN_SPIRV_TARGET_VERSION=([0-9.]+)$')
if (-not $MinimumApiMatch.Success -or -not $Synchronization2CoreMatch.Success -or -not $MinimumSpirvMatch.Success) {
    throw "Could not read Vulkan API/SPIR-V capability floors from $VulkanSettings"
}
$MinimumVulkanApiVersion = $MinimumApiMatch.Groups[1].Value
$Synchronization2CoreVersion = $Synchronization2CoreMatch.Groups[1].Value
$MinimumSpirvTargetVersion = $MinimumSpirvMatch.Groups[1].Value
$VulkanVersion = if ($env:VIXEN_VULKAN_SDK_VERSION) {
    $env:VIXEN_VULKAN_SDK_VERSION.Trim()
} else {
    $VulkanDefaultVersion
}

New-Item -ItemType Directory -Path $DownloadRoot, $LogRoot -Force | Out-Null
$LogPath = Join-Path $LogRoot ("provision-{0}.log" -f (Get-Date -Format 'yyyyMMdd-HHmmss'))
Start-Transcript -Path $LogPath -Append | Out-Null

function Write-Step([string]$Message) {
    Write-Host "[provision] $Message"
}

function Invoke-Native([string]$FilePath, [string[]]$Arguments) {
    Write-Step ("RUN: {0} {1}" -f $FilePath, ($Arguments -join ' '))
    $previousErrorAction = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $FilePath @Arguments 2>&1 | ForEach-Object { Write-Host $_ }
        $commandExitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousErrorAction
    }
    if ($commandExitCode -ne 0) {
        throw "Command failed with exit code ${commandExitCode}: $FilePath"
    }
}

function Test-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Require-Administrator([string]$ToolName) {
    if (Test-Administrator) { return $true }
    Write-Step "$ToolName requires elevation. No installer was started."
    Write-Host "OWNER ACTION: In an elevated Windows PowerShell, run: & '$PSCommandPath'"
    return $false
}

function Get-ToolCommand([string]$Name) {
    $command = Get-Command $Name -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command) { return $command.Source }
    return $null
}

function Test-Version([string]$Actual, [string]$Expected) {
    if (-not $Actual) { return $false }
    $normalized = $Actual.Trim() -replace '\.windows\.', '.'
    return $normalized -eq $Expected
}

function Get-VisualStudioStatus {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { return $null }
    $versionRange = "[$VisualStudioPackageVersion,$VisualStudioPackageVersion]"
    $installPath = & $vswhere -latest -products * -version $versionRange `
        -requires Microsoft.VisualStudio.Workload.NativeDesktop `
                  Microsoft.VisualStudio.Component.Windows11SDK.26100 `
        -property installationPath 2>$null
    if ($LASTEXITCODE -ne 0 -or -not $installPath) { return $null }
    return ($installPath | Select-Object -First 1).Trim()
}

function Get-CMakeCommand {
    $fallback = Join-Path $env:ProgramFiles 'CMake\bin\cmake.exe'
    $candidates = @($fallback, (Get-ToolCommand 'cmake.exe')) |
        Where-Object { $_ } | Select-Object -Unique
    foreach ($candidate in $candidates) {
        if (-not (Test-Path $candidate)) { continue }
        $line = & $candidate --version 2>$null | Select-Object -First 1
        if ($line -match '^cmake version (.+)$' -and (Test-Version $Matches[1] $CMakeVersion)) {
            return $candidate
        }
    }
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) { return $candidate }
    }
    return $null
}

function Get-NinjaCommand {
    $local = Join-Path $NinjaInstall 'ninja.exe'
    if (Test-Path $local) { return $local }
    return Get-ToolCommand 'ninja.exe'
}

function Get-PythonCommand {
    $local = Join-Path $env:LOCALAPPDATA 'Programs\Python\Python313\python.exe'
    $candidate = Get-ToolCommand 'python.exe'
    $candidates = @($local, $candidate) |
        Where-Object { $_ -and $_ -notmatch '\\Microsoft\\WindowsApps\\' } | Select-Object -Unique
    foreach ($path in $candidates) {
        if (-not (Test-Path $path)) { continue }
        $versionLine = (& $path --version 2>&1 | Out-String).Trim()
        if ($versionLine -match '^Python\s+(.+)$' -and (Test-Version $Matches[1] $PythonVersion)) {
            return $path
        }
    }
    foreach ($path in $candidates) {
        if (Test-Path $path) { return $path }
    }
    return $null
}

function Get-GitStatus {
    $candidates = @(
        (Join-Path $env:ProgramFiles 'Git\cmd\git.exe'),
        (Join-Path $env:LOCALAPPDATA 'Programs\Git\cmd\git.exe'),
        (Get-ToolCommand 'git.exe')
    ) | Where-Object { $_ } | Select-Object -Unique
    $firstFound = $null
    foreach ($candidate in $candidates) {
        if (-not (Test-Path $candidate)) { continue }
        $versionLine = & $candidate --version 2>$null
        if ($LASTEXITCODE -ne 0 -or $versionLine -notmatch '^git version (.+)$') { continue }
        $status = [pscustomobject]@{ Path = $candidate; Version = $Matches[1] }
        if (Test-Version $status.Version $GitVersion) { return $status }
        if (-not $firstFound) { $firstFound = $status }
    }
    return $firstFound
}

function Get-VulkanSdkRoot {
    $candidateRoots = @()
    $candidateRoots += "${env:SystemDrive}\VulkanSDK\$VulkanVersion"
    $candidateRoots += (Join-Path $env:ProgramFiles "VulkanSDK\$VulkanVersion")
    foreach ($parent in @("${env:SystemDrive}\VulkanSDK", (Join-Path $env:ProgramFiles 'VulkanSDK'))) {
        if (Test-Path $parent) {
            $candidateRoots += Get-ChildItem -LiteralPath $parent -Directory -ErrorAction SilentlyContinue |
                ForEach-Object { $_.FullName }
        }
    }
    if ($env:VULKAN_SDK) { $candidateRoots += $env:VULKAN_SDK }
    foreach ($candidate in ($candidateRoots | Select-Object -Unique)) {
        if ((Test-Path (Join-Path $candidate 'Include\vulkan\vulkan.h')) -and
            (Test-Path (Join-Path $candidate 'Bin\glslc.exe'))) {
            return $candidate
        }
    }
    return $null
}

function Get-VulkanSdkVersion([string]$Root) {
    if (-not $Root) { return '' }
    $trimChars = [char[]]@('\', '/')
    $leaf = Split-Path -Leaf $Root.TrimEnd($trimChars)
    if ($leaf -match '^\d+\.\d+\.\d+\.\d+$') { return $leaf }
    foreach ($metadataName in @('version.txt', 'version')) {
        $metadataPath = Join-Path $Root $metadataName
        if (-not (Test-Path $metadataPath)) { continue }
        $metadataText = Get-Content -Raw $metadataPath
        $versionMatch = [regex]::Match($metadataText, '\b(\d+\.\d+\.\d+\.\d+)\b')
        if ($versionMatch.Success) { return $versionMatch.Groups[1].Value }
    }
    return ''
}

function Test-VulkanSdkReleaseCompatible([string]$Actual, [string]$Expected) {
    if (-not $Actual -or -not $Expected) { return $false }
    $actualParts = $Actual.Split('.')
    $expectedParts = $Expected.Split('.')
    if ($actualParts.Count -ne 4 -or $expectedParts.Count -ne 4) { return $false }
    return ($actualParts[0] -eq $expectedParts[0] -and
            $actualParts[1] -eq $expectedParts[1] -and
            $actualParts[2] -eq $expectedParts[2])
}

function Get-Inventory {
    $git = Get-GitStatus
    $python = Get-PythonCommand
    $ninja = Get-NinjaCommand
    $cmake = Get-CMakeCommand
    $vsPath = Get-VisualStudioStatus
    $vulkanRoot = Get-VulkanSdkRoot

    $gitActual = if ($git) { $git.Version } else { '' }
    $pythonActual = if ($python) { ((& $python --version 2>&1) -replace '^Python\s+', '').Trim() } else { '' }
    $ninjaActual = if ($ninja) { ((& $ninja --version 2>&1) | Out-String).Trim() } else { '' }
    $cmakeActual = if ($cmake) {
        $line = & $cmake --version 2>$null | Select-Object -First 1
        if ($line -match '^cmake version (.+)$') { $Matches[1] } else { '' }
    } else { '' }
    $vulkanActual = if ($vulkanRoot) { Get-VulkanSdkVersion $vulkanRoot } else { '' }
    $vulkanPresent = $vulkanRoot -and (Test-VulkanSdkReleaseCompatible $vulkanActual $VulkanVersion)
    $vulkanStatus = if ($vulkanPresent) { 'present' } else { 'missing' }
    $vulkanActualDetail = if ($vulkanActual) { $vulkanActual } else { 'unknown version' }
    if ($vulkanRoot -and $vulkanActual -ne $VulkanVersion) {
        if ($vulkanPresent) {
            $vulkanActualDetail = "$vulkanActual at $vulkanRoot (patch variation accepted; expected $VulkanVersion)"
        } else {
            $vulkanActualDetail = "$vulkanActualDetail at $vulkanRoot (expected $VulkanVersion)"
        }
    } elseif ($vulkanRoot) {
        $vulkanActualDetail = "$vulkanActual at $vulkanRoot"
    }

    return @(
        [pscustomobject]@{ Tool='Git for Windows'; Version=$GitVersion; Method='winget Git.Git'; Status=$(if (Test-Version $gitActual $GitVersion) {'present'} else {'missing'}); Actual=$gitActual; Path=$(if ($git) {$git.Path} else {''}) },
        [pscustomobject]@{ Tool='Python'; Version=$PythonVersion; Method='winget Python.Python.3.13'; Status=$(if (Test-Version $pythonActual $PythonVersion) {'present'} else {'missing'}); Actual=$pythonActual; Path=$python },
        [pscustomobject]@{ Tool='Ninja'; Version=$NinjaVersion; Method='winget Ninja-build.Ninja'; Status=$(if (Test-Version $ninjaActual $NinjaVersion) {'present'} else {'missing'}); Actual=$ninjaActual; Path=$ninja },
        [pscustomobject]@{ Tool='CMake'; Version=$CMakeVersion; Method='winget Kitware.CMake'; Status=$(if (Test-Version $cmakeActual $CMakeVersion) {'present'} else {'missing'}); Actual=$cmakeActual; Path=$cmake },
        [pscustomobject]@{ Tool='Visual Studio 2022 Build Tools + Windows SDK'; Version="$VisualStudioPackageVersion / $WindowsSdkVersion"; Method='winget Microsoft.VisualStudio.2022.BuildTools'; Status=$(if ($vsPath) {'present'} else {'missing'}); Actual=$(if ($vsPath) {$vsPath} else {''}); Path=$vsPath },
        [pscustomobject]@{ Tool='Vulkan SDK'; Version=$VulkanVersion; Method='LunarG official installer'; Status=$vulkanStatus; Actual=$(if ($vulkanRoot) {$vulkanActualDetail} else {''}); Path=$vulkanRoot }
    )
}

function Write-Inventory($Inventory) {
    Write-Step 'Tool inventory (all tools are checked before any installation starts):'
    foreach ($tool in $Inventory) {
        $detail = if ($tool.Actual) { "; found $($tool.Actual)" } else { '' }
        Write-Host ("  {0}: {1} {2} ({3}){4}" -f $tool.Tool, $tool.Version, $tool.Status, $tool.Method, $detail)
    }
}

function Get-WingetInstaller([string]$Id, [string]$Version, [string]$Extension) {
    $winget = Get-ToolCommand 'winget.exe'
    if (-not $winget) { throw 'winget.exe is unavailable; install the package through its official installer before provisioning.' }
    $packageDir = Join-Path $DownloadRoot (Join-Path $Id $Version)
    New-Item -ItemType Directory -Path $packageDir -Force | Out-Null
    $cached = Get-ChildItem -Path $packageDir -Recurse -File -Filter "*$Extension" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($cached) {
        Write-Step "Using cached $Id $Version installer: $($cached.FullName)"
        return $cached.FullName
    }
    Invoke-Native $winget @('download', '--exact', '--id', $Id, '--version', $Version,
        '--architecture', 'x64', '--download-directory', $packageDir,
        '--accept-source-agreements', '--skip-dependencies')
    $installer = Get-ChildItem -Path $packageDir -Recurse -File -Filter "*$Extension" | Select-Object -First 1
    if (-not $installer) { throw "winget did not cache an $Extension installer for $Id $Version in $packageDir" }
    return $installer.FullName
}

function Install-Python($Inventory) {
    $pythonRow = $Inventory | Where-Object Tool -eq 'Python'
    if ($pythonRow.Status -eq 'present') { Write-Step "Python $PythonVersion is present; skipping."; return }
    $installer = Get-WingetInstaller 'Python.Python.3.13' $PythonVersion '.exe'
    $target = Join-Path $env:LOCALAPPDATA 'Programs\Python\Python313'
    Invoke-Native $installer @('/quiet', 'InstallAllUsers=0', "TargetDir=$target", 'PrependPath=0', 'Include_launcher=0', 'Include_test=0')
}

function Install-Ninja($Inventory) {
    $ninjaRow = $Inventory | Where-Object Tool -eq 'Ninja'
    if ($ninjaRow.Status -eq 'present') { Write-Step "Ninja $NinjaVersion is present; skipping."; return }
    $archive = Get-WingetInstaller 'Ninja-build.Ninja' $NinjaVersion '.zip'
    New-Item -ItemType Directory -Path $NinjaInstall -Force | Out-Null
    Expand-Archive -LiteralPath $archive -DestinationPath $NinjaInstall -Force
    $ninjaExe = Get-ChildItem -Path $NinjaInstall -Recurse -File -Filter ninja.exe | Select-Object -First 1
    if (-not $ninjaExe) { throw "The cached Ninja archive did not contain ninja.exe: $archive" }
    if ($ninjaExe.DirectoryName -ne $NinjaInstall) {
        Copy-Item -LiteralPath $ninjaExe.FullName -Destination (Join-Path $NinjaInstall 'ninja.exe') -Force
    }
    Set-Content -Path (Join-Path $ToolRoot 'ninja-bin.txt') -Value $NinjaInstall -Encoding ascii
    Write-Step "Installed Ninja $NinjaVersion into the project-local tool cache."
}

function Install-Git($Inventory) {
    $gitRow = $Inventory | Where-Object Tool -eq 'Git for Windows'
    if ($gitRow.Status -eq 'present') { Write-Step "Git for Windows $GitVersion is present; skipping."; return $true }
    $installer = Get-WingetInstaller 'Git.Git' $GitVersion '.exe'
    if (-not (Require-Administrator 'Git for Windows')) { return $false }
    Invoke-Native $installer @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-')
    return $true
}

function Install-CMake($Inventory) {
    $cmakeRow = $Inventory | Where-Object Tool -eq 'CMake'
    if ($cmakeRow.Status -eq 'present') { Write-Step "CMake $CMakeVersion is present; skipping."; return $true }
    $installer = Get-WingetInstaller 'Kitware.CMake' $CMakeVersion '.msi'
    if (-not (Require-Administrator 'CMake')) { return $false }
    Invoke-Native (Join-Path $env:SystemRoot 'System32\msiexec.exe') @('/i', $installer, '/qn', '/norestart', 'ADD_CMAKE_TO_PATH=0')
    return $true
}

function Install-VisualStudio($Inventory) {
    $vsRow = $Inventory | Where-Object Tool -eq 'Visual Studio 2022 Build Tools + Windows SDK'
    if ($vsRow.Status -eq 'present') { Write-Step 'The pinned VS Build Tools workload and Windows SDK are present; skipping.'; return $true }
    $installer = Get-WingetInstaller 'Microsoft.VisualStudio.2022.BuildTools' $VisualStudioPackageVersion '.exe'
    if (-not (Require-Administrator 'Visual Studio 2022 Build Tools')) { return $false }
    Invoke-Native $installer @('--quiet', '--wait', '--norestart',
        '--add', 'Microsoft.VisualStudio.Workload.NativeDesktop',
        '--add', 'Microsoft.VisualStudio.Component.Windows11SDK.26100',
        '--includeRecommended')
    return $true
}

function Install-Vulkan($Inventory) {
    $vulkanRow = $Inventory | Where-Object Tool -eq 'Vulkan SDK'
    if ($vulkanRow.Status -eq 'present') { Write-Step "Vulkan SDK $($vulkanRow.Actual) is compatible with expected $VulkanVersion; skipping."; return $true }
    if ($vulkanRow.Path) { Write-Step "Replacing incompatible Vulkan SDK $($vulkanRow.Actual) with declared version $VulkanVersion." }
    if (-not (Require-Administrator "Vulkan SDK $VulkanVersion")) { return $false }

    $versions = Invoke-RestMethod 'https://vulkan.lunarg.com/sdk/versions/windows.json'
    if ($versions -notcontains $VulkanVersion) {
        $sameRelease = $VulkanVersion -replace '\.\d+$', '.0'
        Write-Step "STOP: LunarG does not publish Windows Vulkan SDK $VulkanVersion."
        Write-Step "The WSL pin is $VulkanVersion; Windows publishes $sameRelease for this API release."
        Write-Step 'Owner decision required: approve this Windows-specific SDK version or select a shared cross-platform pin.'
        return $false
    }

    $metadataUrl = "https://sdk.lunarg.com/sdk/sha/$VulkanVersion/windows/vulkan_sdk.exe.json"
    $metadata = Invoke-RestMethod $metadataUrl
    if (-not $metadata.sha) { throw "LunarG returned no SHA-256 at $metadataUrl" }
    $installerUrl = "https://sdk.lunarg.com/sdk/download/$VulkanVersion/windows/vulkan_sdk.exe"
    $installerDir = Join-Path $DownloadRoot "LunarG.VulkanSDK\$VulkanVersion"
    New-Item -ItemType Directory -Path $installerDir -Force | Out-Null
    $installer = Join-Path $installerDir 'vulkan_sdk.exe'
    if (Test-Path $installer) {
        $actualHash = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actualHash -ne $metadata.sha.ToLowerInvariant()) {
            Remove-Item -LiteralPath $installer -Force
        }
    }
    if (-not (Test-Path $installer)) {
        Write-Step "Downloading LunarG Vulkan SDK $VulkanVersion to the project cache."
        Invoke-WebRequest -Uri $installerUrl -OutFile $installer
        $actualHash = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actualHash -ne $metadata.sha.ToLowerInvariant()) {
            Remove-Item -LiteralPath $installer -Force
            throw "LunarG Vulkan SDK download hash mismatch. Expected $($metadata.sha), got $actualHash"
        }
    }
    if (-not (Require-Administrator "Vulkan SDK $VulkanVersion")) { return $false }
    $sdkRoot = "${env:SystemDrive}\VulkanSDK\$VulkanVersion"
    Invoke-Native $installer @('--root', $sdkRoot, '--accept-licenses', '--default-answer', '--confirm-command', 'install')
    Set-Content -Path (Join-Path $ToolRoot 'vulkan-sdk-root.txt') -Value $sdkRoot -Encoding ascii
    return $true
}

$exitCode = 0
try {
    Write-Step "Starting Windows-native provisioning from $VixenRoot"
    Write-Step "Declared Vulkan setting: VIXEN_VULKAN_SDK_VERSION=$VulkanVersion (Windows default $VulkanDefaultVersion; source $VulkanSettings)."
    Write-Step "Capability floors: Vulkan API $MinimumVulkanApiVersion (synchronization2 is core from $Synchronization2CoreVersion or uses VK_KHR_synchronization2 below it); SPIR-V target $MinimumSpirvTargetVersion (ray-query has a capability-independent twin)."
    Write-Step "Pinned tools: VS $VisualStudioPackageVersion with SDK $WindowsSdkVersion; CMake $CMakeVersion; Ninja $NinjaVersion; Git $GitVersion; Python $PythonVersion."
    $inventory = Get-Inventory
    Write-Inventory $inventory

    if (-not (Install-Git $inventory)) { $exitCode = 20; throw 'Provisioning paused for elevation; rerun the same entry point as administrator.' }
    $inventory = Get-Inventory
    Install-Python $inventory
    $inventory = Get-Inventory
    Install-Ninja $inventory
    $inventory = Get-Inventory
    if (-not (Install-CMake $inventory)) { $exitCode = 20; throw 'Provisioning paused for elevation; rerun the same entry point as administrator.' }
    $inventory = Get-Inventory
    if (-not (Install-VisualStudio $inventory)) { $exitCode = 20; throw 'Provisioning paused for elevation; rerun the same entry point as administrator.' }
    $inventory = Get-Inventory
    if (-not (Install-Vulkan $inventory)) { $exitCode = 30; throw 'Provisioning stopped because the Windows Vulkan SDK version needs an owner decision.' }

    $inventory = Get-Inventory
    Write-Inventory $inventory
    $vulkanRow = $inventory | Where-Object Tool -eq 'Vulkan SDK'
    if ($vulkanRow.Status -eq 'present' -and $vulkanRow.Path) {
        Set-Content -Path (Join-Path $ToolRoot 'vulkan-sdk-root.txt') -Value $vulkanRow.Path -Encoding ascii
    }
    $missing = @($inventory | Where-Object Status -ne 'present')
    if ($missing.Count -gt 0) {
        throw "Provisioning completed with missing tools: $($missing.Tool -join ', ')"
    }
    foreach ($toolName in @('Git for Windows', 'Python', 'Ninja')) {
        $toolRow = $inventory | Where-Object Tool -eq $toolName
        if ($toolRow.Path) {
            $pathFile = Join-Path $ToolRoot ("{0}-bin.txt" -f ($toolName -replace ' for Windows', '').ToLowerInvariant())
            Set-Content -Path $pathFile -Value (Split-Path -Parent $toolRow.Path) -Encoding ascii
        }
    }
    $cmakeRow = $inventory | Where-Object Tool -eq 'CMake'
    if ($cmakeRow.Path) {
        Set-Content -Path (Join-Path $ToolRoot 'cmake-exe.txt') -Value $cmakeRow.Path -Encoding ascii
    }
    Write-Step 'All pinned Windows-native tools are present; no further installation is needed.'
} catch {
    if ($exitCode -eq 0) { $exitCode = 1 }
    Write-Host "[provision] $($_.Exception.Message)"
} finally {
    Stop-Transcript | Out-Null
}

exit $exitCode
