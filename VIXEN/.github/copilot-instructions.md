Short directives to help an AI coding agent become productive in this repo.

Communication & Memory Bank rules (MANDATORY)
- Follow `documentation/Communication Guidelines.md` and the rules in `CLAUDE.md` exactly.
- Radical conciseness: maximum signal, minimum noise. Lead with the conclusion, use lists/tables, and avoid filler.
- No sycophancy: avoid flattery or excessive praise ("You're absolutely right"); use brief acknowledgements ("Got it.").
- Report facts and results, not internal chain-of-thought. Prefer structured outputs (todo, files changed, commands).

- Memory Bank is critical: always read `memory-bank/activeContext.md` and `memory-bank/progress.md` before starting a task.
- For full-context sessions read in order: `memory-bank/projectbrief.md`, `memory-bank/productContext.md`, `memory-bank/systemPatterns.md`, `memory-bank/techContext.md`, then `memory-bank/activeContext.md`, and `memory-bank/progress.md`.
- Update the memory bank when you change initialization flow or implement significant features. Always update `activeContext.md` and `progress.md` to reflect status changes.

1. Quick context
- This is a small C++ Vulkan sample (chapter 3 from "Learning Vulkan") that demonstrates a device handshake and simple rendering.
- Key folders: `source/` (implementation), `include/` (headers), `memory-bank/` (project memory), `binaries/` (runtime output), `build/` (CMake output).

2. How to build (Windows)
- Primary build: CMake. From project root:

  cmake -B build
  cmake --build build --config Debug

- If `AUTO_LOCATE_VULKAN` fails, CMake falls back to `C:/VulkanSDK/<version>` (see `CMakeLists.txt`). If missing, set option `-DAUTO_LOCATE_VULKAN=OFF` and provide `VULKAN_SDK`/`VULKAN_VERSION`.
- Shader option: `BUILD_SPV_ON_COMPILE_TIME` controls whether GLSL is compiled at build time. If OFF, precompile with `glslangValidator`.

3. High-level architecture and responsibilities
- `VulkanApplication` (include/VulkanApplication.h, source/VulkanApplication.cpp)
  - Singleton app orchestrating init, handshake, prepare, update and render loop. Holds `instanceObj`, `deviceObj`, and `renderObj`.

- `VulkanInstance` (include/VulkanInstance.h/.cpp)
  - Creates Vulkan instance, manages layers/extensions.

- `VulkanDevice` (include/VulkanDevice.h/.cpp)
  - Selects physical device, creates logical device and queues. Exposes memory helpers like `MemoryTypeFromProperties`.

- `VulkanRenderer` (include/VulkanRenderer.h/.cpp)
  - Creates presentation window, swapchain, renderpass, framebuffers, pipelines and issues draw calls. Responsible for platform differences (Win32 vs XCB).

Design notes:
- The codebase uses explicit ownership: unique_ptr for components (device, renderer, shaders) and shared_ptr for loggers.
- Platform-specific code is guarded by the `_WIN32` platform macro and uses Win32 window creation in `VulkanRenderer`.

4. Patterns and conventions specific to this repo
- Naming: PascalCase for classes, camelCase for functions/variables, ALL_CAPS for constants (see `documentation/cpp-programming-guidelins.md`).
- Files are organized by subsystem: `include/` headers mirror `source/` .cpp names.
- Use RAII and smart pointers; avoid raw owning pointers. Many classes expose Create/Destroy-style methods (e.g., `CreateDevice` / `DestroyDevice`).
- Logging: `logger/` contains `Logger` and `FrameRateLogger` used across components—use existing logger instead of adding new global prints.

5. Important files to read when making changes
- `CLAUDE.md` — communication and memory-bank usage (present in repo root).
- `memory-bank/activeContext.md` and `memory-bank/progress.md` — current focus and status; update if you change behavior.
- `CMakeLists.txt` — contains build flags, Vulkan SDK discovery and GLSL/SPV options. Adjust here for SDK paths or build-time shader compilation.

6. Developer workflows and debugging
- Run binary from `binaries/` after build. The app expects validation layers in debug builds (see `main.cpp`).
- For shader problems, if `BUILD_SPV_ON_COMPILE_TIME=ON` the project links against glslang; otherwise run `glslangValidator.exe` to produce `.spv` files.
- Use Visual Studio on Windows by opening the `.sln` in `build/` if you need IDE debugging.

7. Safety and constraints for code edits
- Preserve platform guards (the `_WIN32` macro) when editing windowing or surface code.
- Do not change runtime output directory — CMake sets it to `binaries/` intentionally.
- When adding public API surface (headers), keep names and casing consistent with existing conventions.

8. Example quick edits
- To add a debug-only validation message, add it behind the `_DEBUG` debug-only macro near initialization in `source/main.cpp` or `VulkanApplication::CreateVulkanInstance`.
- To add a new device extension opt-in, update `deviceExtensionNames` in `source/main.cpp` and ensure it's passed to `VulkanApplication::CreateVulkanInstance` / `HandShakeWithDevice`.

9. Where to update project memory
- After implementing features or changing initialization flow, update `memory-bank/activeContext.md` and `memory-bank/progress.md` to reflect the change so future agents read the new state.

If any part of this is unclear or you want more detail on build/test/debug commands, tell me which area to expand.

Phase 3 / 4 refactor checklist (practical mapping)
- Goal: Finish phases 3 & 4 of the code-quality plan (smart-pointer conversion, API cleanup).
- Safe-edit rules: change ownership to `std::unique_ptr` in one commit per subsystem; keep observer/raw non-owning pointers only where documented and add a comment `// non-owning`.

- High-priority files to touch:
  - `include/VulkanApplication.h` / `source/VulkanApplication.cpp` — ensure `instance` stays `std::unique_ptr`, and `deviceObj`/`renderObj` are unique_ptrs (already used here).
  - `include/VulkanRenderer.h` / `source/VulkanRenderer.cpp` — convert owning raw pointers to unique_ptr (e.g., `VulkanDevice* deviceObj` -> keep as observer via `.get()`; prefer `GetDevice()` to return pointer but store owner in `VulkanApplication`).
  - `include/VulkanSwapChain.h` / `include/VulkanDrawable.h` — mark renderer/app pointers as non-owning and document with `// non-owning`.
  - `include/VulkanDevice.h` / `source/VulkanDevice.cpp` — review `VkPhysicalDevice* gpu` member: prefer storing the handle (keep pointer only if required by Vulkan API) and avoid `new`/`delete` in device construction.
  - `source/VulkanShader.cpp` — eliminate `new` usages (glslang `TProgram`/`TShader`) by using stack objects or `std::unique_ptr` and ensure the object is deleted or RAII-managed.
  - `include/wrapper.h` / call-sites — replace `void* ReadFile(...)` with `std::vector<uint8_t>` or `std::string` and prefer sized-safe containers.

- Testing each change:
  1. Make a small commit converting one class to `unique_ptr`.
 2. Run the CMake build (`cmake -B build; cmake --build build --config Debug`).
 3. Run the binary from `binaries/` and smoke-test execution.
 4. Update `memory-bank/activeContext.md` and `memory-bank/progress.md` with a one-line summary and link to the PR.

Small safe wins to add now:
- Replace local `new` in `source/VulkanShader.cpp` with stack objects or `std::unique_ptr`.
- Add `// non-owning` comments to raw pointers that are observers (e.g., `VulkanDrawable::rendererObj`).
- Prefer `std::make_unique<T>` for new ownership sites.

