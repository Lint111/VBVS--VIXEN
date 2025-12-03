# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Documentation Index

**📚 [DOCUMENTATION_INDEX.md](DOCUMENTATION_INDEX.md)** - Complete guide to all 90+ documentation files (~3,200 pages)

Use this index to find architecture docs, implementation guides, research papers, and more. Files are organized by topic, difficulty level, and include recommended reading paths.

## Communication Style - MANDATORY

**Read and follow `documentation/Communication Guidelines.md` for all communication.** Key principles:

### Radical Conciseness
- **Maximum signal, minimum noise** - every word must serve a purpose
- **No conversational filler** - eliminate phrases like "Certainly!", "I hope this helps!", "As you requested..."
- **Lead with conclusion** - state the most important information first
- **Use structured data** - lists, tables, code blocks over prose
- **Report facts, not process** - state the plan/action/result, not your thinking process
- **Be economical** - if a sentence can be shorter, make it shorter

### No Sycophantic Language
- **NEVER** use "You're absolutely right!", "Excellent point!", or similar flattery
- **Brief acknowledgments only**: "Got it.", "I understand.", "I see the issue."
- **Proceed directly** to execution when possible

### Examples
- ❌ "Certainly! I'll help you with that. Let me start by reading the file..."
- ✅ [Proceeds directly with Read tool]

- ❌ "You're absolutely right! That's a great decision."
- ✅ "Got it." [proceeds with action]

**See `documentation/Communication Guidelines.md` for complete directive and examples.**

## Agent Preference - MANDATORY

**Default to `coding-partner` agent for all development work.** This agent provides collaborative coding assistance and automatically delegates to specialized agents when appropriate.

### Model Selection
- **Default**: Opus 4.5 for all agents (highest capability)
- **Fallback**: Sonnet when Opus hits token limits or rate limits
- **Exception**: `intern-army-refactor` uses Haiku (fast, repetitive tasks)

Use `coding-partner` for:
- Feature implementation and bug fixes
- Code refactoring and improvements
- Architecture discussions and decisions
- General development questions
- Any coding task requiring clean, maintainable solutions

The `coding-partner` agent will proactively delegate to:
- `code-reviewer` for implementation feedback
- `architecture-advisor` for design decisions
- `debugger-assistant` for crash investigation
- Other specialized agents as needed

**Only bypass `coding-partner` when:**
- User explicitly requests a specific agent
- Task is purely informational (no coding involved)
- Task requires specialized agent that coding-partner wouldn't delegate to

### Agent Communication Protocol - MANDATORY

**Always announce agent usage clearly:**

1. **Before launching agent:**
   - State which agent you're launching and why
   - Example: "Launching `bug-hunter` agent to investigate the crash..."
   - Example: "Using `coding-partner` agent to implement this feature..."

2. **After agent completes:**
   - Summarize the agent's findings/actions
   - Provide specific file references and changes made
   - Example: "Agent fixed X in [file.cpp:42](file.cpp#L42) by adding null check."

3. **Tool usage transparency:**
   - When using Read/Edit/Grep tools directly, proceed without announcement
   - When launching agents, always announce first
   - User sees agent activity via progress spinner, but needs context

**Example flow:**
```
User: "Fix the rendering bug"
Assistant: "Launching bug-hunter agent to investigate rendering issue..."
[agent works - user sees spinner]
Assistant: "Bug found: missing viewport initialization in [Renderer.cpp:156](Renderer.cpp#L156). Fixed by adding vkCmdSetViewport call."
```

## Engineering Philosophy - MANDATORY

### No Quick Fixes - Always Choose Robust Solutions

**CRITICAL RULE**: When faced with implementation choices, ALWAYS select the comprehensive, robust, future-proof solution over quick fixes or workarounds.

**Prohibited Approaches:**
- ❌ Changing user requirements to fit broken implementation (e.g., "just use depth 23 instead of 8")
- ❌ Adding hardcoded constants or magic numbers to bypass problems
- ❌ Commenting out failing tests instead of fixing root causes
- ❌ Adding special-case logic instead of fixing the general algorithm
- ❌ Deferring fixes with "we'll come back to this later"

**Required Approaches:**
- ✅ Fix the underlying algorithm to support user requirements as specified
- ✅ Refactor broken assumptions to handle general cases
- ✅ Implement proper abstractions that work for any valid input
- ✅ Write code that is maintainable and extensible
- ✅ If user specifies depth 8, the implementation MUST support depth 8

