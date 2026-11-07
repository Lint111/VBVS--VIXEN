# Cache-seed helpers for native-capture.ps1: copy a prior native build's runtime caches into a new build.
# The copy never edits a blob. VkPipelineCache blobs and the shared SPIR-V/glyph stores keep their own
# device, driver and ABI identity inside the file, and the runtime rejects an entry that does not match the
# GPU it runs on (CashSystem SharedPipelineCacheStore: envelope identity + pipelineCacheUUID header), so
# seeding from a build made on another device or driver costs a rebuild, never a wrong cache hit.

function Resolve-NativeCacheSeedBuildDir {
  param([Parameter(Mandatory=$true)][string]$Seed, [Parameter(Mandatory=$true)][string]$Root,
        [Parameter(Mandatory=$true)][string]$BuildDir)
  $isCommit = $Seed -match '^[0-9a-fA-F]{40}$'
  $candidate = if ($isCommit) { Join-Path $Root "build\native\$($Seed.ToLowerInvariant())" } else { $Seed }
  if (-not (Test-Path -LiteralPath $candidate -PathType Container)) {
    $what = if ($isCommit) { "no native build of VIXEN commit $Seed exists at $candidate" } else { "the cache seed directory does not exist: $candidate" }
    throw "cache seed not found: $what. Pass -CacheSeed <40-hex VIXEN commit with a native build under $Root\build\native> or a native build directory."
  }
  $full = (Resolve-Path -LiteralPath $candidate).ProviderPath.TrimEnd('\')
  $target = [System.IO.Path]::GetFullPath($BuildDir).TrimEnd('\')
  if ($full -ieq $target) {
    throw "cache seed is the build being run ($full); seed from a prior commit's build so the copy has a different source and destination."
  }
  return $full
}

# Cache trees of one native build, relative to the build directory: the build's default runtime cache plus the
# per-capture VIXEN_CACHE_DIR / VIXEN_SHARED_CACHE_DIR directories the glyph SDF tests register under runtime-captures.
function Get-NativeCacheTrees {
  param([Parameter(Mandatory=$true)][string]$BuildDir)
  $trees = [System.Collections.Generic.List[string]]::new()
  $runtimeCache = Join-Path $BuildDir 'runtime-cache'
  if (Test-Path -LiteralPath $runtimeCache -PathType Container) { $trees.Add($runtimeCache) }
  $captures = Join-Path $BuildDir 'runtime-captures'
  if (Test-Path -LiteralPath $captures -PathType Container) {
    Get-ChildItem -LiteralPath $captures -Directory -Recurse |
      Where-Object { $_.Name -like 'cache-*' -or $_.Name -like 'shared-cache-*' } |
      Sort-Object FullName | ForEach-Object { $trees.Add($_.FullName) }
  }
  return $trees
}

# Copy (never link) every cache file of the seed build that the new build does not already have.
# Returns Files, Bytes and Trees counts for the log line.
function Copy-NativeCacheSeed {
  param([Parameter(Mandatory=$true)][string]$SeedBuildDir, [Parameter(Mandatory=$true)][string]$BuildDir)
  $seedRoot = $SeedBuildDir.TrimEnd('\') + '\'
  $trees = @(Get-NativeCacheTrees -BuildDir $SeedBuildDir)
  if (-not $trees.Count) {
    throw "cache seed holds no cache: $SeedBuildDir has no runtime-cache directory and no cache-*/shared-cache-* directory under runtime-captures (was that build ever run on a GPU?)."
  }
  $files = 0; $bytes = [long]0
  foreach ($tree in $trees) {
    foreach ($file in (Get-ChildItem -LiteralPath $tree -File -Recurse)) {
      $relative = $file.FullName.Substring($seedRoot.Length)
      $destination = Join-Path $BuildDir $relative
      if (Test-Path -LiteralPath $destination) { continue }
      New-Item -ItemType Directory -Force (Split-Path -Parent $destination) | Out-Null
      Copy-Item -LiteralPath $file.FullName -Destination $destination
      $files++; $bytes += $file.Length
    }
  }
  return [pscustomobject]@{ Files = $files; Bytes = $bytes; Trees = $trees.Count }
}
