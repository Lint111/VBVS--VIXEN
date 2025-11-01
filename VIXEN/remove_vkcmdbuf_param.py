#!/usr/bin/env python3
"""Remove VkCommandBuffer parameter from ExecuteImpl methods."""

import re
import glob

def process_file(filepath):
    """Remove VkCommandBuffer parameter from file."""
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()

    original = content

    # Header files: void ExecuteImpl(VkCommandBuffer ...) override; → void ExecuteImpl() override;
    content = re.sub(
        r'void ExecuteImpl\(VkCommandBuffer\s+\w+\)\s*override;',
        r'void ExecuteImpl() override;',
        content
    )

    # Cpp files: void ClassName::ExecuteImpl(VkCommandBuffer ...) → void ClassName::ExecuteImpl()
    content = re.sub(
        r'void (\w+)::ExecuteImpl\(VkCommandBuffer\s+\w+\)',
        r'void \1::ExecuteImpl()',
        content
    )

    if content != original:
        with open(filepath, 'w', encoding='utf-8') as f:
            f.write(content)
        return True
    return False

def main():
    base = r"C:\cpp\VBVS--VIXEN\VIXEN\RenderGraph"

    # Process all header files
    for filepath in glob.glob(f"{base}/include/Nodes/*.h"):
        if process_file(filepath):
            print(f"[OK] Updated {filepath.split('\\')[-1]}")

    # Process all cpp files
    for filepath in glob.glob(f"{base}/src/Nodes/*.cpp"):
        if process_file(filepath):
            print(f"[OK] Updated {filepath.split('\\')[-1]}")

if __name__ == "__main__":
    main()
