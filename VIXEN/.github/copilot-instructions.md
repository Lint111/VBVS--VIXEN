# Copilot Instructions for VIXEN

**VIXEN** is a learning-focused Vulkan graphics engine implementing a graph-based rendering architecture on Windows. Progressing through chapters toward full rendering pipeline integration.

## Critical Documentation

**ALWAYS read before starting work:**
1. `CLAUDE.md` - Project overview, build system, coding standards
2. `documentation/Communication Guidelines.md` - Mandatory radical conciseness style
3. `documentation/cpp-programming-guidelins.md` - C++23 standards (PascalCase classes, <20-instruction functions, smart pointers)

**For architecture context:**
- `memory-bank/projectbrief.md` - Goals, scope, current chapter (Device Handshake/RenderGraph)
- `memory-bank/systemPatterns.md` - Layered architecture, Singleton pattern, component responsibilities
- `documentation/EventBusArchitecture.md` - Event-driven invalidation between RenderGraph nodes

## Architecture Overview

**Three-tier system:**

```
┌─────────────────────────────────────────┐
│  Application Layer (VulkanApplication)  │
│  Singleton orchestrator & lifecycle mgmt│
└─────────────────┬───────────────────────┘
                  │
    ┌─────────────┼──────────────┬──────────────┐
    │             │              │              │
┌───▼──┐  ┌──────▼─────┐  ┌─────▼──────┐  ┌──▼──────────┐
│ Inst │  │   Device   │  │  RenderGraph   │  │ SwapChain  │
└──────┘  └────────────┘  └────────────┘  └────────────┘
  ↓            ↓                 ↓              ↓
Layers &   Physical/          Nodes &      Presentation
Extensions Logical Device     Compilation
```

**Key insight:** `VulkanApplication` is orchestrator only. It delegates to specialized subsystems. Each subsystem owns its Vulkan resources and follows RAII (acquisition = construction, release = destruction).

## RenderGraph System (Primary Focus)

**Node Type vs Node Instance:**
- `NodeType` = template/definition (e.g., `ShadowMapPass` type - ONE per type)
- `NodeInstance` = concrete usage in graph (e.g., `ShadowMap_Light0`, `ShadowMap_Light1` - MANY per type)

**Compilation Flow:**
```
AddNode() → Compile() → RecompileDirtyNodes() → Execute()
 (Instances) (Analyze)  (Events trigger)     (Command Buffers)
```

**EventBus Pattern:** Nodes emit events (WindowResize, ShaderReload, ResourceInvalidated) to EventBus. EventBus queues events. `ProcessEvents()` during safe points (between frames) triggers cascade recompilation of dependent nodes. Example: WindowResize → SwapChainNode emits SwapChainInvalidated → FramebufferNode marks dirty → recompiles.

**See:** `documentation/GraphArchitecture/` (node-system.md, graph-compilation.md, render-graph-quick-reference.md)

## Build & Development

**CMake-based Windows-only build (x64 + MSVC):**

```bash
# Generate
cmake -B build

# Build Debug
cmake --build build --config Debug

# Alternative: Open VS Solution
build/7e_ShadersWithSPIRV.sln
```

**Key CMake flags:**
- `AUTO_LOCATE_VULKAN=ON` (default) - Auto-detect Vulkan SDK
- `BUILD_SPV_ON_COMPILE_TIME=ON` - Runtime GLSL→SPIR-V (requires glslang libraries)

**Fallback manual path:** `C:/VulkanSDK/1.4.321.1`

**Output:** Executables → `binaries/`, Build artifacts → `build/`

## C++ Standards

**Mandatory:** C++23 + C23, no exceptions.

**Nomenclature:**
- Classes: `PascalCase` (e.g., `WindowNode`, `EventBus`)
- Methods/vars: `camelCase` (e.g., `processEvents()`, `eventQueue`)
- Constants: `ALL_CAPS` (e.g., `MAX_NODES`)
- Files: `snake_case` (e.g., `event_bus.h`)

**Function design:**
- Single responsibility, <20 instructions
- Use early returns to avoid nesting
- Prefer standard library algorithms over manual loops

**Memory management:**
- **Smart pointers ONLY:** `std::unique_ptr`, `std::shared_ptr` (never raw `new/delete`)
- **RAII principle:** Constructor acquires, destructor releases
- Use `std::optional` for nullable values instead of raw pointers

**Classes:**
- <200 instructions, <10 public methods, prefer composition over inheritance
- Private members with getters only when necessary
- Const-correctness on member functions

## Common Workflows

### Adding a New RenderGraph Node Type

1. **Create NodeType class** in `include/RenderGraph/Nodes/YourNodeType.h`:
   - Derive from `NodeType` base class
   - Implement `CreateInstance()` factory method
   - Define input/output ports, parameters schema

2. **Create NodeInstance execution** in `include/RenderGraph/Nodes/YourNode.h`:
   - Override `Compile()` (validates, creates VkPipeline, descriptor sets)
   - Override `Execute()` (records command buffer)
   - Override `OnEvent()` (handle EventBus events for invalidation)

3. **Register in NodeTypeRegistry:**
   - Add instance creation in registry setup
   - See `RenderGraphUsageExample.cpp` for pattern

4. **Example files:** `include/RenderGraph/Nodes/GeometryPassNode.h` (template)

### Building & Testing

- **Incremental:** `cmake --build build --config Debug` (if CMake files exist)
- **Full rebuild:** Delete `build/` folder, run `cmake -B build` again
- **Debugging:** Open `binaries/executable_name.exe` in Visual Studio debugger
- **Validation layer output:** Check console for `VK_LAYER_KHRONOS_validation` messages

### Shader Management

- **Location:** `Shaders/` directory
- **Runtime compilation:** If `BUILD_SPV_ON_COMPILE_TIME=ON`, glslang compiles GLSL→SPIR-V automatically
- **Manual compilation:** `glslangValidator.exe shader.glsl -V -o shader.spv`
- **Emits EventBus event:** `ShaderReloadEvent` triggers dependent pipeline recompilation

## Integration Points

**Cross-component communication:**
- **EventBus:** Central event queue for all component invalidations
- **VulkanDevice:** Shared device handle passed to all subsystems (no duplication)
- **NodeTypeRegistry:** Global registry, passed to RenderGraph at construction
- **Memory:** `std::unique_ptr` ownership chains prevent circular dependencies

**Avoid:**
- Raw pointers for ownership
- Direct component-to-component method calls (use EventBus for loose coupling)
- Shared state without clear ownership

## Troubleshooting

| Issue | Check |
|-------|-------|
| Build fails: Vulkan SDK not found | Ensure `C:/VulkanSDK/1.4.321.1` exists or set `AUTO_LOCATE_VULKAN=OFF` |
| Shader compile errors | Verify GLSL syntax, check `BuiltAssets/CompiledShaders/` output |
| Node doesn't execute | Check EventBus subscription, verify `Compile()` succeeded, inspect `dirty` flag |
| Memory leaks | Verify RAII pattern followed, all `new` wrapped in `std::unique_ptr` |
| Validation layer warnings | See console output, check Vulkan SDK version compatibility |

## Documentation Policy

**Only create `.md` files when explicitly requested by user.** 
- Do NOT auto-generate summary documents after changes
- Do NOT create checklist files, completion reports, or tracking documents
- Focus on code implementation, not documentation
- If user wants documentation, they will ask: "Create a summary" or "Document this"
- All changes should speak for themselves through clean code and inline comments

**Exception:** Update existing `.md` files (like this one) or config files as needed for project management.
