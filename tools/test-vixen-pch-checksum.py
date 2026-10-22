#!/usr/bin/env python3
"""Check VIXEN/cmake/vixen_pch_checksum.py: the PCH sum follows the header text and flags, and
does not depend on where the tree lives (the build's prefix maps are applied). Needs g++."""
from pathlib import Path
import subprocess
import sys
import tempfile

script = Path(__file__).resolve().parents[1] / "VIXEN" / "cmake" / "vixen_pch_checksum.py"


def pch_sum(root, *flags):
    command = [sys.executable, str(script), "g++", "-O2", *flags, f"-ffile-prefix-map={root}=/vixen",
               "-I", str(root / "inc"), "-x", "c++-header", "-include", str(root / "inc" / "pch.h"),
               "-o", str(root / "pch.h.gch"), "-c", str(root / "pch.cxx")]
    subprocess.run(command, check=True)
    return (root / "pch.h.gch.sum").read_text()


with tempfile.TemporaryDirectory() as temp:
    trees = [Path(temp) / "a", Path(temp) / "longer" / "b"]
    for tree in trees:
        (tree / "inc").mkdir(parents=True)
        (tree / "inc" / "pch.h").write_text('#include "dep.h"\n#include <vector>\n')
        (tree / "inc" / "dep.h").write_text("inline int dep() { return 1; }\n")
        (tree / "pch.cxx").write_text("")
    first, moved = pch_sum(trees[0]), pch_sum(trees[1])
    assert first == moved, "the sum depends on the tree location"
    assert pch_sum(trees[1], "-DEXTRA=1") != moved, "the sum ignores a define"
    (trees[0] / "inc" / "dep.h").write_text("inline int dep() { return 2; }\n")
    assert pch_sum(trees[0]) != first, "the sum ignores an included header"
print("vixen_pch_checksum: OK")
