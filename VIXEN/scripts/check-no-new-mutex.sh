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
#   --fix rewrites the allowlist from the tree: counts re-synced, new files appended (a file that owns no
#         mutex and acquires SubmitMutex() is classified VK2-borrowed; any other new file is UNCLASSIFIED),
#         stale rows dropped, existing family IDs preserved. The diff IS the declaration to review.
#
# The acquisition index (R661.3, T-2939): the checker also generates the lock-inventory acquisition index,
# Vixen-Docs/01-Architecture/runtime-lock-index.g.md, from the same source scan plus the allowlist's
# families: one row per census file (sorted), each acquisition site named by its enclosing symbol and lock
# alias — never a line number, so unrelated edits do not move it.
#   FAIL  the generated index differs from the tree/allowlist     (stale — re-sync with --fix)
#   --fix rewrites the index together with the allowlist.
#
# usage: check-no-new-mutex.sh [--root REPOSITORY_ROOT] [--allowlist FILE] [--index FILE] [--fix]
set -euo pipefail
export LC_ALL=C

usage() {
    cat >&2 <<'EOF'
usage: check-no-new-mutex.sh [--root REPOSITORY_ROOT] [--allowlist FILE] [--index FILE] [--fix]

Fails if VIXEN/libraries or VIXEN/application gains a mutex declaration that is not pinned in
scripts/no-new-mutex.allowlist (or self-declared via Core/LockCensus.h), or if the generated
acquisition index (Vixen-Docs/01-Architecture/runtime-lock-index.g.md) is stale. --fix re-syncs the
allowlist and the index from the tree (new rows are appended as UNCLASSIFIED and reported as WARN
until classified).
EOF
}

