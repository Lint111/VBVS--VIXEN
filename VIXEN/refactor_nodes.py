#!/usr/bin/env python3
"""
Script to refactor all node files to use template method pattern.
Renames Setup/Compile/Execute to SetupImpl/CompileImpl/ExecuteImpl
and removes VkCommandBuffer parameter from ExecuteImpl.
"""

import re
import os

NODES = [
    "DepthBufferNode",
    "VertexBufferNode",
    "CommandPoolNode",
    "RenderPassNode",
    "WindowNode",
    "FramebufferNode",
    "PresentNode",
    "TextureLoaderNode",
    "ShaderLibraryNode",
    "GraphicsPipelineNode",
    "DeviceNode",
    "DescriptorSetNode",
    "SwapChainNode",
    "FrameSyncNode",
    "GeometryRenderNode",
]

def refactor_cpp_file(filepath):
    """Refactor .cpp file: rename methods and remove RegisterCleanup() calls."""
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()

    original = content

    # Rename Setup() -> SetupImpl()
    content = re.sub(r'void (\w+)::Setup\(\)', r'void \1::SetupImpl()', content)

    # Rename Compile() -> CompileImpl()
    content = re.sub(r'void (\w+)::Compile\(\)', r'void \1::CompileImpl()', content)

    # Rename Execute(VkCommandBuffer ...) -> ExecuteImpl()
    content = re.sub(
        r'void (\w+)::Execute\(VkCommandBuffer\s+\w+\)',
        r'void \1::ExecuteImpl()',
        content
    )

    # Remove RegisterCleanup() calls (with optional whitespace/comments)
    content = re.sub(r'\s*RegisterCleanup\(\);?\s*(?://.*)?', '', content)

    if content != original:
        with open(filepath, 'w', encoding='utf-8') as f:
            f.write(content)
        return True
    return False

def refactor_header_file(filepath):
    """Refactor .h file: move methods to protected and rename."""
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()

    original = content

    # Pattern to match the public lifecycle methods section
    # Looking for: void Setup() override; void Compile() override; void Execute(...) override;
    pattern = r'(\s*)void Setup\(\) override;(\s*)void Compile\(\) override;(\s*)void Execute\(VkCommandBuffer\s+\w+\) override;'

    replacement = r'''protected:
    // Template method pattern - override *Impl() methods
    void SetupImpl() override;
    void CompileImpl() override;
    void ExecuteImpl() override;'''

    content = re.sub(pattern, replacement, content)

    # Also handle cases where Setup might be missing
    pattern2 = r'(\s*)void Compile\(\) override;(\s*)void Execute\(VkCommandBuffer\s+\w+\) override;'
    replacement2 = r'''protected:
    // Template method pattern - override *Impl() methods
    void CompileImpl() override;
    void ExecuteImpl() override;'''

    if 'SetupImpl' not in content:
        content = re.sub(pattern2, replacement2, content)

    # Remove duplicate protected: keywords if they exist
    content = re.sub(r'(protected:\s*//[^\n]*\n\s*void \w+Impl.*?)\s*protected:', r'\1', content, flags=re.DOTALL)

    if content != original:
        with open(filepath, 'w', encoding='utf-8') as f:
            f.write(content)
        return True
    return False

def main():
    base_path = r"C:\cpp\VBVS--VIXEN\VIXEN\RenderGraph"

    for node_name in NODES:
        cpp_file = os.path.join(base_path, "src", "Nodes", f"{node_name}.cpp")
        h_file = os.path.join(base_path, "include", "Nodes", f"{node_name}.h")

        cpp_changed = False
        h_changed = False

        if os.path.exists(cpp_file):
            cpp_changed = refactor_cpp_file(cpp_file)

        if os.path.exists(h_file):
            h_changed = refactor_header_file(h_file)

        if cpp_changed or h_changed:
            print(f"✓ Refactored {node_name} (cpp={cpp_changed}, h={h_changed})")
        else:
            print(f"  Skipped {node_name} (no changes needed)")

if __name__ == "__main__":
    main()
