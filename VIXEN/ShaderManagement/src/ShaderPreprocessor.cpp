#include "ShaderManagement/ShaderPreprocessor.h"
#include <fstream>
#include <sstream>
#include <regex>
#include <unordered_set>

namespace ShaderManagement {

ShaderPreprocessor::ShaderPreprocessor(const PreprocessorConfig& config)
    : config_(config)
{
}

PreprocessedSource ShaderPreprocessor::Preprocess(
    const std::string& source,
    const std::unordered_map<std::string, std::string>& defines,
    const std::filesystem::path& currentFilePath)
{
    PreprocessedSource result;
    result.success = false;

    // Merge global and local defines
    std::unordered_map<std::string, std::string> allDefines = config_.globalDefines;
    allDefines.insert(defines.begin(), defines.end());

    // Track included files to prevent circular includes
    std::unordered_set<std::string> includedFiles;
    uint32_t includeDepth = 0;

    // Process source
    std::string processed = ProcessSource(
        source,
        currentFilePath,
        allDefines,
        includedFiles,
        includeDepth,
        result.errorMessage
    );

    if (result.errorMessage.empty()) {
        result.processedSource = processed;
        result.success = true;

        // Copy included files
        for (const auto& file : includedFiles) {
            result.includedFiles.push_back(file);
        }
    }

    return result;
}

PreprocessedSource ShaderPreprocessor::PreprocessFile(
    const std::filesystem::path& filePath,
    const std::unordered_map<std::string, std::string>& defines)
{
    // Read file
    std::ifstream file(filePath);
    if (!file.is_open()) {
        PreprocessedSource result;
        result.success = false;
        result.errorMessage = "Failed to open file: " + filePath.string();
        return result;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();

    return Preprocess(source, defines, filePath);
}

std::string ShaderPreprocessor::ProcessSource(
    const std::string& source,
    const std::filesystem::path& currentFilePath,
    const std::unordered_map<std::string, std::string>& defines,
    std::unordered_set<std::string>& includedFiles,
    uint32_t& includeDepth,
    std::string& errorMessage)
{
    if (includeDepth >= config_.maxIncludeDepth) {
        errorMessage = "Maximum include depth exceeded (" + std::to_string(config_.maxIncludeDepth) + ")";
        return "";
    }

    std::stringstream output;
    std::istringstream input(source);
    std::string line;
    int lineNumber = 0;

    while (std::getline(input, line)) {
        ++lineNumber;

        // Check for #include directive
        std::regex includeRegex(R"(^\s*#\s*include\s+[\"<]([^">]+)[\">])");
        std::smatch match;

        if (std::regex_search(line, match, includeRegex)) {
            std::string includePath = match[1].str();

            // Resolve include path
            std::filesystem::path resolvedPath = ResolveIncludePath(includePath, currentFilePath);

            if (resolvedPath.empty()) {
                errorMessage = "Failed to resolve include: " + includePath + " at line " + std::to_string(lineNumber);
                return "";
            }

            // Check for circular includes
            std::string canonicalPath = std::filesystem::canonical(resolvedPath).string();
            if (includedFiles.count(canonicalPath) > 0) {
                errorMessage = "Circular include detected: " + canonicalPath;
                return "";
            }

            // Read included file
            std::ifstream includeFile(resolvedPath);
            if (!includeFile.is_open()) {
                errorMessage = "Failed to open include file: " + resolvedPath.string();
                return "";
            }

            std::stringstream includeBuffer;
            includeBuffer << includeFile.rdbuf();
            std::string includeSource = includeBuffer.str();

            // Mark as included
            includedFiles.insert(canonicalPath);

            // Recursively process included file
            includeDepth++;
            std::string processedInclude = ProcessSource(
                includeSource,
                resolvedPath,
                defines,
                includedFiles,
                includeDepth,
                errorMessage
            );
            includeDepth--;

            if (!errorMessage.empty()) {
                return "";
            }

            // Add line directive if enabled
            if (config_.enableLineDirectives) {
                output << "#line 1 \"" << resolvedPath.string() << "\"\n";
            }

            output << processedInclude << "\n";

            // Restore line directive
            if (config_.enableLineDirectives) {
                output << "#line " << (lineNumber + 1) << " \"" << currentFilePath.string() << "\"\n";
            }
        }
        else {
            // Not an include - process defines
            std::string processedLine = ProcessDefines(line, defines);
            output << processedLine << "\n";
        }
    }

    return output.str();
}

std::string ShaderPreprocessor::ProcessDefines(
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

std::filesystem::path ShaderPreprocessor::ResolveIncludePath(
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
    for (const auto& searchPath : config_.includePaths) {
        std::filesystem::path fullPath = searchPath / includePath;
        if (std::filesystem::exists(fullPath)) {
            return fullPath;
        }
    }

    return {}; // Not found
}

void ShaderPreprocessor::AddIncludePath(const std::filesystem::path& path) {
    config_.includePaths.push_back(path);
}

void ShaderPreprocessor::SetIncludePaths(const std::vector<std::filesystem::path>& paths) {
    config_.includePaths = paths;
}

const std::vector<std::filesystem::path>& ShaderPreprocessor::GetIncludePaths() const {
    return config_.includePaths;
}

void ShaderPreprocessor::AddGlobalDefine(const std::string& name, const std::string& value) {
    config_.globalDefines[name] = value;
}

void ShaderPreprocessor::RemoveGlobalDefine(const std::string& name) {
    config_.globalDefines.erase(name);
}

void ShaderPreprocessor::ClearGlobalDefines() {
    config_.globalDefines.clear();
}

const std::unordered_map<std::string, std::string>& ShaderPreprocessor::GetGlobalDefines() const {
    return config_.globalDefines;
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