ROOT_ARG=""
ALLOWLIST_ARG=""
INDEX_ARG=""
FIX=0
while (($# > 0)); do
    case "$1" in
        --root)
            if (($# < 2)); then echo "ERROR: --root requires a repository path" >&2; usage; exit 2; fi
            ROOT_ARG="$2"; shift 2 ;;
        --allowlist)
            if (($# < 2)); then echo "ERROR: --allowlist requires a file path" >&2; usage; exit 2; fi
            ALLOWLIST_ARG="$2"; shift 2 ;;
        --index)
            if (($# < 2)); then echo "ERROR: --index requires a file path" >&2; usage; exit 2; fi
            INDEX_ARG="$2"; shift 2 ;;
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
INDEX="${INDEX_ARG:-$ROOT/VIXEN/Vixen-Docs/01-Architecture/runtime-lock-index.g.md}"

python3 - "$ROOT" "$ALLOWLIST" "$FIX" "$INDEX" <<'PY'
import bisect
import re
import sys
from pathlib import Path

root = Path(sys.argv[1])
allowlist_path = Path(sys.argv[2])
fix = sys.argv[3] == "1"
index_path = Path(sys.argv[4])

EXTENSIONS = {'.h', '.hpp', '.hxx', '.cpp', '.c', '.cc', '.cxx', '.inl', '.ipp', '.ixx'}
SCAN_ROOTS = (root / 'VIXEN' / 'libraries', root / 'VIXEN' / 'application')

# The inventory's census lexer: blank out comments and string/char literals, keep line structure.
LEX = re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')
SYNTAX = re.compile(r'\bstd::(?:mutex|recursive_mutex|shared_mutex|shared_timed_mutex|'
                    r'timed_mutex|lock_guard|scoped_lock|unique_lock|shared_lock)\b')
MUTEX_TYPE = re.compile(r'\bstd::(?:mutex|recursive_mutex|shared_mutex|shared_timed_mutex|timed_mutex)\b')
GUARD_OPENER = re.compile(r'(?:lock_guard|unique_lock|shared_lock|scoped_lock)\s*<\s*$')
SUBMIT_BORROW = re.compile(r'\bSubmitMutex\s*\(')
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


# --- Acquisition index: enclosing symbol + lock alias per mutex-syntax site (no line numbers) ---
SITE = re.compile(r'\bstd::(?:mutex|recursive_mutex|shared_mutex|shared_timed_mutex|timed_mutex|'
                  r'lock_guard|scoped_lock|unique_lock|shared_lock)\b|\bLockCensus::(?:Mutex|SharedMutex)\s*<')
GUARD_TYPES = ('lock_guard', 'scoped_lock', 'unique_lock', 'shared_lock')
BRACE_EVENT = re.compile(r'[{};]')
TYPE_SCOPE = re.compile(r'^(?:template\s*<[^{};]*?>\s*)?(?:(?:typedef|export|inline)\s+)*'
                        r'(class|struct|union|enum(?:\s+(?:class|struct))?|namespace)\b\s*'
                        r'(?:alignas\s*\([^)]*\)\s*)?(?:\[\[[^\]]*\]\]\s*)?((?:\w+::)*\w+)?')
INIT_BRACE = re.compile(r'(?<!:)[:,](?!:)\s*[\w:<>~]+\s*$')
LAMBDA_TAIL = re.compile(r'\]\s*(?:\([^()]*\))?\s*(?:mutable\s*|noexcept\s*|->\s*[\w:<>&*\s]+)*$')
OPERATOR_NAME = re.compile(r'((?:~?\w+(?:<[^<>]*>)?::)*operator\s*(?:\(\s*\)|\[\s*\]|[^\s\w(]+|\w+))\s*\(')
NOT_A_FUNCTION = {'if', 'for', 'while', 'switch', 'catch', 'else', 'do', 'try', 'return', 'sizeof',
                  'decltype', 'alignas', 'noexcept', 'requires'}
FILE_SCOPE = '(file scope)'


def clean_header(text: str) -> str:
    text = re.sub(r'(?m)^[ \t]*#.*$', ' ', text)
    text = re.sub(r'\b(?:public|private|protected)\s*:(?!:)', ' ', text)
    return ' '.join(text.split())


def function_name(header: str):
    header = re.sub(r'^template\s*<[^()]*?>\s*', '', header)
    operator = OPERATOR_NAME.search(header)
    if operator:
        return ''.join(operator.group(1).split())
    open_paren = header.find('(')
    if open_paren < 0 or LAMBDA_TAIL.search(header):
        return None
    pre = header[:open_paren].rstrip()
    if pre.endswith(']'):
        return None
    previous = None
    while previous != pre:
        previous = pre
        pre = re.sub(r'<[^<>]*>', '', pre)
    name = re.search(r'~?\w+(?:::~?\w+)*$', pre)
    if not name or name.group(0) in NOT_A_FUNCTION:
        return None
    return name.group(0)


def classify_scope(header: str):
    if '(' not in header:
        scope = TYPE_SCOPE.match(header)
        if scope:
            return ('namespace' if scope.group(1) == 'namespace' else 'type'), scope.group(2)
    if '->' not in header and INIT_BRACE.search(header):
        return 'init', None
    name = function_name(header)
    return ('function', name) if name else ('anonymous', None)


def scope_timeline(source: str):
    """(positions, scope names): the enclosing class/function chain after each brace/semicolon event."""
    stack = []
    header_start = 0
    positions = []
    scopes = []
    for event in BRACE_EVENT.finditer(source):
        mark = event.group()
        if mark == '{':
            stack.append(classify_scope(clean_header(source[header_start:event.start()])))
            header_start = event.end()
        elif mark == '}':
            kind = stack.pop()[0] if stack else 'anonymous'
            if kind != 'init':  # a constructor's brace-init stays part of its header
                header_start = event.end()
        else:
            header_start = event.end()
        names = [name for kind, name in stack if kind in ('type', 'function') and name]
        positions.append(event.start())
        scopes.append('::'.join(names) if names else FILE_SCOPE)
    return positions, scopes


def scope_at(timeline, offset: int) -> str:
    positions, scopes = timeline
    at = bisect.bisect_left(positions, offset) - 1
    return scopes[at] if at >= 0 else FILE_SCOPE


def balanced(source: str, start: int, opener: str, closer: str):
    """Text between the bracket at source[start] and its partner (bounded), plus the index after it."""
    depth = 0
    for index in range(start, min(len(source), start + 400)):
        if source[index] == opener:
            depth += 1
        elif source[index] == closer:
            depth -= 1
            if depth == 0:
                return source[start + 1:index], index + 1
    return None, start + 1


def tidy(text: str) -> str:
    text = ' '.join(text.replace('`', "'").split()).replace('|', '\\|')
    return text if len(text) <= 120 else text[:117] + '...'


def lock_alias(source: str, match) -> tuple:
    """(alias, end offset consumed): the guard's lock expression, or the declared lock's name."""
    token = match.group()
    index = match.end()
    if token.startswith('LockCensus::'):
        template, after = balanced(source, index - 1, '<', '>')
        name = re.match(r'\s*(\w+)', source[after:])
        kind = token.split('::')[1].rstrip(' <').strip()
        label = f"{kind}<{' '.join((template or '').split())}>"
        return (f"{label} {name.group(1)}" if name else label), after
    short = token[len('std::'):]
    if short in GUARD_TYPES:
        end = index
        angle = re.match(r'\s*<', source[index:])
        if angle:
            _, end = balanced(source, index + angle.end() - 1, '<', '>')
        declarator = re.match(r'\s*(?:[&*]\s*)*(\w+)?\s*', source[end:])
        name = declarator.group(1)
        bracket = end + declarator.end()
        if bracket < len(source) and source[bracket] in '({':
            closer = ')' if source[bracket] == '(' else '}'
            argument, after = balanced(source, bracket, source[bracket], closer)
            if argument is not None and argument.strip():
                return f"{short}({tidy(argument)})", max(end, after)
        return (f"{short} {name}" if name else short), end
    declarator = re.match(r'\s*((?:[&*]\s*)*)(\w+)?', source[index:])
    name = declarator.group(2)
    if not name or name in ('const', 'operator'):
        return short, index
    qualifier = declarator.group(1).replace(' ', '')
    return f"{short}{qualifier} {name}", index


def acquisition_sites(source: str) -> list:
    timeline = scope_timeline(source)
    sites = set()
    consumed = -1
    for match in SITE.finditer(source):
        if match.start() < consumed:
            continue  # a guard's own template argument
        alias, consumed = lock_alias(source, match)
        sites.add((scope_at(timeline, match.start()), alias))
    return sorted(sites)


def render_index(rows, sites_by_file) -> str:
    """rows: sorted (relative path, owned, families); the whole file is a function of tree + allowlist."""
    out = [
        "<!-- GENERATED by VIXEN/scripts/check-no-new-mutex.sh (--fix rewrites it; the check fails when it is stale). Do not edit. -->",
        "# Runtime lock acquisition index (generated)",
        "",
        "One row per non-test file under `libraries/` and `application/` that carries std mutex/guard syntax "
        "(the census lexer of `2026-09-08-runtime-lock-inventory-and-classification.md`). Families come from "
        "`scripts/no-new-mutex.allowlist`; sites are `enclosing symbol -> lock alias` and carry no line numbers, "
        "so an unrelated edit does not move them. An alias is a declared lock (`mutex m_mutex`, `mutex& m`), a guard over "
        "its lock expression (`lock_guard(m_mutex)`), or a guard parameter/variable (`unique_lock lk`).",
        "",
        f"{len(rows)} files, {sum(owned for _, owned, _ in rows)} owning declarations.",
        "",
        "| Source file | Owned | Families | Acquisition sites (symbol -> lock alias) |",
        "|---|---|---|---|",
    ]
    for rel, owned, families in rows:
        display = rel[len('VIXEN/'):] if rel.startswith('VIXEN/') else rel
        cell = '<br>'.join(f"`{scope}` -> `{alias}`" for scope, alias in sites_by_file[rel])
        out.append(f"| `{display}` | {owned} | {families.replace(',', ', ')} | {cell} |")
    return '\n'.join(out) + '\n'


scanned = 0
actual = {}  # relative path -> (owned, syntax_lines)
sites_by_file = {}  # relative path -> sorted (symbol, lock alias) acquisition sites
borrows_submit = set()  # files that take VulkanDevice::SubmitMutex() (the VK2 family)
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
        sites_by_file[rel] = acquisition_sites(source)
        if SUBMIT_BORROW.search(source):
            borrows_submit.add(rel)

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
        if rel in pinned:
            families = pinned[rel][1]
        elif owned == 0 and rel in borrows_submit:
            families = 'VK2-borrowed'
        else:
            families = 'UNCLASSIFIED'
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
    index_path.parent.mkdir(parents=True, exist_ok=True)
    index_path.write_text(render_index(rows, sites_by_file), encoding='utf-8', newline='\n')
    added = sorted(set(actual) - set(pinned))
    dropped = sorted(set(pinned) - set(actual))
    resynced = sorted(rel for rel in actual if rel in pinned and pinned[rel][0] != actual[rel][0])
    print(f"fixed {index_path}: {len(rows)} files")
    print(f"fixed {allowlist_path}: {len(rows)} rows; added={len(added)} dropped={len(dropped)} resynced={len(resynced)}")
    for rel in added:
        if rel in borrows_submit and actual[rel][0] == 0:
            print(f"  + {rel} (VK2-borrowed: derived from its SubmitMutex() acquisition)")
        else:
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

expected_rows = [(rel, actual[rel][0], pinned[rel][1] if rel in pinned else 'UNCLASSIFIED') for rel in sorted(actual)]
expected_index = render_index(expected_rows, sites_by_file)
if not index_path.is_file():
    failures.append(f"acquisition index missing: {index_path} (generate it with --fix)")
elif index_path.read_bytes().decode('utf-8') != expected_index:
    failures.append(f"stale acquisition index: {index_path} (re-sync with --fix)")

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
