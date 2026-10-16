#!/usr/bin/env bash
# Run one native capture through the box queue and copy CTest's artifacts back to this WSL tree.
set -euo pipefail

usage() {
  echo "usage: native-capture.sh lookdev <state> [--gpu-benchmark]" >&2
  echo "       native-capture.sh planet <state> [--gpu-benchmark]" >&2
  echo "       native-capture.sh glyphsdf <latin-carved|latin-embossed|qps-rtl-carved|qps-rtl-embossed|analytic-vs-voxel> [--gpu-benchmark]" >&2
  echo "       native-capture.sh --label <ctest-label> [--gpu-benchmark]" >&2
  echo "       native-capture.sh --all-tests [--gpu-benchmark]" >&2
  echo "       native-capture.sh --negative-missing-dll [--gpu-benchmark]" >&2
}

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../../.." && pwd)"
repo_commit="$(git -C "$repo_root" rev-parse HEAD)"
native_root=/mnt/c/vixen-native/nativerunner-textshader
queue=/home/liory/.local/bin/with-test-lock.sh
powershell_exe=/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe
mode=state
capture=""
state=""
ctest_label=""
benchmark=false

if [[ "${1:-}" == --negative-missing-dll ]]; then
  mode=negative-dll
  shift
elif [[ "${1:-}" == --all-tests ]]; then
  mode=all-tests
  shift
elif [[ "${1:-}" == --label ]]; then
  [[ $# -ge 2 ]] || { usage; exit 2; }
  mode=label
  ctest_label="$2"
  shift 2
else
  [[ $# -ge 2 ]] || { usage; exit 2; }
  capture="$1"
  state="$2"
  [[ "$capture" == lookdev || "$capture" == planet || "$capture" == glyphsdf ]] || { usage; exit 2; }
  shift 2
fi
while (($#)); do
  case "$1" in
    --gpu-benchmark) benchmark=true ;;
    *) usage; exit 2 ;;
  esac
  shift
done

if [[ ! -f "$native_root/build/native/$repo_commit/.vixendll-build-ready" ]]; then
  bash "$script_dir/native-build.sh"
fi

native_ps="$native_root/reports/capturespeed/reproduce/native-capture.ps1"
native_ps_win="$(wslpath -w "$native_ps")"
native_root_win="$(wslpath -w "$native_root")"
resource="${VIXEN_NATIVE_QUEUE_RESOURCE:-gpu-native}"
queue_args=(--agent vixendll --resource "$resource" \
  --label "vixendll:native-${capture:-${mode}}-${state:-${ctest_label:-missing-dll}}")
if [[ "$benchmark" == true ]]; then queue_args+=(--gpu-benchmark); fi

capture_rc=0
if [[ "$mode" == state ]]; then
  bash "$queue" "${queue_args[@]}" -- "$powershell_exe" -NoLogo -NoProfile -NonInteractive \
    -ExecutionPolicy Bypass -File "$native_ps_win" -Root "$native_root_win" \
    -Commit "$repo_commit" -Mode state -Capture "$capture" -State "$state" || capture_rc=$?
else
  if [[ "$mode" == label ]]; then
    bash "$queue" "${queue_args[@]}" -- "$powershell_exe" -NoLogo -NoProfile -NonInteractive \
      -ExecutionPolicy Bypass -File "$native_ps_win" -Root "$native_root_win" \
      -Commit "$repo_commit" -Mode label -CtestLabel "$ctest_label" || capture_rc=$?
  elif [[ "$mode" == negative-dll ]]; then
    bash "$queue" "${queue_args[@]}" -- "$powershell_exe" -NoLogo -NoProfile -NonInteractive \
      -ExecutionPolicy Bypass -File "$native_ps_win" -Root "$native_root_win" \
      -Commit "$repo_commit" -Mode missing-dll || capture_rc=$?
  else
    bash "$queue" "${queue_args[@]}" -- "$powershell_exe" -NoLogo -NoProfile -NonInteractive \
      -ExecutionPolicy Bypass -File "$native_ps_win" -Root "$native_root_win" \
      -Commit "$repo_commit" -Mode all-tests || capture_rc=$?
  fi
fi

# The CTest scripts create the same relative runtime-captures layout on Windows and WSL.
artifact_root="$native_root/build/native/$repo_commit/runtime-captures"
wsl_output="$repo_root/build/native-captures/$repo_commit"
if [[ -d "$artifact_root" ]]; then
  mkdir -p "$wsl_output/runtime-captures"
  cp -a "$artifact_root/." "$wsl_output/runtime-captures/"
fi
log_root="$native_root/.tmp/native-temp"
if [[ -d "$log_root" ]]; then
  mkdir -p "$wsl_output/logs"
  find "$log_root" -maxdepth 1 -type f -name "ctest-${repo_commit}-*.log" \
    -exec cp -f -- {} "$wsl_output/logs/" \;
  manifest="$log_root/native-executable-smokes-${repo_commit}.tsv"
  if [[ -f "$manifest" ]]; then cp -f -- "$manifest" "$wsl_output/logs/"; fi
fi
exit "$capture_rc"
