#!/usr/bin/env bash
# usage: run-captures.sh <ids> <rep> <tag> [ExtraEnv 'K=V;K2=V2' | any other capture-matrix.ps1 args]
# A 4th argument that looks like KEY=VALUE[;KEY=VALUE...] is passed as -ExtraEnv (it used to bind
# positionally to -Root, so the harness looked for configs under C:\VIXEN_WINDOW_WIDTH=...).
set -euo pipefail
if [ "$#" -lt 3 ]; then
  echo "run-captures.sh: need <ids> <rep> <tag> [ExtraEnv]; got $# args. Example: run-captures.sh 1,2 0 mytag 'VIXEN_WINDOW_WIDTH=1920;VIXEN_WINDOW_HEIGHT=1080'" >&2
  exit 2
fi
ids="$1"
rep="$2"
tag="$3"
shift 3
if [ "$#" -ge 1 ] && [[ "$1" =~ ^[A-Za-z_][A-Za-z0-9_]*= ]]; then
  set -- -ExtraEnv "$@"
fi
cd /mnt/c
/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe -NoProfile -ExecutionPolicy Bypass \
  -File 'C:\vixen-native\brickcost\reports\brickcost\reproduce\capture-matrix.ps1' \
  -ConfigIds "$ids" -Rep "$rep" -Tag "$tag" "$@"
