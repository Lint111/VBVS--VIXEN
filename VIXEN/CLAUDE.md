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

## Build System

This is a CMake-based C++ Vulkan application project. The build system is configured for Windows with Visual Studio.

### Build Commands

**IMPORTANT**: Before building, always kill existing compiler processes to prevent background buildup:
```bash
taskkill /F /IM MSBuild.exe /T 2>nul; taskkill /F /IM cl.exe /T 2>nul
```

```bash
# Generate build files (run from project root)
cmake -B build

# Build the project
cmake --build build --config Debug
# or
cmake --build build --config Release

# Alternative: Use Visual Studio solution
# Open build/3_0_DeviceHandshake.sln in Visual Studio and build from IDE
```

**Build Error Logging - MANDATORY**:
- **Always capture build output** to `temp/build-errors.txt` (overwrite existing)
- Use PowerShell redirection: `cmake --build build --config Debug 2>&1 | Out-File -FilePath "temp/build-errors.txt" -Encoding utf8`
- This provides persistent error log for user review
- File location: `c:\cpp\VBVS--VIXEN\VIXEN\temp\build-errors.txt`

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