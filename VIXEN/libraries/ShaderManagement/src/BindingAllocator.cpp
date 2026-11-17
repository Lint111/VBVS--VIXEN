#include "BindingAllocator.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace ShaderManagement {

namespace {

std::string ReadFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// Replace comments with spaces, keeping newlines so line numbers survive.
std::string StripComments(const std::string& text) {
    std::string out = text;
    size_t i = 0;
    while (i < out.size()) {
        if (out[i] == '"') {  // string literal (only #include paths in GLSL)
            ++i;
            while (i < out.size() && out[i] != '"' && out[i] != '\n') ++i;
            if (i < out.size()) ++i;
        } else if (out.compare(i, 2, "//") == 0) {
            while (i < out.size() && out[i] != '\n') out[i++] = ' ';
        } else if (out.compare(i, 2, "/*") == 0) {
            while (i < out.size() && out.compare(i, 2, "*/") != 0) {
                if (out[i] != '\n') out[i] = ' ';
                ++i;
            }
            if (i < out.size()) { out[i] = ' '; out[i + 1] = ' '; i += 2; }
        } else {
            ++i;
        }
    }
    return out;
}

std::string Trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t\r");
    return s.substr(b, e - b + 1);
}

bool IsGeneratedBindingsFile(const fs::path& path) {
    const std::string name = path.filename().string();
    const std::string suffix = ".bindings.g.glsl";
    return name.size() > suffix.size() &&
           name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
}

struct ScanState {
    std::set<std::string> visited;
    std::set<std::string> names;
    std::vector<BindingLiteral> literals;
    bool includesOwnBindings = false;
    std::string ownBindingsFile;
    std::string error;
};

