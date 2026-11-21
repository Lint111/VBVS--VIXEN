#!/usr/bin/env bash
# Keep VIXEN's tracked Python surface on a one-way shrink ratchet.
# Deleted tracked files are ignored while a deletion is staged but not committed.
set -euo pipefail

root="$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel 2>/dev/null || true)"
limit=11247
extra_lines=0
self_test=0

while (( $# )); do
  case "$1" in
    --root) root="${2:?--root needs a value}"; shift 2 ;;
    --extra-lines) extra_lines="${2:?--extra-lines needs a value}"; shift 2 ;;
    --self-test) self_test=1; shift ;;
    -h|--help)
      sed -n '2,6p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    *) echo "[check-python-lines] unknown argument: $1" >&2; exit 2 ;;
  esac
done

if [[ -z "$root" || ! -d "$root/.git" && ! -f "$root/.git" ]]; then
  echo "[check-python-lines] ERROR: cannot resolve a git root" >&2
  exit 2
fi

python_lines() {
  git -C "$root" ls-files -z '*.py' |
    while IFS= read -r -d '' path; do
      [[ -f "$root/$path" ]] || continue
      wc -l < "$root/$path"
    done |
    awk '{ total += $1 } END { print total + 0 }'
}

check_total() {
  local actual="$1"
  local expected=$((actual + extra_lines))
  if (( expected > limit )); then
    echo "[check-python-lines] FAIL: ${expected} Python lines exceeds ratchet ${limit} (actual=${actual}, extra=${extra_lines})" >&2
    return 1
  fi
  echo "[check-python-lines] OK: ${actual} Python lines (ratchet <= ${limit})"
}

actual="$(python_lines)"
if (( self_test )); then
  check_total "$actual"
  if bash "${BASH_SOURCE[0]}" --root "$root" --extra-lines 1 >/dev/null 2>&1; then
    echo "[check-python-lines] FAIL: seeded +1 was accepted" >&2
    exit 1
  fi
  echo "[check-python-lines] seeded +1 rejected"
  exit 0
fi

check_total "$actual"
