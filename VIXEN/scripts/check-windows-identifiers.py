#!/usr/bin/env python3
"""Purpose: catch declarations that Windows SDK macros rewrite before MSVC sees them.

Replaces: the late native-Windows compile-only discovery of names such as ``near`` and ``max``.
Saves: a fast source scan during every configured build and from pre-commit/CI.
Usage: ``python3 VIXEN/scripts/check-windows-identifiers.py --root .``
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


# Keep this list deliberately conservative: these are identifiers that are either macros or
# macro-like names supplied by the Windows SDK headers.  A project identifier with one of these
# exact spellings is not portable even when the current Linux include graph never imports Windows.h.
WINDOWS_IDENTIFIERS = frozenset(
    {
        "near",
        "far",
        "min",
        "max",
        "small",
        "interface",
        "ERROR",
        "DELETE",
        "GetMessage",
        "CreateWindow",
        "SendMessage",
        "MessageBox",
        "Yield",
        "IN",
        "OUT",
        "OPTIONAL",
        "TRUE",
        "FALSE",
        "NULL",
    }
)

SOURCE_SUFFIXES = frozenset({".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"})
SKIP_PARTS = frozenset({"third_party", "build", "archive", "generated", "Vixen-Docs"})

_identifier = r"[A-Za-z_][A-Za-z0-9_]*"
_type_name = rf"{_identifier}(?:::{_identifier})*(?:\s*<[^;(){{}}]*>)?"
_declaration = re.compile(
    rf"(?<![A-Za-z0-9_])(?!(?:return|if|while|for|switch|case|throw|sizeof|decltype)\b)"
    rf"(?:const\s+|static\s+|constexpr\s+|volatile\s+|mutable\s+|"
    rf"inline\s+|unsigned\s+|signed\s+|short\s+|long\s+|typename\s+)*"
    rf"{_type_name}\s+(?:const\s+)?[*&]?\s*(?<![A-Za-z0-9_])(?P<name>{'|'.join(map(re.escape, sorted(WINDOWS_IDENTIFIERS, key=len, reverse=True)))})"
    rf"(?![A-Za-z0-9_])"
)
_type_alias = re.compile(
    rf"\b(?:using|typedef|struct|class|enum)\s+(?P<name>{'|'.join(map(re.escape, sorted(WINDOWS_IDENTIFIERS, key=len, reverse=True)))})\b"
)
_macro_definition = re.compile(
    rf"^\s*#\s*define\s+(?P<name>{'|'.join(map(re.escape, sorted(WINDOWS_IDENTIFIERS, key=len, reverse=True)))})\b"
)


def _blank(value: str) -> str:
    """Mask comments and literals while preserving line numbers and source columns."""

    output = list(value)
    i = 0
    state = "code"
    raw_end = ""
    while i < len(value):
        if state == "line_comment":
            if value[i] == "\n":
                state = "code"
            else:
                output[i] = " "
            i += 1
            continue
        if state == "block_comment":
            if value.startswith("*/", i):
                output[i] = output[i + 1] = " "
                i += 2
                state = "code"
            else:
                if value[i] != "\n":
                    output[i] = " "
                i += 1
            continue
        if state in {"string", "char"}:
            if value[i] == "\\":
                output[i] = " "
                if i + 1 < len(value):
                    if value[i + 1] != "\n":
                        output[i + 1] = " "
                    i += 2
                else:
                    i += 1
            elif (state == "string" and value[i] == '"') or (state == "char" and value[i] == "'"):
                output[i] = " "
                i += 1
                state = "code"
            else:
                if value[i] != "\n":
                    output[i] = " "
                i += 1
            continue
        if state == "raw":
            if value.startswith(raw_end, i):
                for j in range(i, i + len(raw_end)):
                    output[j] = " "
                i += len(raw_end)
                state = "code"
            else:
                if value[i] != "\n":
                    output[i] = " "
                i += 1
            continue

        if value.startswith("//", i):
            output[i] = output[i + 1] = " "
            i += 2
            state = "line_comment"
        elif value.startswith("/*", i):
            output[i] = output[i + 1] = " "
            i += 2
            state = "block_comment"
        elif value[i] in {'"', "'"}:
            state = "string" if value[i] == '"' else "char"
            output[i] = " "
            i += 1
        elif value[i] == "R" and i + 1 < len(value) and value[i + 1] == '"':
            opening = value.find("(", i + 2)
            if opening != -1 and "\n" not in value[i + 2 : opening]:
                delimiter = value[i + 2 : opening]
                raw_end = ")" + delimiter + '"'
                for j in range(i, opening + 1):
                    output[j] = " "
                i = opening + 1
                state = "raw"
            else:
                i += 1
        else:
            i += 1
    return "".join(output)


def _source_files(root: Path) -> list[Path]:
    return sorted(
        path
        for path in root.rglob("*")
        if path.is_file()
        and path.suffix in SOURCE_SUFFIXES
        and not any(part in SKIP_PARTS for part in path.relative_to(root).parts)
    )


def find_collisions(root: Path) -> list[tuple[Path, int, str, str]]:
    collisions: list[tuple[Path, int, str, str]] = []
    for path in _source_files(root):
        text = path.read_text(encoding="utf-8", errors="replace")
        for line_number, line in enumerate(_blank(text).splitlines(), start=1):
            stripped = line.lstrip()
            match = None
            if not stripped.startswith("#"):
                match = _declaration.search(line) or _type_alias.search(line)
            else:
                match = _macro_definition.search(line)
            if match:
                name = match.group("name")
                original = text.splitlines()[line_number - 1].strip()
                collisions.append((path, line_number, name, original))
    return collisions


def _self_test() -> None:
    safe = _blank('const char* x = "float near"; // const int far;')
    assert "near" not in safe and "far" not in safe
    assert len(find_collisions_from_text("const float near = 1;\n")) == 1
    assert not find_collisions_from_text('const char* x = R"(int near)";\n')


def find_collisions_from_text(text: str) -> list[tuple[int, str]]:
    collisions = []
    for line_number, line in enumerate(_blank(text).splitlines(), start=1):
        stripped = line.lstrip()
        match = None
        if not stripped.startswith("#"):
            match = _declaration.search(line) or _type_alias.search(line)
        else:
            match = _macro_definition.search(line)
        if match:
            collisions.append((line_number, match.group("name")))
    return collisions


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)
    if args.self_test:
        _self_test()
        print("check-windows-identifiers self-test: PASS")
        return 0

    root = args.root.resolve()
    collisions = find_collisions(root / "VIXEN" if (root / "VIXEN").is_dir() else root)
    if collisions:
        for path, line_number, name, line in collisions:
            print(f"{path.relative_to(root)}:{line_number}: {name}: {line}")
        print(f"check-windows-identifiers: FAIL ({len(collisions)} collision(s))", file=sys.stderr)
        return 1
    print("check-windows-identifiers: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
