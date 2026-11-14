#!/usr/bin/env bash
# ctest -L RenderGraph on a native build dir (Windows ctest, serial), output kept next to this script.
# Usage: run-ctest-rendergraph.sh TAG BINDIR_WSL
tag="$1"; bindir="$2"; here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"; mkdir -p "$here/$tag"
ps=/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe
"$ps" -NoProfile -Command "cd '$(wslpath -w "$bindir")'; & 'C:\Program Files\CMake\bin\ctest.exe' -L RenderGraph -j1 --output-on-failure --timeout 600" 2>&1 | tr -d '\r' | tee "$here/$tag/ctest.log" | tail -25
exit "${PIPESTATUS[0]}"
