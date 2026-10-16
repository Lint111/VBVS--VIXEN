#!/usr/bin/env python3
"""Compile the RmlUi UI shaders and regenerate their checked-in SPIR-V header."""

from __future__ import annotations

import argparse
import os
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[5]
SHADER_DIR = ROOT / "libraries/RenderGraph/src/Ui/shaders"
OUTPUT = ROOT / "libraries/RenderGraph/include/Ui/UiShaderSpirv.g.h"


def find_glslc() -> str:
    if found := shutil.which("glslc"):
        return found
    sdk = os.environ.get("VULKAN_SDK")
    if sdk:
        for candidate in (Path(sdk) / "bin/glslc", Path(sdk) / "Bin/glslc.exe"):
            if candidate.is_file():
                return str(candidate)
    candidates = [
        ROOT / ".vulkan-sdk/1.4.350.1/x86_64/bin/glslc",
        Path.home() / ".cache/vixen/vulkan/1.4.350.1/x86_64/bin/glslc",
    ]
    for candidate in candidates:
        if candidate.is_file():
            return str(candidate)
    raise RuntimeError("glslc was not found on PATH, in VULKAN_SDK, or in the VIXEN SDK cache")


def words_from_spirv(path: Path) -> list[int]:
    data = path.read_bytes()
    if len(data) % 4:
        raise RuntimeError(f"SPIR-V byte count is not divisible by four: {path}")
    return list(struct.unpack(f"<{len(data) // 4}I", data))


def format_array(name: str, words: list[int]) -> str:
    lines = [f"inline constexpr uint32_t {name}[] = {{"]
    for start in range(0, len(words), 8):
        lines.append("    " + ",".join(f"0x{word:08x}u" for word in words[start : start + 8]) + ",")
    lines.append("};")
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--glslc", help="path to glslc; defaults to the PATH/VIXEN SDK search")
    args = parser.parse_args()
    glslc = args.glslc or find_glslc()

    with tempfile.TemporaryDirectory(prefix="vixen-ui-shader-") as temp_dir:
        temp = Path(temp_dir)
        vert_spv = temp / "ui.vert.spv"
        frag_spv = temp / "ui.frag.spv"
        subprocess.run([glslc, str(SHADER_DIR / "ui.vert"), "-o", str(vert_spv)], check=True)
        subprocess.run([glslc, str(SHADER_DIR / "ui.frag"), "-o", str(frag_spv)], check=True)
        header = "\n".join(
            [
                "// AUTO-GENERATED from src/Ui/shaders/ui.vert + ui.frag via glslc.",
                "// Regenerate: python3 libraries/RenderGraph/src/Ui/shaders/regenerate-ui-shader-header.py",
                "#pragma once",
                "#include <cstdint>",
                "",
                "namespace Vixen::Ui {",
                format_array("kUiVertSpirv", words_from_spirv(vert_spv)),
                format_array("kUiFragSpirv", words_from_spirv(frag_spv)),
                "} // namespace Vixen::Ui",
                "",
            ]
        )
        OUTPUT.write_text(header, encoding="utf-8")


if __name__ == "__main__":
    main()