void Scan(const fs::path& file, const std::vector<fs::path>& includePaths, ScanState& state) {
    const std::string key = fs::weakly_canonical(file).generic_string();
    if (!state.visited.insert(key).second || !state.error.empty()) return;

    const std::string text = StripComments(ReadFile(file));
    static const std::regex includeRe(R"re(^\s*#\s*include\s*"([^"]+)")re");
    static const std::regex tokenRe(R"re(\bVXB_([A-Za-z_][A-Za-z0-9_]*))re");
    static const std::regex literalRe(R"re(\bbinding\s*=\s*[0-9])re");

    std::istringstream lines(text);
    std::string line;
    uint32_t lineNo = 0;
    while (std::getline(lines, line)) {
        ++lineNo;
        std::smatch m;
        if (std::regex_search(line, m, includeRe)) {
            const std::string target = m[1].str();
            fs::path resolved;
            std::vector<fs::path> candidates{file.parent_path()};
            candidates.insert(candidates.end(), includePaths.begin(), includePaths.end());
            for (const auto& dir : candidates) {
                if (fs::exists(dir / target)) { resolved = dir / target; break; }
            }
            if (fs::path(target).filename().string() == state.ownBindingsFile) {
                state.includesOwnBindings = true;
                continue;  // the generated file itself is output, not a declaration
            }
            if (resolved.empty()) {
                // Generated bindings files of OTHER programs, or files absent until generation,
                // are a hard error: a program reaches only its own namespace.
                if (IsGeneratedBindingsFile(target)) {
                    state.error = file.generic_string() + ":" + std::to_string(lineNo) +
                                  ": includes another program's bindings file '" + target + "'";
                    return;
                }
                state.error = file.generic_string() + ":" + std::to_string(lineNo) +
                              ": cannot resolve #include \"" + target + "\"";
                return;
            }
            if (IsGeneratedBindingsFile(resolved)) {
                state.error = file.generic_string() + ":" + std::to_string(lineNo) +
                              ": includes another program's bindings file '" + target + "'";
                return;
            }
            Scan(resolved, includePaths, state);
            if (!state.error.empty()) return;
            continue;
        }
        for (std::sregex_iterator it(line.begin(), line.end(), tokenRe), end; it != end; ++it) {
            state.names.insert((*it)[1].str());
        }
        if (std::regex_search(line, literalRe)) {
            state.literals.push_back({file.generic_string(), lineNo, Trim(line)});
        }
    }
}

} // namespace

std::string ProgramBindingsGlslFileName(const std::string& programName) {
    return programName + ".bindings.g.glsl";
}

std::string ProgramBindingsCppFileName(const std::string& programName) {
    return programName + "-Bindings.g.h";
}

std::string ProgramBindingsInclude(const std::string& programName) {
    return "Generated/" + ProgramBindingsGlslFileName(programName);
}

BindingAllocationResult AllocateProgramBindings(const std::string& programName,
                                                const fs::path& source,
                                                const std::vector<fs::path>& includePaths) {
    BindingAllocationResult result;
    ScanState state;
    state.ownBindingsFile = ProgramBindingsGlslFileName(programName);
    Scan(source, includePaths, state);
    if (!state.error.empty()) {
        result.errorMessage = "R896 bindings (" + programName + "): " + state.error;
        return result;
    }

    ProgramBindingAllocation& a = result.allocation;
    a.programName = programName;
    a.migrated = state.includesOwnBindings;
    a.names.assign(state.names.begin(), state.names.end());  // std::set: ordinal order
    for (uint32_t i = 0; i < a.names.size(); ++i) a.bindingOf[a.names[i]] = i;
    a.literals = std::move(state.literals);
    a.reachableFiles.assign(state.visited.begin(), state.visited.end());

    std::ostringstream err;
    if (!a.migrated && !a.names.empty()) {
        err << "R896 bindings (" << programName << "): declares VXB_ names ("
            << a.names.front() << (a.names.size() > 1 ? ", ..." : "")
            << ") but does not #include \"" << ProgramBindingsInclude(programName) << "\"\n";
    }
    if (a.migrated) {
        for (const auto& lit : a.literals) {
            err << lit.file << ":" << lit.line << ": R896: hand-picked binding number in "
                << programName << " (declare it as binding = VXB_<Name>): " << lit.text << "\n";
        }
    }
    result.errorMessage = err.str();
    result.success = result.errorMessage.empty();
    return result;
}

std::string EmitProgramBindingsGlsl(const ProgramBindingAllocation& a) {
    std::ostringstream out;
    std::string guard = "VIXEN_BINDINGS_" + a.programName + "_G_GLSL";
    std::transform(guard.begin(), guard.end(), guard.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    out << "// Generated by sdi_tool merge-variants (R896): descriptor bindings of program "
        << a.programName << ".\n"
        << "// Names are the resources' interface names, allocated densely in ordinal order.\n"
        << "// Do not edit; declare a resource as `binding = VXB_<Name>` and regenerate.\n"
        << "#ifndef " << guard << "\n#define " << guard << "\n";
    for (uint32_t i = 0; i < a.names.size(); ++i) {
        out << "#define VXB_" << a.names[i] << " " << i << "\n";
    }
    out << "#endif\n";
    return out.str();
}

std::string EmitProgramBindingsCpp(const ProgramBindingAllocation& a) {
    std::ostringstream out;
    out << "// Generated by sdi_tool merge-variants (R896): descriptor bindings of program "
        << a.programName << ".\n"
        << "// The same allocation as shaders/Generated/" << ProgramBindingsGlslFileName(a.programName)
        << "; do not edit.\n"
        << "#pragma once\n\n#include <cstdint>\n\n"
        << "namespace ShaderInterface {\nnamespace " << a.programName << " {\nnamespace Bindings {\n\n";
    for (uint32_t i = 0; i < a.names.size(); ++i) {
        out << "inline constexpr uint32_t " << a.names[i] << " = " << i << "u;\n";
    }
    out << "\ninline constexpr uint32_t COUNT = " << a.names.size() << "u;\n"
        << "inline constexpr const char* NAMES[] = {";
    for (uint32_t i = 0; i < a.names.size(); ++i) {
        out << (i ? ", " : "") << "\"" << a.names[i] << "\"";
    }
    if (a.names.empty()) out << "nullptr";
    out << "};\n\n} // namespace Bindings\n} // namespace " << a.programName
        << "\n} // namespace ShaderInterface\n";
    return out.str();
}

std::string CheckReflectionAgainstAllocation(const ProgramBindingAllocation& a,
                                             const SdiMergedInterface& merged) {
    if (!a.migrated) return {};
    std::ostringstream err;
    for (const auto& mb : merged.bindings) {
        const auto& b = mb.binding;
        const auto it = a.bindingOf.find(b.name);
        if (it == a.bindingOf.end()) {
            err << "R896 bindings (" << a.programName << "): resource '" << b.name
                << "' at (set " << b.set << ", binding " << b.binding
                << ") is not declared through VXB_" << b.name << "\n";
        } else if (it->second != b.binding) {
            err << "R896 bindings (" << a.programName << "): resource '" << b.name
                << "' reflects at binding " << b.binding << " but VXB_" << b.name << " = "
                << it->second << " (declared through another resource's name?)\n";
        }
    }
    return err.str();
}

} // namespace ShaderManagement
