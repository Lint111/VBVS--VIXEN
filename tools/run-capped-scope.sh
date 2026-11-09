#!/usr/bin/env bash
set -euo pipefail

# Run one command in a memory-capped transient systemd scope and return its exit status.
#
# Quoting contract: nothing here goes through a shell. Per-invocation environment is passed as
# repeated NAME=VALUE arguments and handed to systemd-run as --setenv, and the command is exec'd
# from its own argv, so values containing spaces, '$' or quotes reach the child unchanged. Do not
# build `systemd-run ... bash -c "... $STATE ..."` strings: the parent shell expands the variables
# before launch and the child sees empty values (T-4692).
#
# Wait contract: a scope is the foreground process, so this script returns when the command exits.
# `systemd-run --wait` is for services and is rejected together with --scope; do not add it.
#
# Usage: tools/run-capped-scope.sh [--memory-max 16G] [--env NAME=VALUE]... -- <command> [args...]
# Example (inside a queue GPU job, never around one):
#   tools/run-capped-scope.sh --env VIXEN_LOOKDEV_CAPTURE_STATE=midday \
#       --env VIXEN_LOOKDEV_CAPTURE_DIR="$out/midday" -- build/vixen_lookdev_capture \
#       --gtest_filter=LookdevCapture.RendersFourStatesAtFourAnglesDeterministically
usage() { echo "Usage: $0 [--memory-max SIZE] [--env NAME=VALUE]... -- <command> [args...]" >&2; exit 2; }

memory_max=16G
setenv=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --memory-max) [[ $# -ge 2 ]] || usage; memory_max="$2"; shift 2 ;;
        --env)
            [[ $# -ge 2 && "$2" =~ ^[A-Za-z_][A-Za-z0-9_]*= ]] || { echo "--env needs NAME=VALUE" >&2; usage; }
            setenv+=("--setenv=$2"); shift 2 ;;
        --) shift; break ;;
        *) usage ;;
    esac
done
[[ $# -ge 1 ]] || usage

echo "[run-capped-scope] MemoryMax=$memory_max env=${#setenv[@]} cmd=$1" >&2
exec systemd-run --user --scope --quiet --collect -p "MemoryMax=$memory_max" "${setenv[@]}" -- "$@" </dev/null
