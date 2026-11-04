#!/usr/bin/env bash
# Shared inputs for native-build.sh and native-capture.sh: lane-derived roots, the commit being
# built (HEAD, or an opt-in snapshot of a reviewed dirty tree) and the paired Undertow identity.
# Sourced; expects script_dir and repo_root to be set.

# Lane id: explicit VIXEN_NATIVE_LANE, else the queue agent id, else the worktree directory name.
native_lane() {
  local lane="${VIXEN_NATIVE_LANE:-${UT_AGENT_ID:-$(basename "$repo_root")}}"
  lane="$(printf '%s' "$lane" | tr -c 'A-Za-z0-9._-' '-')"
  printf '%s' "$lane"
}

native_lane_id="$(native_lane)"
# One NTFS checkout (and, via the batch file's own location, one .tmp cache) per lane, so
# concurrent lanes never mutate each other's source or FetchContent cache. Override with
# VIXEN_NATIVE_ROOT only to share a checkout on purpose.
native_root="${VIXEN_NATIVE_ROOT:-/mnt/c/vixen-native/nativerunner-${native_lane_id}}"

# Commit to build. A dirty tree is refused unless VIXEN_NATIVE_ALLOW_DIRTY=1; then the tracked and
# untracked (non-ignored) working tree is snapshotted as a deterministic commit on a private ref so the
# same tree always gets the same build key (build dir, ready file) and the default stays commit-pinned.
native_resolve_commit() {
  local head dirty tmp_index tree snap
  head="$(git -C "$repo_root" rev-parse HEAD)"
  dirty="$(git -C "$repo_root" status --porcelain)"
  if [[ -z "$dirty" ]]; then printf '%s' "$head"; return 0; fi
  if [[ "${VIXEN_NATIVE_ALLOW_DIRTY:-}" != 1 ]]; then
    echo "native-build: refusing to sync a dirty VIXEN tree ($(printf '%s\n' "$dirty" | wc -l) changed paths in $repo_root)." >&2
    echo "native-build: commit the change, or set VIXEN_NATIVE_ALLOW_DIRTY=1 to build a snapshot of this reviewed working tree (the snapshot is not a reproducible report commit)." >&2
    return 2
  fi
  tmp_index="$(mktemp)"
  rm -f "$tmp_index"
  tree="$(GIT_INDEX_FILE="$tmp_index" git -C "$repo_root" read-tree HEAD && \
    GIT_INDEX_FILE="$tmp_index" git -C "$repo_root" add -A && \
    GIT_INDEX_FILE="$tmp_index" git -C "$repo_root" write-tree)"
  rm -f "$tmp_index"
  snap="$(GIT_AUTHOR_NAME=native GIT_AUTHOR_EMAIL=native@localhost GIT_AUTHOR_DATE='@0 +0000' \
    GIT_COMMITTER_NAME=native GIT_COMMITTER_EMAIL=native@localhost GIT_COMMITTER_DATE='@0 +0000' \
    git -C "$repo_root" commit-tree "$tree" -p "$head" -m "native dirty snapshot of $head")"
  git -C "$repo_root" update-ref "refs/native-snapshots/$snap" "$snap"
  echo "native-build: VIXEN_NATIVE_ALLOW_DIRTY=1: building snapshot $snap of dirty tree on $head" >&2
  printf '%s' "$snap"
}

# Identity of the paired Undertow schema tree; part of "build ready" so a build made against one
# schema tree is never reused for another (same VIXEN commit, different UNDERTOW_ROOT).
native_undertow_identity() {
  (cd "$1/core/src/Undertow.Authoring/Schema" && find . -type f | LC_ALL=C sort | xargs sha256sum | sha256sum | cut -c1-32)
}

# Absolute Windows-interop executables: cmd.exe/powershell.exe are often missing from the WSL PATH.
native_cmd_exe=/mnt/c/Windows/System32/cmd.exe
native_powershell_exe=/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe
native_require_interop() {
  local exe
  for exe in "$native_cmd_exe" "$native_powershell_exe"; do
    [[ -x "$exe" ]] || { echo "native: Windows interop executable missing: $exe (is WSL interop/automount enabled? fix: check /etc/wsl.conf [automount] and [interop])" >&2; return 2; }
  done
}

# Historical source: `--source-commit <rev>` (or VIXEN_NATIVE_SOURCE_COMMIT) builds and captures that exact
# commit instead of HEAD. A detached worktree of <rev> is created under build/native-source, the script is
# re-run from that tree so it builds with and captures through the tree's own runner scripts (not this
# tree's), then the artifacts are copied back to build/native-captures/<sha> here and the worktree is
# removed (VIXEN_NATIVE_KEEP_SOURCE=1 keeps it). Call as: native_source_dispatch "$0" "$@"; it exits when
# it dispatched, otherwise it leaves the remaining arguments in the native_args array.
native_args=()
native_source_dispatch() {
  local script="$1" rev="${VIXEN_NATIVE_SOURCE_COMMIT:-}"
  shift
  native_args=()
  while (($#)); do
    case "$1" in
      --source-commit)
        [[ $# -ge 2 ]] || { echo "native: --source-commit needs a revision" >&2; exit 2; }
        rev="$2"; shift 2 ;;
      --source-commit=*) rev="${1#--source-commit=}"; shift ;;
      *) native_args+=("$1"); shift ;;
    esac
  done
  if [[ -n "${VIXEN_NATIVE_SOURCE_ACTIVE:-}" ]]; then rev=""; fi
  [[ -n "$rev" ]] || return 0

  local sha tree rc=0 relscript out
  sha="$(git -C "$repo_root" rev-parse --verify --quiet "${rev}^{commit}")" || {
    echo "native: --source-commit '$rev' is not a commit in $repo_root" >&2; exit 2; }
  tree="$repo_root/build/native-source/$sha"
  relscript="reports/capturespeed/reproduce/$(basename "$script")"
  if [[ ! -d "$tree" ]]; then
    mkdir -p "$(dirname "$tree")"
    git -C "$repo_root" worktree add --detach "$tree" "$sha" >&2
  fi
  [[ "$(git -C "$tree" rev-parse HEAD)" == "$sha" ]] || {
    echo "native: $tree is not at $sha" >&2; exit 2; }
  # The repo checks out sparsely; the runner scripts must exist in the historical tree.
  if [[ ! -f "$tree/$relscript" ]]; then
    git -C "$tree" sparse-checkout add "/reports/capturespeed/reproduce" >/dev/null 2>&1 || true
  fi
  [[ -f "$tree/$relscript" ]] || {
    echo "native: $sha has no $relscript; that commit predates the native runner" >&2; exit 2; }
  echo "native: --source-commit $sha: running $tree/$relscript" >&2

  VIXEN_NATIVE_SOURCE_ACTIVE=1 VIXEN_NATIVE_SOURCE_COMMIT= \
    VIXEN_NATIVE_LANE="$native_lane_id" VIXEN_NATIVE_ROOT="$native_root" \
    bash "$tree/$relscript" ${native_args[@]+"${native_args[@]}"} || rc=$?

  out="$tree/build/native-captures/$sha"
  if [[ -d "$out" ]]; then
    mkdir -p "$repo_root/build/native-captures"
    rm -rf "$repo_root/build/native-captures/$sha"
    cp -a "$out" "$repo_root/build/native-captures/$sha"
  fi
  if [[ "${VIXEN_NATIVE_KEEP_SOURCE:-}" != 1 ]]; then
    git -C "$repo_root" worktree remove --force "$tree" >&2 || true
  fi
  exit "$rc"
}
