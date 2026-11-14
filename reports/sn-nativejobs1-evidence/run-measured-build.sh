#!/usr/bin/env bash
# Measured native build: samples Windows CPU / cl.exe memory around one vixen-native.sh build.
# Usage: run-measured-build.sh TAG BINDIR_WSL CCACHE_WSL [JOBS|derived] [ACTION] [clean]
# "clean" first removes the build outputs (ninja clean) but keeps the compiler cache under the build dir: a warm build.
# BINDIR_WSL / CCACHE_WSL are /mnt/c paths. JOBS=derived leaves VIXEN_MAX_BUILD_JOBS unset.
set -uo pipefail
tag="$1"; bindir="$2"; ccache="$3"; jobs="${4:-derived}"; action="${5:-all}"; clean="${6:-}"
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
ps="/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe"
out="$here/$tag"; mkdir -p "$out"
stop="/mnt/c/vixen-native/.tmp/sample-$tag.stop"; rm -f "$stop"; mkdir -p /mnt/c/vixen-native/.tmp
export VIXEN_BINARY_DIR="$bindir" CCACHE_DIR="$ccache"
export WSLENV="${WSLENV:+$WSLENV:}CCACHE_DIR/p"
if [[ "$jobs" == derived ]]; then unset VIXEN_MAX_BUILD_JOBS; else export VIXEN_MAX_BUILD_JOBS="$jobs"; fi
"$ps" -NoProfile -ExecutionPolicy Bypass -File "$(wslpath -w "$here/sample-build.ps1")" \
  -Out "$(wslpath -w "$out/samples.csv")" -Stop "$(wslpath -m "$stop")" >/dev/null 2>&1 &
samp=$!
if [[ "$clean" == clean ]]; then
  # Warm build: remove the built outputs (objects, libraries, executables, PDBs, PCHs) under the build dir
  # (not its _cache compiler cache) and under the fetched dependencies' build dirs; leave build.ninja, the
  # CMake cache and the sources alone so no CMake regeneration is triggered. ninja then rebuilds every step
  # and ccache serves it.
  "$ps" -NoProfile -Command "\$n=0; foreach (\$d in @('$(wslpath -w "$bindir")') + @(Get-ChildItem 'C:\\vixen-native\\nativejobs1\\.tmp\\fetch' -Directory -Filter '*-build' | % FullName)) { Get-ChildItem \$d -Recurse -File -Include *.obj,*.lib,*.exe,*.dll,*.pdb,*.pch,*.ilk,*.exp -ErrorAction SilentlyContinue | ? { \$_.FullName -notmatch '\\\\_cache\\\\' } | % { Remove-Item \$_.FullName -Force; \$n++ } }; 'removed ' + \$n + ' output files'" > "$out/clean.log" 2>&1
  cat "$out/clean.log"
fi
sleep 4
start=$(date +%s.%N)
nice -n 10 bash "$repo/tools/vixen-native.sh" --log "$out/build.log" build "$action" vixen-ninja vixen_lookdev_capture
rc=$?
end=$(date +%s.%N)
touch "$stop"; wait "$samp" 2>/dev/null
wall=$(awk -v a="$start" -v b="$end" 'BEGIN{printf "%.1f", b-a}')
{ echo "tag=$tag rc=$rc wall_s=$wall jobs=$jobs"; python3 -I "$here/summarize-samples.py" "$out/samples.csv"; } | tee "$out/summary.txt"
grep -ci "warning" "$out/build.log" | sed 's/^/warning_lines(any case, incl. CMake notices)=/' | tee -a "$out/summary.txt"
grep -E "Max parallel jobs|BUILD SUMMARY|VixenBuildLock|lock acquired|FAILED" "$out/build.log" | head -8 | tee -a "$out/summary.txt"
exit "$rc"
