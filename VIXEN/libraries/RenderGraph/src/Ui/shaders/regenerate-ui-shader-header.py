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
TEXT_SHADER_DIR = ROOT / "libraries/Text/src/shaders"
TEXT_OUTPUT = ROOT / "libraries/Text/include/Text/TextShaderSpirv.g.h"
DEFAULT_TEXT_INTERFACE_DIR = Path(os.environ.get("UNDERTOW_ROOT", "")) / "transpiled/textshader/Text"


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
    parser.add_argument("--ui-output", type=Path, default=OUTPUT,
                        help="destination for the generated UI shader header")
    parser.add_argument("--text-output", type=Path, default=TEXT_OUTPUT,
                        help="destination for the generated text shader header")
    parser.add_argument("--text-interface-dir", type=Path, default=DEFAULT_TEXT_INTERFACE_DIR,
                        help="directory containing Undertow-generated Text.g.h and text.g.glsl")
    parser.add_argument("--check", action="store_true", help="compile and fail if either generated header is stale")
    args = parser.parse_args()
    text_interface_dir = args.text_interface_dir.resolve()
    text_glsl = text_interface_dir / "text.g.glsl"
    text_header = text_interface_dir / "Text.g.h"
    if not text_glsl.is_file() or not text_header.is_file():
        raise SystemExit(
            "generated text shader interface is missing under "
            f"{text_interface_dir}; set UNDERTOW_ROOT or pass --text-interface-dir"
        )
    glslc = args.glslc or find_glslc()

    with tempfile.TemporaryDirectory(prefix="vixen-ui-shader-") as temp_dir:
        temp = Path(temp_dir)
        vert_spv = temp / "ui.vert.spv"
        frag_spv = temp / "ui.frag.spv"
        text_vert_spv = temp / "text.vert.spv"
        text_frag_spv = temp / "text.frag.spv"
        subprocess.run([glslc, str(SHADER_DIR / "ui.vert"), "-o", str(vert_spv)], check=True)
        subprocess.run([glslc, str(SHADER_DIR / "ui.frag"), "-o", str(frag_spv)], check=True)
        subprocess.run([glslc, "-I", str(text_interface_dir), "-I", str(TEXT_SHADER_DIR),
                        str(TEXT_SHADER_DIR / "text.vert"),
                        "-o", str(text_vert_spv)], check=True)
        subprocess.run([glslc, "-I", str(text_interface_dir), "-I", str(TEXT_SHADER_DIR),
                        str(TEXT_SHADER_DIR / "text.frag"),
                        "-o", str(text_frag_spv)], check=True)
        ui_header = "\n".join(
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
        text_header = "\n".join(
            [
                "// AUTO-GENERATED from libraries/Text/src/shaders/text.vert + text.frag via glslc.",
                "// Regenerate: python3 libraries/RenderGraph/src/Ui/shaders/regenerate-ui-shader-header.py",
                "#pragma once",
                "#include <cstdint>",
                "",
                "namespace Vixen::Text {",
                format_array("kTextVertSpirv", words_from_spirv(text_vert_spv)),
                format_array("kTextFragSpirv", words_from_spirv(text_frag_spv)),
                "} // namespace Vixen::Text",
                "",
            ]
        )
        generated = ((args.ui_output, ui_header), (args.text_output, text_header))
        if args.check:
            stale = [str(path) for path, contents in generated
                     if not path.is_file() or path.read_text(encoding="utf-8") != contents]
            if stale:
                raise SystemExit("generated SPIR-V headers are stale: " + ", ".join(stale))
        else:
            for path, contents in generated:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(contents, encoding="utf-8")


if __name__ == "__main__":
    main()
