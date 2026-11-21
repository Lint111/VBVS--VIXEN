#!/usr/bin/env bash
# Raw lane evidence must stay outside the VIXEN repository.
# Markdown reports may retain image files they link as part of their explanation; all other
# report logs, captures, and tabular dumps are external evidence.
set -euo pipefail
export LC_ALL=C

ROOT_ARG=""
while (($# > 0)); do
    case "$1" in
        --root)
            (($# >= 2)) || { echo "ERROR: --root requires a repository path" >&2; exit 2; }
            ROOT_ARG="$2"
            shift 2
            ;;
        --help|-h)
            echo "usage: VIXEN/scripts/check-raw-evidence.sh [--root REPOSITORY_ROOT]"
            exit 0
            ;;
        *)
            echo "ERROR: unknown argument: $1" >&2
            exit 2
            ;;
    esac
done

if [[ -n "$ROOT_ARG" ]]; then
    ROOT="$(cd "$ROOT_ARG" && pwd -P)"
else
    ROOT="$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
fi
cd "$ROOT"

mapfile -d '' -t added < <(
    {
        git diff --cached --name-only --diff-filter=A -z -- .
        if git rev-parse --verify HEAD >/dev/null 2>&1; then
            git diff-tree --no-commit-id --name-only --diff-filter=A -r -z HEAD
        fi
    } | sort -zu
)

python3 - "$ROOT" "${added[@]}" <<'PY'
import posixpath
import subprocess
import sys
from pathlib import Path

root = Path(sys.argv[1])
added = [path.replace("\\", "/") for path in sys.argv[2:]]
tracked = subprocess.check_output(["git", "-C", str(root), "ls-files", "-z"]).decode().split("\0")
tracked = {path for path in tracked if path}

linked_images = set()
for markdown in (path for path in tracked if path.endswith(".md")):
    text = (root / markdown).read_text(encoding="utf-8", errors="replace")
    start = 0
    while True:
        start = text.find("](", start)
        if start < 0:
            break
        end = text.find(")", start + 2)
        if end < 0:
            break
        raw_ref = text[start + 2:end].split(None, 1)
        start = end + 1
        if not raw_ref:
            # Code examples can contain C++/lambda syntax such as [](){} that resembles a
            # Markdown link opener. It is not a link and must not make the guard fail to parse.
            continue
        ref = raw_ref[0].strip("<>").split("#", 1)[0].split("?", 1)[0]
        if not ref or ref.startswith(("/", "#")) or "://" in ref:
            continue
        target = posixpath.normpath(posixpath.join(posixpath.dirname(markdown), ref))
        if target in tracked and target.lower().endswith((".png", ".ppm", ".exr")):
            linked_images.add(target)

violations = []
report_roots = ("reports/", "VIXEN/reports/")
for path in added:
    lower = path.lower()
    if path.startswith((".vs/", "temp_bench/", "gate-artifacts/", "VIXEN/gate-artifacts/")):
        violations.append((path, "temporary/build directory"))
    elif lower.endswith((".log", ".log.gz")):
        violations.append((path, "raw log"))
    elif path.startswith(report_roots) and lower.endswith(".csv"):
        violations.append((path, "raw tabular dump"))
    elif path.startswith(report_roots) and lower.endswith((".png", ".ppm", ".exr")) and path not in linked_images:
        violations.append((path, "unlinked raw capture"))

if violations:
    print("FAIL: newly added raw evidence must stay outside the VIXEN repository:", file=sys.stderr)
    for path, kind in violations:
        print(f"  {path} ({kind})", file=sys.stderr)
    print("Commit Markdown reports only; store raw evidence under the external lane evidence store.", file=sys.stderr)
    raise SystemExit(1)

print(f"PASS: raw evidence guard checked {len(added)} newly added path(s); linked report images are allowed.")
PY