**Example Violation:**
```
User: "Cornell Box uses depth 8"
❌ Bad: "Change it to depth 23 because ESVO is hardcoded for that"
✅ Good: "Fix ESVO traversal to support arbitrary depths"
```

**Rationale:**
- Quick fixes create technical debt that compounds over time
- Workarounds make code brittle and hard to maintain
- Future features will hit the same limitations
- Professional engineering requires solving problems correctly, not duct-taping symptoms

## Code Review Philosophy - MANDATORY

**Approach all interactions as a senior developer mentoring a junior developer.**

### Critical Analysis
- **Always provide honest, critical feedback** - identify weaknesses, gaps, and areas behind industry standards
- **Don't over-praise** - acknowledge what works, but focus on what needs improvement
- **Compare to industry standards** - Unity HDRP, Unreal RDG, Frostbite, modern AAA engines
- **Identify technical debt** - TODOs, missing features, architectural limitations
- **Challenge assumptions** - question design decisions, ask "why not use X instead?"

### Constructive Feedback Format
1. **Acknowledge strengths briefly** (1-2 sentences)
2. **Identify critical weaknesses** (🔴 blockers, 🟡 major issues, 🟠 minor issues)
3. **Explain industry standard** (what top-tier engines do differently)
4. **Quantify impact** (performance loss, memory waste, time cost)
5. **Suggest priorities** (what to fix first, what to defer)

### Examples
- ❌ "This is exceptional work! The architecture is brilliant!"
- ✅ "Clean implementation. However, single-threaded execution leaves 75-90% CPU cores idle - 10 years behind Unity/Unreal (2015 wave-based parallelism). Add to Phase N+3 roadmap."

- ❌ "The SlotRole pattern is novel and publication-worthy!"
- ✅ "SlotRole bitwise flags are standard (Vulkan/D3D12 use everywhere). Application to split descriptor binding is clean engineering, not research novelty. Focus publication on voxel ray tracing comparison instead."

### When to Be Critical
- **Architecture reviews** - always compare to industry best practices
- **Performance discussions** - identify optimization gaps
- **Design decisions** - challenge with "what about [alternative]?"
- **Research claims** - verify novelty against existing work
- **Completion estimates** - point out missing features or risks

## Session Management - CRITICAL

**At the end of every significant unit of work, update `memory-bank/activeContext.md`** with:
- What was completed
- Test results
- Files modified (with line numbers)
- Next steps
- Any blockers or issues

This ensures context persistence across sessions and provides a clear history of progress.

### activeContext.md Structure - MANDATORY

The `activeContext.md` file has a **REQUIRED STRUCTURE** that must be preserved across sessions. When updating this file, you MUST:

1. **NEVER delete these sections** - they provide essential long-term context:
   - **Week 1 & 1.5+ Success Criteria** - tracks project milestones
   - **Known Limitations** - documents accepted edge cases
   - **Reference Sources** - ESVO implementation paths and paper citations
   - **Todo List (Active Tasks)** - comprehensive task tracking across weeks

2. **Always update these sections** with current session info:
   - **Last Updated** date
   - **Current Status** summary
   - **Session Summary** - what was accomplished this session
   - **Test Results** - current pass/fail counts
   - **Modified Files** - specific files and line numbers changed
   - **Next Steps** - immediate priorities for next session
   - **Session Metrics** - time invested, code changes, lessons learned

3. **Section ordering** (maintain this exact structure):
   - Current Status
   - Session Summary
   - Test Results
   - Bug Analysis (if applicable)
   - Modified Files
   - Key Technical Discoveries (if any)
   - Next Steps (Priority Order)
   - Week 1 & 1.5+ Success Criteria ← **NEVER DELETE**
   - Known Limitations ← **NEVER DELETE**
   - Production Readiness Assessment
   - Reference Sources ← **NEVER DELETE**
   - Session Metrics
   - Todo List (Active Tasks) ← **NEVER DELETE**

4. **Why these sections persist**:
   - Success Criteria: Tracks overall project progress across multiple sessions
   - Known Limitations: Prevents re-investigating known edge cases
   - Reference Sources: Essential for understanding ESVO algorithm details
   - Todo List: Maintains work queue spanning weeks, not just current session

**Example violation**: Replacing entire file with only current session info loses critical context about completed milestones, known limitations, and long-term tasks.

**Correct approach**: Update session-specific sections while preserving structural sections that track long-term project state.

