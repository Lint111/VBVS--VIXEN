#!/usr/bin/env bash
# sn-winjob1: tools/vixen-native.sh roots the Windows build tree in box-winjob.exe (a Job Object with KILL_ON_JOB_CLOSE).
# Offline part: which exe is chosen, and the off switch. Live part (VIXEN_NATIVE_LIVE_TEST=1, run it as a native job through the queue): a SIGKILL of the
# Linux side of the interop call, which is what the queue's kill does to the wrapper, leaves no cl.exe/ninja.exe of the tree running on Windows.
#   VIXEN_NATIVE_LIVE_TEST=1 WINJOB_EXE=/path/box-winjob.exe [WINJOB_WAIT_S=15] bash tools/tests/test-vixen-native-winjob.sh
set -uo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
script="$here/../vixen-native.sh"
work="$(mktemp -d "${TMPDIR:-/tmp}/vixen-native-winjob.XXXXXX")"
lane=""
trap '[[ -z "$lane" ]] || rm -rf "$lane"; rm -rf "$work"' EXIT
fails=0
check() { if [[ "$2" == 0 ]]; then echo "ok   $1"; else echo "FAIL $1"; fails=$((fails + 1)); fi; }

# winjob_exe is the one function this test needs; take its text from the launcher instead of running the launcher.
eval "$(sed -n '/^winjob_exe() {/,/^}/p' "$script")"
printf '#!/bin/sh\n' >"$work/box-winjob.exe"; chmod +x "$work/box-winjob.exe"
printf 'x' >"$work/plain.exe"
none="$(env -u UT_QUEUE_WINJOB_EXE -u UT_QUEUE_CORE_DIR -u UT_QUEUE_WINJOB bash -c "$(sed -n '/^winjob_exe() {/,/^}/p' "$script"); winjob_exe")"
check "no queue core dir and no override: no exe" "$([[ -z "$none" ]]; echo $?)"
got="$(UT_QUEUE_CORE_DIR="$work" winjob_exe)"
check "the queue's core dir supplies box-winjob.exe" "$([[ "$got" == "$work/box-winjob.exe" ]]; echo $?)"
got="$(UT_QUEUE_CORE_DIR=/nonexistent UT_QUEUE_WINJOB_EXE="$work/box-winjob.exe" winjob_exe)"
check "UT_QUEUE_WINJOB_EXE wins over the core dir" "$([[ "$got" == "$work/box-winjob.exe" ]]; echo $?)"
got="$(UT_QUEUE_CORE_DIR="$work" UT_QUEUE_WINJOB=0 winjob_exe)"
check "UT_QUEUE_WINJOB=0 turns it off" "$([[ -z "$got" ]]; echo $?)"
got="$(UT_QUEUE_WINJOB_EXE="$work/plain.exe" winjob_exe)"
check "a file that is not executable is not used" "$([[ -z "$got" ]]; echo $?)"
grep -q '"${runner\[@\]}" /c "$win_arg"' "$script"; check "run_bat starts the build through the runner" $?

if [[ "${VIXEN_NATIVE_LIVE_TEST:-0}" == 1 ]]; then
    exe="${WINJOB_EXE:?WINJOB_EXE names box-winjob.exe}"; wait_s="${WINJOB_WAIT_S:-15}"
    ps=/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe
    win_temp="$("$ps" -NoProfile -Command '$env:TEMP' | tr -d '\r')"
    lane_win="$win_temp\\winjob-live-$$"; lane="$(wslpath -u "$lane_win")"
    mkdir -p "$lane"
    cp /mnt/c/Windows/System32/ping.exe "$lane/cl.exe"; cp /mnt/c/Windows/System32/ping.exe "$lane/ninja.exe"
    printf '@echo off\r\nstart "" /b "%%~dp0cl.exe" -n 600 127.0.0.1 >nul\r\n"%%~dp0ninja.exe" -n 600 127.0.0.1 >nul\r\n' >"$lane/tree.cmd"
    lane_procs() { # count of processes whose image is under the lane directory
        "$ps" -NoProfile -Command "@(Get-CimInstance Win32_Process | ? { \$_.ExecutablePath -and \$_.ExecutablePath.StartsWith('$lane_win', 'OrdinalIgnoreCase') }).Count" | tr -d '\r'
    }
    cleanup_lane() { "$ps" -NoProfile -Command "Get-CimInstance Win32_Process | ? { \$_.ExecutablePath -and \$_.ExecutablePath.StartsWith('$lane_win', 'OrdinalIgnoreCase') } | % { Stop-Process -Id \$_.ProcessId -Force }" >/dev/null 2>&1 || true; }
    trap 'cleanup_lane; [[ -z "$lane" ]] || rm -rf "$lane"; rm -rf "$work"' EXIT
    cd /mnt/c/Windows/System32 || exit 2
    # The same shape run_bat uses: the helper as the interop process, cmd.exe as its child, the build tree below.
    ( exec "$exe" run -- cmd.exe /c "$lane_win\\tree.cmd" >/dev/null 2>&1 </dev/null ) & relay=$!
    for _ in $(seq 1 40); do [[ "$(lane_procs)" -ge 2 ]] && break; sleep 0.5; done
    check "live: the tree is running (cl.exe + ninja.exe under the lane)" "$([[ "$(lane_procs)" -ge 2 ]]; echo $?)"
    kill -KILL "$relay"
    left=2; start=$SECONDS
    while (( SECONDS - start < wait_s )); do left="$(lane_procs)"; [[ "$left" == 0 ]] && break; sleep 0.5; done
    check "live: SIGKILL of the interop call leaves nothing under the lane after $((SECONDS - start))s (limit ${wait_s}s; left $left)" "$([[ "$left" == 0 ]]; echo $?)"
fi
exit "$fails"
