#!/usr/bin/env bash
# Offline test for `--source-commit`: the historical commit's own native-capture.sh runs (a stub here),
# artifacts come back under build/native-captures/<sha>, the source worktree is removed, errors are clean.
set -euo pipefail
unset UT_AGENT_ID VIXEN_NATIVE_LANE VIXEN_NATIVE_ROOT VIXEN_NATIVE_SOURCE_COMMIT VIXEN_NATIVE_KEEP_SOURCE
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
tmp="$(mktemp -d)"
[[ -n "${KEEP_TMP:-}" ]] || trap 'rm -rf "$tmp"' EXIT
repo="$tmp/repo"
rr=reports/capturespeed/reproduce
git init -q "$repo"
git -C "$repo" config user.email t@t
git -C "$repo" config user.name t
mkdir -p "$repo/$rr"

# Old commit: its own runner script (a stub that records what it was given and writes an artifact).
cp "$here/native-common.sh" "$repo/$rr/"
cat > "$repo/$rr/native-capture.sh" <<'STUB'
#!/usr/bin/env bash
set -euo pipefail
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../../.." && pwd)"
sha="$(git -C "$repo_root" rev-parse HEAD)"
mkdir -p "$repo_root/build/native-captures/$sha"
echo "old-runner args=$* lane=${VIXEN_NATIVE_LANE:-} source_commit='${VIXEN_NATIVE_SOURCE_COMMIT:-}'" > "$repo_root/build/native-captures/$sha/ran.txt"
exit "${STUB_RC:-0}"
STUB
git -C "$repo" add -A
git -C "$repo" commit -qm old
old="$(git -C "$repo" rev-parse HEAD)"

# New commit: the real scripts under test.
cp "$here/native-common.sh" "$here/native-capture.sh" "$repo/$rr/"
git -C "$repo" add -A
git -C "$repo" commit -qm new
new="$(git -C "$repo" rev-parse HEAD)"
cap="$repo/$rr/native-capture.sh"
fail() { echo "FAIL: $*" >&2; exit 1; }

# 1. short rev selects the old tree; option stripped; artifact lands; worktree removed.
bash "$cap" --source-commit "${old:0:10}" lookdev midday 2>"$tmp/err1" || fail "run 1: $(cat "$tmp/err1")"
ran="$repo/build/native-captures/$old/ran.txt"
[[ -f "$ran" ]] || fail "no artifact for $old"
grep -q "args=lookdev midday " "$ran" || fail "args not forwarded without the option: $(cat "$ran")"
grep -q "lane=repo " "$ran" || fail "lane not pinned to invoking tree: $(cat "$ran")"
grep -q "source_commit=''" "$ran" || fail "source commit leaked into the child"
[[ ! -e "$repo/build/native-source/$old" ]] || fail "source worktree left behind"
[[ -z "$(git -C "$repo" worktree list | grep native-source || true)" ]] || fail "worktree still registered"
[[ ! -e "$repo/build/native-captures/$new" ]] || fail "HEAD tree was captured"

# 2. env form, --source-commit=<rev> form, child exit status propagates, VIXEN_NATIVE_KEEP_SOURCE keeps the tree.
rc=0
STUB_RC=7 VIXEN_NATIVE_KEEP_SOURCE=1 bash "$cap" --source-commit="$old" --label x 2>/dev/null || rc=$?
[[ "$rc" == 7 ]] || fail "exit status $rc, wanted 7"
[[ -d "$repo/build/native-source/$old" ]] || fail "KEEP_SOURCE did not keep the worktree"
rc=0
VIXEN_NATIVE_SOURCE_COMMIT="$old" bash "$cap" planet noon 2>/dev/null || rc=$?
[[ "$rc" == 0 ]] || fail "env form exit $rc"
grep -q "args=planet noon " "$ran" || fail "env form not dispatched"

# 3. bad revision and missing option value fail cleanly with exit 2.
rc=0; bash "$cap" --source-commit nosuchrev lookdev x 2>"$tmp/err3" || rc=$?
[[ "$rc" == 2 ]] && grep -q "not a commit" "$tmp/err3" || fail "bad rev: rc=$rc $(cat "$tmp/err3")"
rc=0; bash "$cap" --source-commit 2>"$tmp/err4" || rc=$?
[[ "$rc" == 2 ]] && grep -q "needs a revision" "$tmp/err4" || fail "missing value: rc=$rc"
echo "test-source-commit: PASS"
