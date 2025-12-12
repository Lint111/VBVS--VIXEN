# SDI Tool / Library Consolidation

## Status: In Progress

## Problem Statement
The `sdi_tool` (shader compiler CLI) and `ShaderManagement` library had duplicated logic. This violates single-source-of-truth principles and leads to maintenance burden.

## Completed Work

### 1. Duplicate Struct Bug Fix
**Files:** `SpirvReflector.cpp`, `SpirvInterfaceGenerator.cpp`

Fixed duplicate struct definitions in generated SDI headers when same UBO used by multiple shader stages:
- Modified `MergeReflectionData()` to deduplicate structs by name
- Added `typeInfo.structName` assignment for proper index mapping
- Added safety deduplication in `GenerateStructDefinitions()`

### 2. SDI Cleanup Extracted to Library
**Files:** `SpirvInterfaceGenerator.h`, `SpirvInterfaceGenerator.cpp`, `shader_tool.cpp`

Moved SDI cleanup logic from tool to `SdiFileManager` class:
- `ExtractSdiUuidFromInclude()` - Parse `#include "*-SDI.h"` lines
- `GetReferencedUuids()` - Scan naming files for referenced UUIDs
- `CleanupOrphanedSdis()` - Delete unreferenced SDI files

Tool's `CommandCleanupSdi()` now calls library functions.

### 3. Pipeline Utils Extracted to Library
**Files:** `ShaderPipelineUtils.h`, `ShaderPipelineUtils.cpp` (NEW), `shader_tool.cpp`

Created `ShaderPipelineUtils` class with:
- `DetectStageFromPath()` - Shader stage from file extension
- `DetectPipelineFromExtension()` - Pipeline type from extension
- `DetectPipelineFromFiles()` - Pipeline type from multiple files
- `GetPipelineExtensions()` - Required/optional extensions per pipeline
- `DiscoverSiblingShaders()` - Auto-discover related shader files
- `ValidatePipelineStages()` - Validate required stages present
- `GetPipelineTypeName()` - Human-readable pipeline name

Removed ~290 lines of duplicate code from tool.

## Remaining Work

### Priority 1: Bundle Serialization
**Location:** `shader_tool.cpp` lines 434-593

Functions to extract to library:
```cpp
bool SaveBundleToJson(const ShaderDataBundle&, const fs::path&, bool embedSpirv, FileManifest*)
bool LoadBundleFromJson(const fs::path&, ShaderDataBundle&)
```

**Suggested approach:**
- Create `ShaderBundleSerializer` class in library
- Methods: `SaveToJson()`, `LoadFromJson()`, `SaveToBinary()`, `LoadFromBinary()`
- Tool calls library for serialization

### Priority 2: File Manifest System
**Location:** `shader_tool.cpp` lines 130-235

The `FileManifest` class tracks generated SPIRV files for cleanup. Consider:
- Extract to library as `GeneratedFileTracker`
- Generalize to track both SPIRV and SDI files
- Or merge functionality into existing `SdiFileManager`

### Priority 3: Old SDI Cleanup
**Location:** `shader_tool.cpp` lines 594-625

`CleanupOldSdiFiles()` should use `SdiFileManager` methods instead of direct filesystem operations.

## Current Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                        sdi_tool.exe                         │
│  ┌─────────────────────────────────────────────────────┐   │
│  │ CLI Layer (OK - tool-specific)                       │   │
│  │ - ToolOptions, ParseCommandLine, PrintUsage          │   │
│  │ - CommandCompile, CommandBatch, CommandCleanup*      │   │
│  └─────────────────────────────────────────────────────┘   │
│  ┌─────────────────────────────────────────────────────┐   │
│  │ TODO: Extract to Library                             │   │
│  │ - SaveBundleToJson, LoadBundleFromJson               │   │
│  │ - FileManifest class                                 │   │
│  │ - CleanupOldSdiFiles                                 │   │
│  └─────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                   ShaderManagement Library                   │
│  ┌──────────────────┐  ┌──────────────────┐                │
│  │ ShaderPipelineUtils│  │ SdiFileManager   │  ✓ Extracted  │
│  │ - DetectStage*     │  │ - CleanupOrphanedSdis            │
│  │ - DetectPipeline*  │  │ - GetReferencedUuids             │
│  │ - DiscoverSiblings │  │ - ExtractSdiUuid*                │
│  └──────────────────┘  └──────────────────┘                │
│  ┌──────────────────┐  ┌──────────────────┐                │
│  │ ShaderBundleBuilder│ │ SpirvReflector   │  Existing     │
│  │ ShaderCompiler     │ │ SpirvInterfaceGen│                │
│  └──────────────────┘  └──────────────────┘                │
└─────────────────────────────────────────────────────────────┘
```

## Testing

After changes, verify with:
```bash
# Test pipeline detection + sibling discovery
./build/bin/Debug/sdi_tool.exe shaders/VoxelRT.rgen --verbose

# Test SDI cleanup
./build/bin/Debug/sdi_tool.exe cleanup-sdi generated/sdi --verbose

# Verify no duplicate structs in generated SDI
grep "struct OctreeConfigUBO" generated/sdi/*-SDI.h | wc -l  # Should be 1
```

## Files Changed This Session

| File | Change |
|------|--------|
| `libraries/ShaderManagement/src/SpirvReflector.cpp` | Struct deduplication in merge |
| `libraries/ShaderManagement/src/SpirvInterfaceGenerator.cpp` | Safety dedup + cleanup methods |
| `libraries/ShaderManagement/include/SpirvInterfaceGenerator.h` | New SdiFileManager methods |
| `libraries/ShaderManagement/include/ShaderPipelineUtils.h` | NEW - Pipeline utilities |
| `libraries/ShaderManagement/src/ShaderPipelineUtils.cpp` | NEW - Pipeline utilities impl |
| `libraries/ShaderManagement/tools/shader_tool.cpp` | Use library, remove duplicates |

## Related Documentation
- [[Shader-Descriptor-Interface]] - SDI system overview
- [[ShaderManagement-Library]] - Library architecture
