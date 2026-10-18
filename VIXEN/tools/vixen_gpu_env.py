"""Vulkan loader environment that VixenTesting.cmake gives CTest GPU tests, read from a build's CMakeCache."""

from __future__ import annotations

import os
import pathlib


def cache_values(build_dir: pathlib.Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in (build_dir / "CMakeCache.txt").read_text(errors="replace").splitlines():
        key, separator, value = line.partition("=")
        if separator and not line.startswith(("#", "//")):
            values[key.split(":", 1)[0]] = value
    return values


def gpu_test_environment(app: pathlib.Path) -> dict[str, str]:
    build_dir = next((parent for parent in app.resolve().parents if (parent / "CMakeCache.txt").is_file()), None)
    if build_dir is None:
        raise RuntimeError(f"no CMakeCache.txt above {app}; cannot find the build's Vulkan environment")
    cache = cache_values(build_dir)
    environment: dict[str, str] = {}
    if cache.get("VIXEN_WSL_DZN_ICD"):
        environment["VK_ICD_FILENAMES"] = cache["VIXEN_WSL_DZN_ICD"]
    if cache.get("VIXEN_VULKAN_LAYER_PATH"):
        environment["VK_LAYER_PATH"] = cache["VIXEN_VULKAN_LAYER_PATH"]

    architecture = cache.get("CMAKE_LIBRARY_ARCHITECTURE", "")
    windowing = cache.get("VIXEN_WINDOWING_CACHE_DIR", "")
    vulkan = cache.get("VULKAN_PATH", "")
    candidates = [cache.get("VIXEN_WSL_DZN_LIBRARY_DIR", "")]
    if windowing:
        candidates += [f"{windowing}/usr/lib/{architecture}", f"{windowing}/usr/lib"]
    if vulkan:
        candidates += [f"{vulkan}/lib/VulkanLoader/lib", f"{vulkan}/lib"]
    library_dirs: list[str] = []
    for candidate in candidates:
        if candidate and pathlib.Path(candidate).is_dir():
            normalized = os.path.normpath(candidate)
            if normalized not in library_dirs:
                library_dirs.append(normalized)
    if os.environ.get("LD_LIBRARY_PATH"):
        library_dirs.append(os.environ["LD_LIBRARY_PATH"])
    if library_dirs:
        environment["LD_LIBRARY_PATH"] = ":".join(library_dirs)
    return environment
