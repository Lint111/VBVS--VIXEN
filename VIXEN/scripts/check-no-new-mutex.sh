#!/usr/bin/env bash
# No-new-mutex declaration gate — lock-free federation phase 0 (design §5 phase 0/6, OD-11).
#
# Engine-side twin of the kernel's R-B enforcement gate (undertow tools/check-native-dispatch-plan.sh):
# the same shape — a declaration check that FAILs when the tree gains a mutex that is not declared,
# with the declared set pinned in a reviewed allowlist. "Declared" means one of:
#   (a) the file's owning mutex declarations are pinned in scripts/no-new-mutex.allowlist with their
#       inventory family IDs (2026-09-08-runtime-lock-inventory-and-classification.md), or
#   (b) the declaration is spelled through Core/LockCensus.h (`LockCensus::Mutex<Family::XX>` /
#       `LockCensus::SharedMutex<Family::XX>`), which names its inventory row in the type itself.
#
# What is counted (per non-test file under VIXEN/libraries and VIXEN/application, comments and
# string literals stripped — the inventory's census lexer):
#   owned  = std::mutex / recursive_mutex / shared_mutex / shared_timed_mutex / timed_mutex tokens that
#            are neither a guard's template argument (lock_guard<...>, unique_lock<...>, ...) nor a
#            borrowed pointer/reference (std::mutex* / std::mutex&) — i.e. a NEW lock object;
#            plus LockCensus wrapper declarations (self-declared, still counted so the census is whole).
#   syntax = any std mutex/guard token at all (borrowed acquisitions included) — decides whether a
#            file belongs in the census even with owned == 0 (the VK2 borrowers).
#
# Verdicts (mirrors the kernel gate: FAIL: lines + exit 1; PASS: line + exit 0):
#   FAIL  a mutex-syntax file absent from the allowlist            (new mutex-bearing file)
#   FAIL  owned > pinned                                           (new owning declaration in a known file)
#   FAIL  owned < pinned, or a pinned file without mutex syntax    (stale pin — the count may only go
#                                                                   down, but the pin must follow it so
#                                                                   a later re-add cannot hide under it)
#   WARN  a pinned row whose family is UNCLASSIFIED                (declared but not yet in the inventory;
#                                                                   exit 0 — the list is honest, not closed)
#   --fix rewrites the allowlist from the tree: counts re-synced, new files appended as UNCLASSIFIED,
#         stale rows dropped, existing family IDs preserved. The diff IS the declaration to review.
#
# usage: check-no-new-mutex.sh [--root REPOSITORY_ROOT] [--allowlist FILE] [--fix]
set -euo pipefail
export LC_ALL=C

usage() {
    cat >&2 <<'EOF'
usage: check-no-new-mutex.sh [--root REPOSITORY_ROOT] [--allowlist FILE] [--fix]

Fails if VIXEN/libraries or VIXEN/application gains a mutex declaration that is not pinned in
scripts/no-new-mutex.allowlist (or self-declared via Core/LockCensus.h). --fix re-syncs the allowlist
from the tree (new rows are appended as UNCLASSIFIED and reported as WARN until classified).
EOF
}

