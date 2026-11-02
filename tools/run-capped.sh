#!/usr/bin/env bash
# Run a command in a memory-capped transient systemd scope and report the cgroup peak (T-5001).
#
# `systemd-run --scope` already runs the command in the foreground and returns its exit status, so it must
# NOT be combined with --wait (systemd rejects that pair). Use this wrapper inside a queue payload:
#   with-test-lock.sh --agent <lane> --resource gpu --label <lane>:capture -- \
#     tools/run-capped.sh 16G -- <command> [args...]
# The cap is MemoryMax with swap disabled; the peak (memory.peak) and OOM-kill count are printed to stderr.
set -uo pipefail

usage() { echo "usage: run-capped.sh <MemoryMax e.g. 16G> -- <command> [args...]" >&2; exit 2; }
[[ $# -ge 3 && "$2" == "--" ]] || usage
cap="$1"; shift 2
command -v systemd-run >/dev/null || { echo "run-capped: systemd-run not found; run the command uncapped or install systemd (WSL needs systemd=true in /etc/wsl.conf)" >&2; exit 127; }

# The scope's cgroup disappears when the command exits, so the peak is read from inside the scope.
inner='"$@"; status=$?
cg=/sys/fs/cgroup$(sed -n "s/^0:://p" /proc/self/cgroup)
peak=unknown; oom_kill=unknown
[[ -r $cg/memory.peak ]] && peak=$(<"$cg/memory.peak")
[[ -r $cg/memory.events ]] && oom_kill=$(awk "\$1==\"oom_kill\"{print \$2}" "$cg/memory.events")
echo "run-capped: exit=$status memory.peak=$peak oom_kill=$oom_kill" >&2
exit $status'
exec systemd-run --user --scope --quiet --unit="vixen-capped-$$" -p "MemoryMax=$cap" -p MemorySwapMax=0 -- bash -c "$inner" run-capped "$@"
