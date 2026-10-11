#!/usr/bin/env bash
set -euo pipefail
ids="$1"
rep="$2"
tag="$3"
shift 3
cd /mnt/c
/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe -NoProfile -ExecutionPolicy Bypass \
  -File 'C:\vixen-native\brickcost\reports\brickcost\reproduce\capture-matrix.ps1' \
  -ConfigIds "$ids" -Rep "$rep" -Tag "$tag" "$@"
