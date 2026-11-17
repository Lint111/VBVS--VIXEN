#pragma once

// R896: descriptor bindings are generated, never hand-picked.
//
// A program declares each descriptor resource by name only:
//     layout(std430, binding = VXB_TileHeaderBuffer) readonly buffer TileHeaderBuffer { ... };
// The resource's interface name (block name, or variable name for images/samplers/TLAS) is
// the declaration. The allocator collects every VXB_<Name> token reachable from a program's
// source (all #include files, all #if branches, comments stripped), sorts the names ordinally
// and assigns dense binding numbers 0..N-1 (one descriptor set per program, R532). The result
// is a pure function of the declarations: deterministic, merge-order independent, no lock file.
//
// Emitted together:
//   shaders/Generated/<Program>.bindings.g.glsl   #define VXB_<Name> <n>  (the program #includes it)
//   generated/sdi/merged/<Program>-Bindings.g.h   ShaderInterface::<Program>::Bindings::<Name>
// and the merged SDI (reflection) is cross-checked against the allocation.

#include "SdiVariantMerge.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace ShaderManagement {

struct BindingLiteral {
    std::string file;   // generic path
    uint32_t line = 0;  // 1-based
    std::string text;   // trimmed source line
};

struct ProgramBindingAllocation {
    std::string programName;
    bool migrated = false;                      // the program includes its own bindings file
    std::vector<std::string> names;             // sorted ordinal; index == binding number
    std::map<std::string, uint32_t> bindingOf;  // name -> binding
    std::vector<BindingLiteral> literals;       // `binding = <digit>` reachable from the program
    std::vector<std::string> reachableFiles;    // generic paths, sorted
};

struct BindingAllocationResult {
    bool success = false;
    ProgramBindingAllocation allocation;
    std::string errorMessage;
};

/// The include line a migrated program carries (relative to the shader include root).
std::string ProgramBindingsInclude(const std::string& programName);
/// File names of the emitted artifacts.
std::string ProgramBindingsGlslFileName(const std::string& programName);
std::string ProgramBindingsCppFileName(const std::string& programName);

/// Scan `source` and its includes (resolved against the including file's directory first,
/// then `includePaths`) and allocate the program's bindings.
/// Errors: unresolvable include; VXB_ tokens reachable from a program that does not include
/// its own bindings file; a literal `binding = <digit>` reachable from a migrated program.
BindingAllocationResult AllocateProgramBindings(const std::string& programName,
                                                const std::filesystem::path& source,
                                                const std::vector<std::filesystem::path>& includePaths);

std::string EmitProgramBindingsGlsl(const ProgramBindingAllocation& allocation);
std::string EmitProgramBindingsCpp(const ProgramBindingAllocation& allocation);

/// Cross-check a migrated program's merged reflection against its allocation. Every reflected
/// descriptor must be declared through VXB_<its own name> and sit at the allocated number.
/// Returns an empty string when consistent, otherwise one located message per violation.
std::string CheckReflectionAgainstAllocation(const ProgramBindingAllocation& allocation,
                                             const SdiMergedInterface& merged);

} // namespace ShaderManagement