ROOT_ARG=""
ALLOWLIST_ARG=""
FIX=0
while (($# > 0)); do
    case "$1" in
        --root)
            if (($# < 2)); then echo "ERROR: --root requires a repository path" >&2; usage; exit 2; fi
            ROOT_ARG="$2"; shift 2 ;;
        --allowlist)
            if (($# < 2)); then echo "ERROR: --allowlist requires a file path" >&2; usage; exit 2; fi
            ALLOWLIST_ARG="$2"; shift 2 ;;
        --fix) FIX=1; shift ;;
        --help|-h) usage; exit 0 ;;
        *) echo "ERROR: unknown argument: $1" >&2; usage; exit 2 ;;
    esac
done

if [[ -n "$ROOT_ARG" ]]; then
    ROOT="$ROOT_ARG"
else
    ROOT="$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
fi
ALLOWLIST="${ALLOWLIST_ARG:-$ROOT/VIXEN/scripts/no-new-mutex.allowlist}"

python3 - "$ROOT" "$ALLOWLIST" "$FIX" <<'PY'
import re
import sys
from pathlib import Path

root = Path(sys.argv[1])
allowlist_path = Path(sys.argv[2])
fix = sys.argv[3] == "1"

EXTENSIONS = {'.h', '.hpp', '.hxx', '.cpp', '.c', '.cc', '.cxx', '.inl', '.ipp', '.ixx'}
SCAN_ROOTS = (root / 'VIXEN' / 'libraries', root / 'VIXEN' / 'application')

# The inventory's census lexer: blank out comments and string/char literals, keep line structure.
LEX = re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')
SYNTAX = re.compile(r'\bstd::(?:mutex|recursive_mutex|shared_mutex|shared_timed_mutex|'
                    r'timed_mutex|lock_guard|scoped_lock|unique_lock|shared_lock)\b')
MUTEX_TYPE = re.compile(r'\bstd::(?:mutex|recursive_mutex|shared_mutex|shared_timed_mutex|timed_mutex)\b')
GUARD_OPENER = re.compile(r'(?:lock_guard|unique_lock|shared_lock|scoped_lock)\s*<\s*$')
CENSUS_DECL = re.compile(r'\bLockCensus::(?:Mutex|SharedMutex)\s*<')


def owning_declarations(source: str) -> int:
    count = 0
    for match in MUTEX_TYPE.finditer(source):
        before = source[max(0, match.start() - 40):match.start()]
        if GUARD_OPENER.search(before):
            continue  # a guard's template argument, not a lock object
        after = source[match.end():match.end() + 8].lstrip()
        if after.startswith('*') or after.startswith('&'):
            continue  # borrowed pointer/reference to someone else's lock
        count += 1
    return count + len(CENSUS_DECL.findall(source))


scanned = 0
actual = {}  # relative path -> (owned, syntax_lines)
for base in SCAN_ROOTS:
    if not base.is_dir():
        print(f"FAIL: scan root missing: {base}")
        raise SystemExit(1)
    for path in sorted(base.rglob('*')):
        if path.suffix not in EXTENSIONS or 'tests' in path.parts:
            continue
        scanned += 1
        try:
            text = path.read_text(encoding='utf-8', errors='replace')
        except OSError as exc:
            print(f"FAIL: cannot read {path}: {exc}")
            raise SystemExit(1)
        source = LEX.sub(lambda m: '\n' * m[0].count('\n'), text)
        syntax_lines = sum(1 for line in source.splitlines() if SYNTAX.search(line) or CENSUS_DECL.search(line))
        if syntax_lines == 0:
            continue
        rel = path.relative_to(root).as_posix()
        actual[rel] = (owning_declarations(source), syntax_lines)

pinned = {}  # relative path -> (owned, families)
order = []
if allowlist_path.is_file():
    for lineno, raw in enumerate(allowlist_path.read_text(encoding='utf-8').splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith('#'):
            continue
        parts = line.split('\t')
        if len(parts) != 3:
            print(f"FAIL: {allowlist_path}:{lineno}: expected <path>\\t<owned>\\t<families>, got {raw!r}")
            raise SystemExit(1)
        rel, owned, families = parts
        if rel in pinned:
            print(f"FAIL: {allowlist_path}:{lineno}: duplicate row for {rel}")
            raise SystemExit(1)
        try:
            pinned[rel] = (int(owned), families)
        except ValueError:
            print(f"FAIL: {allowlist_path}:{lineno}: owned count is not an integer: {owned!r}")
            raise SystemExit(1)
        order.append(rel)
elif not fix:
    print(f"FAIL: allowlist not found: {allowlist_path} (run with --fix to create it)")
    raise SystemExit(1)

if fix:
    rows = []
    for rel in sorted(actual):
        owned, _ = actual[rel]
        families = pinned[rel][1] if rel in pinned else 'UNCLASSIFIED'
        rows.append((rel, owned, families))
    header = (
        "# No-new-mutex allowlist — the declared exception set for scripts/check-no-new-mutex.sh.\n"
        "# One row per non-test file under VIXEN/libraries + VIXEN/application that carries std mutex/guard\n"
        "# syntax: <path>\\t<owning mutex declarations>\\t<inventory family IDs, comma-separated>.\n"
        "# Family IDs are the lock inventory's (2026-09-08-runtime-lock-inventory-and-classification.md);\n"
        "# 'VK2-borrowed' = acquires VulkanDevice::SubmitMutex() without owning a lock; 'CENSUS' = the\n"
        "# LockCensus.h wrapper itself; 'UNCLASSIFIED' = declared here but not yet classified (WARN).\n"
        "# Counts may only go down; a removal commit must re-sync its row (--fix), an addition must be classified.\n"
    )
    body = ''.join(f"{rel}\t{owned}\t{families}\n" for rel, owned, families in rows)
    allowlist_path.write_text(header + body, encoding='utf-8')
    added = sorted(set(actual) - set(pinned))
    dropped = sorted(set(pinned) - set(actual))
    resynced = sorted(rel for rel in actual if rel in pinned and pinned[rel][0] != actual[rel][0])
    print(f"fixed {allowlist_path}: {len(rows)} rows; added={len(added)} dropped={len(dropped)} resynced={len(resynced)}")
    for rel in added:
        print(f"  + {rel} (UNCLASSIFIED — classify before merging)")
    for rel in dropped:
        print(f"  - {rel}")
    for rel in resynced:
        print(f"  ~ {rel}: {pinned[rel][0]} -> {actual[rel][0]}")
    raise SystemExit(0)

failures = []
warnings = []
for rel in sorted(actual):
    owned, _ = actual[rel]
    if rel not in pinned:
        failures.append(f"new mutex-bearing file not in allowlist: {rel} (owning declarations: {owned})")
        continue
    pinned_owned, families = pinned[rel]
    if owned > pinned_owned:
        failures.append(f"new owning mutex declaration(s) in {rel}: {owned} > pinned {pinned_owned} "
                        f"(declare via LockCensus::Mutex<Family::XX> and classify, or pin with a family)")
    elif owned < pinned_owned:
        failures.append(f"stale pin for {rel}: {owned} < pinned {pinned_owned} (re-sync with --fix)")
    if families == 'UNCLASSIFIED':
        warnings.append(f"{rel}: {owned} owning declaration(s) declared but UNCLASSIFIED — needs an inventory row")
for rel in order:
    if rel not in actual:
        failures.append(f"stale allowlist row (file gone or mutex-free): {rel} (re-sync with --fix)")

for message in warnings:
    print(f"WARN: {message}")
if failures:
    for message in failures:
        print(f"FAIL: {message}")
    raise SystemExit(1)

total_owned = sum(owned for owned, _ in actual.values())
print(f"PASS: no-new-mutex — scanned {scanned} files, {len(actual)} mutex-syntax files, "
      f"{total_owned} owning declarations, {len(warnings)} UNCLASSIFIED row(s)")
PY
