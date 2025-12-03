#include "ShaderPreprocessor.h"
#include <fstream>
#include <sstream>
#include <regex>
#include <unordered_set>
#include <optional>

namespace ShaderManagement {

ShaderPreprocessor::ShaderPreprocessor(const PreprocessorConfig& cfg)
    : config(cfg)
{
}

PreprocessedSource ShaderPreprocessor::Preprocess(
    const std::string& source,
    const std::unordered_map<std::string, std::string>& defines,
    const std::filesystem::path& currentFilePath)
{
    // Merge global and local defines
    std::unordered_map<std::string, std::string> allDefines = config.globalDefines;
    allDefines.insert(defines.begin(), defines.end());

    // Track included files to prevent circular includes
    std::unordered_set<std::string> includeGuard;

    // Process source
    return ProcessRecursive(
        source,
        currentFilePath,
        allDefines,
        includeGuard,
        0
    );
}

PreprocessedSource ShaderPreprocessor::PreprocessFile(
    const std::filesystem::path& filePath,
    const std::unordered_map<std::string, std::string>& defines)
{
    // Read file
    std::string source = ReadFileToString(filePath);
    if (source.empty()) {
        PreprocessedSource result;
        result.success = false;
        result.errorMessage = "Failed to open file: " + filePath.string();
        return result;
    }

    return Preprocess(source, defines, filePath);
}

PreprocessedSource ShaderPreprocessor::ProcessRecursive(
    const std::string& source,
    const std::filesystem::path& currentFilePath,
    const std::unordered_map<std::string, std::string>& allDefines,
    std::unordered_set<std::string>& includeGuard,
    uint32_t depth)
{
    PreprocessedSource result;
    result.success = false;

    if (depth >= config.maxIncludeDepth) {
        result.errorMessage = "Maximum include depth exceeded (" + std::to_string(config.maxIncludeDepth) + ")";
        return result;
    }

    std::stringstream output;
    std::istringstream input(source);
    std::string line;
    int lineNumber = 0;

    while (std::getline(input, line)) {
        ++lineNumber;

        // Check for #include directive
        std::string includeName;
        if (IsIncludeDirective(line, includeName)) {
            // Resolve include path
            auto resolvedPath = ResolveIncludePath(includeName, currentFilePath);

            if (!resolvedPath.has_value()) {
                result.errorMessage = "Failed to resolve include: " + includeName + " at line " + std::to_string(lineNumber);
                return result;
            }

            // Check for circular includes
            std::string canonicalPath = std::filesystem::canonical(resolvedPath.value()).string();
            if (includeGuard.count(canonicalPath) > 0) {
                result.errorMessage = "Circular include detected: " + canonicalPath;
                return result;
            }

            // Read included file
            std::string includeSource = ReadFileToString(resolvedPath.value());
            if (includeSource.empty()) {
                result.errorMessage = "Failed to open include file: " + resolvedPath.value().string();
                return result;
            }

            // Mark as included
            includeGuard.insert(canonicalPath);
            result.includedFiles.push_back(resolvedPath.value());

            // Recursively process included file
            PreprocessedSource nestedResult = ProcessRecursive(
                includeSource,
                resolvedPath.value(),
                allDefines,
                includeGuard,
                depth + 1
            );

            if (!nestedResult.success) {
                return nestedResult;
            }

            // Add line directive if enabled
            if (config.enableLineDirectives) {
                output << "#line 1 \"" << resolvedPath.value().string() << "\"\n";
            }

            output << nestedResult.processedSource << "\n";

            // Restore line directive
            if (config.enableLineDirectives) {
                output << "#line " << (lineNumber + 1) << " \"" << currentFilePath.string() << "\"\n";
            }

            // Merge included files from nested result
            result.includedFiles.insert(
                result.includedFiles.end(),
                nestedResult.includedFiles.begin(),
                nestedResult.includedFiles.end()
            );
        }
        else {
            // Not an include - inject defines
            std::string processedLine = InjectDefines(line, allDefines);
            output << processedLine << "\n";
        }
    }

    result.processedSource = output.str();
    result.success = true;
    return result;
}

std::string ShaderPreprocessor::InjectDefines(
    const std::string& line,
    const std::unordered_map<std::string, std::string>& defines)
{
    // Simple macro substitution (doesn't handle function-like macros)
    std::string result = line;

    for (const auto& [name, value] : defines) {
        // Create regex to match whole words only
        std::regex defineRegex("\\b" + name + "\\b");
        result = std::regex_replace(result, defineRegex, value);
    }

    return result;
}

std::optional<std::filesystem::path> ShaderPreprocessor::ResolveIncludePath(
    const std::string& includePath,
    const std::filesystem::path& currentFilePath)
{
    // Try relative to current file first
    if (!currentFilePath.empty()) {
        std::filesystem::path relativePath = currentFilePath.parent_path() / includePath;
        if (std::filesystem::exists(relativePath)) {
            return relativePath;
        }
    }

    // Try include search paths
    for (const auto& searchPath : config.includePaths) {
        std::filesystem::path fullPath = searchPath / includePath;
        if (std::filesystem::exists(fullPath)) {
            return fullPath;
        }
    }

    return std::nullopt; // Not found
}

std::string ShaderPreprocessor::ReadFileToString(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        return "";
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

bool ShaderPreprocessor::IsIncludeDirective(const std::string& line, std::string& outIncludeName)
{
    // Check for #include directive
    std::regex includeRegex(R"(^\s*#\s*include\s+[\"<]([^\">]+)[\">])");
    std::smatch match;

    if (std::regex_search(line, match, includeRegex)) {
        outIncludeName = match[1].str();
        return true;
    }

    return false;
}

std::string ShaderPreprocessor::NormalizeIncludePath(const std::filesystem::path& path)
{
    return path.lexically_normal().string();
}

void ShaderPreprocessor::AddIncludePath(const std::filesystem::path& path) {
    config.includePaths.push_back(path);
}

void ShaderPreprocessor::SetIncludePaths(const std::vector<std::filesystem::path>& paths) {
    config.includePaths = paths;
}

const std::vector<std::filesystem::path>& ShaderPreprocessor::GetIncludePaths() const {
    return config.includePaths;
}

void ShaderPreprocessor::AddGlobalDefine(const std::string& name, const std::string& value) {
    config.globalDefines[name] = value;
}

void ShaderPreprocessor::RemoveGlobalDefine(const std::string& name) {
    config.globalDefines.erase(name);
}

void ShaderPreprocessor::ClearGlobalDefines() {
    config.globalDefines.clear();
}

const std::unordered_map<std::string, std::string>& ShaderPreprocessor::GetGlobalDefines() const {
    return config.globalDefines;
}

std::unordered_map<std::string, std::string> ParseDefinesString(const std::string& definesStr) {
    std::unordered_map<std::string, std::string> defines;

    if (definesStr.empty()) {
        return defines;
    }

    // Split by semicolon or comma
    std::regex splitRegex("[;,]");
    std::sregex_token_iterator iter(definesStr.begin(), definesStr.end(), splitRegex, -1);
    std::sregex_token_iterator end;

    for (; iter != end; ++iter) {
        std::string define = *iter;

        // Trim whitespace
        define.erase(0, define.find_first_not_of(" \t"));
        define.erase(define.find_last_not_of(" \t") + 1);

        if (define.empty()) {
            continue;
        }

        // Split by '=' to get name and value
        size_t eqPos = define.find('=');
        if (eqPos != std::string::npos) {
            std::string name = define.substr(0, eqPos);
            std::string value = define.substr(eqPos + 1);

            // Trim name and value
            name.erase(0, name.find_first_not_of(" \t"));
            name.erase(name.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);

            defines[name] = value;
        }
        else {
            // Simple define without value
            defines[define] = "";
        }
    }

    return defines;
}

std::string DefinesToString(const std::unordered_map<std::string, std::string>& defines) {
    std::stringstream ss;
    bool first = true;

    for (const auto& [name, value] : defines) {
        if (!first) {
            ss << ";";
        }
        first = false;

        ss << name;
        if (!value.empty()) {
            ss << "=" << value;
        }
    }

    return ss.str();
}

} // namespace ShaderManagement