## Memory Bank - CRITICAL

The Memory Bank in `memory-bank/` provides persistent project context across sessions. **Read the appropriate files based on session type.**

### Quick Start (Every Task/Prompt)
Read these "hot" files that change frequently:
1. `memory-bank/activeContext.md` - Current focus, recent changes, active decisions
2. `memory-bank/progress.md` - Implementation status, what's done, what's left

### Full Context (New Conversation / After Reset)
When starting a fresh conversation or after conversation compression, read ALL files IN ORDER:
1. `memory-bank/projectbrief.md` - Project goals, scope, and success criteria
2. `memory-bank/productContext.md` - Why the project exists and design philosophy
3. `memory-bank/systemPatterns.md` - Architecture patterns and component design
4. `memory-bank/techContext.md` - Technology stack and development setup
5. `memory-bank/activeContext.md` - Current focus and recent decisions
6. `memory-bank/progress.md` - Implementation status and what's left to build

### Keeping Memory Bank Updated
Update the Memory Bank when:
- Implementing significant features or architectural changes
- Discovering new patterns or making important decisions
- User explicitly requests **"update memory bank"** (must review ALL files)
- Project status changes meaningfully

When updating:
- **Always update**: `activeContext.md` (current focus) and `progress.md` (status)
- **Update if changed**: `systemPatterns.md` (new patterns), `techContext.md` (tech changes)
- **Rarely update**: `projectbrief.md` and `productContext.md` (stable foundations)
- Keep documentation concise but comprehensive

## Build System - MANDATORY

This is a CMake-based C++ Vulkan application project. The build system is configured for Windows with Visual Studio.

### Build Procedure - REQUIRED WORKFLOW

**CRITICAL**: Always follow this exact procedure for optimal build speed and error tracking.

#### 1. Initial Configuration (First Time / After CMake Changes)

```bash
# Standard configuration (Unity builds disabled - incompatible with some files)
cmake -B build -DCMAKE_BUILD_TYPE=Debug
```

**Note**: Unity builds (`-DUSE_UNITY_BUILD=ON`) are disabled due to syntax conflicts in DXT1Compressor.cpp. Standard incremental builds are still fast (~10-30 seconds for typical changes).

#### 2. Full Build (Clean Build / Major Changes)

```bash
# Build entire project with all 16 cores, filter PDB warnings
cd C:/cpp/VBVS--VIXEN/VIXEN
cmake --build build --config Debug --parallel 16 2>&1 | grep -v "warning LNK4099" | tail -100
```

**Build time**: ~3-5 minutes (full project)

#### 3. Incremental Build (Daily Workflow - Fastest)

After initial full build, **only rebuild changed files**:

```bash
# Build only modified targets (10-30 seconds typically)
cmake --build build --config Debug --parallel 16
```

#### 4. Target-Specific Build (Recommended for Development)

Build only what you're working on:

```bash
# Build only SVO library
cmake --build build --config Debug --target SVO --parallel 16

# Build only specific tests
cmake --build build --config Debug --target test_rebuild_hierarchy test_cornell_box --parallel 16

# Build only Core + GaiaVoxelWorld + SVO stack
cmake --build build --config Debug --target Core GaiaVoxelWorld SVO --parallel 16
```

**Build time**: ~30 seconds - 1 minute (single library)

#### 5. Build Optimization Flags

Already enabled in CMakeLists.txt:
- ✅ `/MP` - MSVC multi-processor compilation (all 16 cores)
- ✅ `sccache` - Compilation caching (automatic)
- ✅ Precompiled headers (pch.h in each library)
- ✅ PDB warnings suppressed (`/ignore:4099`)
- ✅ Unity builds (when enabled with `-DUSE_UNITY_BUILD=ON`)

#### 6. Build Performance Tips

**DO**:
- ✅ Use `--parallel 16` (leverages all CPU cores)
- ✅ Build specific targets during development (faster iteration)
- ✅ Use incremental builds (cmake tracks changes automatically)
- ✅ Filter PDB warnings with `grep -v "warning LNK4099"`
- ✅ Enable Unity builds for clean builds

**DON'T**:
- ❌ Clean build directory unnecessarily (incremental builds are fast)
- ❌ Build all tests when only working on one library
- ❌ Disable parallel compilation
- ❌ Ignore build errors in output (scroll to find real issues)

#### 7. Testing After Build

