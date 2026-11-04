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

# Capture routes: capture-routes.tsv is the single declaration of (capture, state) -> CTest regex,
# state environment variable and cleanup paths; native-capture.ps1 reads the same file.
native_routes_file="${script_dir}/capture-routes.tsv"

# native_route_lookup <capture> <state>: sets route_regex, route_env, route_cleanup (return 2 if undeclared).
native_route_lookup() {
  local capture="$1" state="$2" c s
  while IFS=$'\t' read -r c s route_regex route_env route_cleanup; do
    [[ -z "$c" || "$c" == \#* ]] && continue
    if [[ "$c" == "$capture" && "$s" == "$state" ]]; then
      [[ "$route_env" == - ]] && route_env=""
      route_cleanup="${route_cleanup//\{state\}/$state}"
      return 0
    fi
  done < "$native_routes_file"
  echo "native-capture: no route for capture '$capture' state '$state' in $native_routes_file" >&2
  echo "native-capture: declared: $(native_route_list "$capture")" >&2
  return 2
}

# native_route_list [capture]: declared captures, or the states of one capture, space-separated.
native_route_list() {
  local want="${1:-}" c s
  while IFS=$'\t' read -r c s _; do
    [[ -z "$c" || "$c" == \#* ]] && continue
    if [[ -z "$want" ]]; then printf '%s\n' "$c"; elif [[ "$c" == "$want" ]]; then printf '%s\n' "$s"; fi
  done < "$native_routes_file" | awk '!seen[$0]++' | tr '\n' ' ' | sed 's/ $//'
}
