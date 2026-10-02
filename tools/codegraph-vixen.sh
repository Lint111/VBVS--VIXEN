#!/usr/bin/env bash
set -euo pipefail

if [[ $# -eq 0 ]]; then
    echo "Usage: tools/codegraph-vixen.sh <query...>" >&2
    echo "Set VIXEN_CODEGRAPH_ROOT to an indexed VIXEN checkout when needed." >&2
    exit 2
fi

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
worktree_root="$(git -C "$script_dir/.." rev-parse --show-toplevel)"
common_git_dir="$(git -C "$worktree_root" rev-parse --path-format=absolute --git-common-dir)"
canonical_root="$(cd -- "$(dirname -- "$common_git_dir")" && pwd -P)"

if [[ -n "${VIXEN_CODEGRAPH_ROOT:-}" ]]; then
    project_root="$(cd -- "$VIXEN_CODEGRAPH_ROOT" 2>/dev/null && pwd -P)" || {
        echo "VIXEN_CODEGRAPH_ROOT is not a directory: $VIXEN_CODEGRAPH_ROOT" >&2
        exit 2
    }
elif [[ -d "$worktree_root/.codegraph" ]]; then
    project_root="$worktree_root"
elif [[ -d "$canonical_root/.codegraph" ]]; then
    project_root="$canonical_root"
else
    echo "No VIXEN CodeGraph index found in this worktree or its canonical checkout." >&2
    echo "Index one VIXEN checkout through the normal CodeGraph workflow, then set:" >&2
    echo "  VIXEN_CODEGRAPH_ROOT=/absolute/path/to/indexed/VIXEN" >&2
    exit 2
fi

if [[ ! -d "$project_root/.codegraph" ]]; then
    echo "No CodeGraph index at $project_root/.codegraph" >&2
    echo "Set VIXEN_CODEGRAPH_ROOT to an indexed VIXEN checkout." >&2
    exit 2
fi

if [[ "$project_root" != "$worktree_root" ]]; then
    echo "Using shared CodeGraph index: $project_root" >&2
    echo "The index may not include uncommitted changes in: $worktree_root" >&2
fi

exec codegraph explore --path "$project_root" "$@"