```bash
# Run specific test suite
./build/libraries/SVO/tests/Debug/test_rebuild_hierarchy.exe --gtest_brief=1

# Run all SVO tests
cd build/libraries/SVO/tests/Debug && for test in test_*.exe; do ./$test --gtest_brief=1; done
```

**Test time**: ~2-3 seconds per test, ~30 seconds for full SVO suite

#### 8. Build Troubleshooting

**Problem**: "Cannot open include file"
- **Solution**: Run full configuration: `cmake -B build -DUSE_UNITY_BUILD=ON`

**Problem**: "LNK4099 PDB warnings spam"
- **Solution**: Filter with `grep -v "warning LNK4099"` (these are external lib warnings, safe to ignore)

**Problem**: "MSBuild.exe or cl.exe using 100% CPU"
- **Solution**: Kill zombie processes: `taskkill /F /IM MSBuild.exe /T 2>nul; taskkill /F /IM cl.exe /T 2>nul`

**Problem**: "Build takes 10+ minutes"
- **Solution**: Check Unity builds enabled, use `--parallel 16`, build specific targets only

### Project Structure

- `source/` - C++ source files (.cpp)
- `include/` - Header files (.h)
- `build/` - CMake generated build files and Visual Studio project files
- `binaries/` - Output directory for compiled executables
- `.vscode/` - VSCode configuration for IntelliSense and file associations

## Architecture Overview

This is a Vulkan learning project implementing a basic device handshake. The architecture follows a layered approach:

### Core Components

1. **VulkanApplication** (`VulkanApplication.h/.cpp`)
   - Singleton pattern main application class
   - Orchestrates the Vulkan initialization process
   - Entry point: `VulkanApplication::GetInstance()->Initialize()`

2. **VulkanInstance** (`VulkanInstance.h/.cpp`)
   - Manages Vulkan instance creation and destruction
   - Handles layer and extension setup
   - Core method: `CreateInstance()` for Vulkan instance creation

3. **VulkanDevice** (`VulkanDevice.h/.cpp`)
   - Manages Vulkan logical device operations
   - Physical device selection and logical device creation

4. **VulkanLayerAndExtension** (`VulkanLayerAndExtension.h/.cpp`)
   - Handles Vulkan validation layers and extensions
   - Layer enumeration and validation

### Configuration

- **Language Standard**: C++23 and C23
- **Platform**: Windows (VK_USE_PLATFORM_WIN32_KHR)
- **Vulkan SDK**: Auto-detection via CMake or manual path specification
- **Default Extensions**: VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME
- **Default Layers**: VK_LAYER_LUNARG_api_dump

### Development Notes

- The project uses manual Vulkan SDK path fallback: `C:/VulkanSDK/1.4.321.1`
- VSCode is configured with Vulkan SDK include paths for IntelliSense
- Executable output goes to `binaries/` directory
- The main application creates extensions and layers in `main.cpp` and passes them to the application singleton

## File Organization

```
source/
├── main.cpp              # Entry point with extension/layer setup
├── VulkanApplication.cpp # Main application singleton
├── VulkanInstance.cpp    # Vulkan instance management
├── VulkanDevice.cpp      # Device selection and creation
└── VulkanLayerAndExtension.cpp # Layer/extension handling

include/
├── Headers.h             # Common includes (vulkan.h, vector, iostream)
├── VulkanApplication.h   # Main application interface
├── VulkanInstance.h      # Instance management interface
├── VulkanDevice.h        # Device management interface
└── VulkanLayerAndExtension.h # Layer/extension interface
```

This is Chapter 3 example code focusing on establishing the initial handshake between the application and Vulkan drivers/devices.

## Coding Standards

All C++ code in this project must follow the guidelines specified in `documentation/cpp-programming-guidelins.md`. Key standards include:

- **Nomenclature**: PascalCase for classes, camelCase for functions/variables, ALL_CAPS for constants
- **Functions**: Keep functions short (<20 instructions), single purpose, use early returns to avoid nesting
- **Data**: Use const-correctness, prefer immutability, use std::optional for nullable values
- **Classes**: Follow SOLID principles, prefer composition over inheritance, keep classes small (<200 instructions, <10 public methods)
- **Memory**: Use smart pointers (std::unique_ptr, std::shared_ptr) over raw pointers, follow RAII principles
- **Modern C++**: Leverage C++23 features and the standard library (std::string, std::vector, std::filesystem, std::chrono, etc.)

See the full guidelines in `documentation/cpp-programming-guidelins.md` for complete details.