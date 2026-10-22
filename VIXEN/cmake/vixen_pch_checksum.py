#!/usr/bin/env python3
"""Run a GCC precompiled-header compile and write <pch>.gch.sum beside it.

GCC's .gch bytes differ between identical compiles (address-space randomisation and the
environment end up in the dump), and ccache keys every consumer TU on those bytes, so one
recompiled PCH turned all of its consumers into cache misses (184 for rmlui). With
CCACHE_PCH_EXTSUM set, ccache hashes <pch>.gch.sum instead. This writes that file as a hash of
what decides the PCH: the compiler version, the flags and the preprocessed header text, with the
build's own -f*-prefix-map rewrites applied so every worktree computes the same sum.

Usage: vixen_pch_checksum.py <launcher and compiler command...>
"""

from __future__ import annotations

import hashlib
import os
import subprocess
import sys

# Arguments whose value names a dependency or output file, not anything the PCH contains.
_OUTPUT_VALUE_FLAGS = {"-o", "-MF", "-MT", "-MQ"}
_OUTPUT_FLAGS = {"-c", "-MD", "-MMD"}


def prefix_maps(args: list[str]) -> list[tuple[bytes, bytes]]:
    maps = {}
    for arg in args:
        for flag in ("-ffile-prefix-map=", "-fmacro-prefix-map=", "-fdebug-prefix-map="):
            if arg.startswith(flag) and "=" in arg[len(flag):]:
                old, new = arg[len(flag):].split("=", 1)
                maps[old.encode()] = new.encode()
    return sorted(maps.items(), key=lambda item: len(item[0]), reverse=True)


def normalise(data: bytes, maps: list[tuple[bytes, bytes]]) -> bytes:
    for old, new in maps:
        data = data.replace(old, new)
    return data


def main() -> int:
    command = sys.argv[1:]
    output = command[command.index("-o") + 1]
    checksum = output + ".sum"
    if os.path.exists(checksum):
        os.remove(checksum)
    result = subprocess.call(command)
    if result != 0:
        return result

    # The launcher (ccache) comes first; the compiler is the first argument that is not one.
    first = next(i for i, arg in enumerate(command)
                 if os.path.basename(arg).split(".")[0] != "ccache")
    compiler, args = command[first], command[first + 1:]
    preprocess = [compiler]
    skip = False
    for arg in args:
        if skip:
            skip = False
        elif arg in _OUTPUT_VALUE_FLAGS:
            skip = True
        elif arg not in _OUTPUT_FLAGS:
            preprocess.append(arg)
    maps = prefix_maps(args)
    digest = hashlib.sha256()
    digest.update(subprocess.run([compiler, "--version"], check=True, capture_output=True).stdout)
    for arg in preprocess[1:]:
        digest.update(normalise(arg.encode(), maps) + b"\0")
    text = subprocess.run(preprocess + ["-E", "-o", "-"], check=True, capture_output=True).stdout
    digest.update(normalise(text, maps))
    with open(checksum, "w", encoding="ascii", newline="\n") as handle:
        handle.write(digest.hexdigest() + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
