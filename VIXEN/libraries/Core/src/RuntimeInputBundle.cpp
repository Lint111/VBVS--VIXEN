#include "RuntimeInputBundle.h"

#include "VixenHash.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_set>

namespace Vixen {
namespace {

using Json = nlohmann::json;

constexpr std::string_view kIndexFileName = "runtime-data.index.json";

bool Fail(std::string* error, std::string message) {
    if (error) *error = std::move(message);
    return false;
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool IsSha256(std::string_view value) {
    if (value.size() != 64) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isdigit(c) || (c >= 'a' && c <= 'f');
    });
}

bool ReadDigest(
    const Json& object,
    std::string_view key,
    std::string& output,
    std::string* error,
    std::string_view context) {
    if (!object.contains(key) || !object.at(key).is_string()) {
        return Fail(error, std::string(context) + " missing string " + std::string(key));
    }
    output = Lower(object.at(key).get<std::string>());
    if (!IsSha256(output)) {
        return Fail(error, std::string(context) + " has invalid SHA-256 " + std::string(key));
    }
    return true;
}

bool ReadText(
    const Json& object,
    std::string_view key,
    std::string& output,
    std::string* error,
    std::string_view context) {
    if (!object.contains(key) || !object.at(key).is_string()) {
        return Fail(error, std::string(context) + " missing string " + std::string(key));
    }
    output = object.at(key).get<std::string>();
    if (output.empty()) {
        return Fail(error, std::string(context) + " has empty " + std::string(key));
    }
    return true;
}

bool ReadU32(
    const Json& object,
    std::string_view key,
    uint32_t& output,
    std::string* error,
    std::string_view context) {
    if (!object.contains(key) || !object.at(key).is_number_unsigned() ||
        object.at(key).get<uint64_t>() > std::numeric_limits<uint32_t>::max()) {
        return Fail(error, std::string(context) + " has invalid " + std::string(key));
    }
    output = object.at(key).get<uint32_t>();
    return true;
}

bool ReadU64(
    const Json& object,
    std::string_view key,
    uint64_t& output,
    std::string* error,
    std::string_view context) {
    if (!object.contains(key) || !object.at(key).is_number_unsigned()) {
        return Fail(error, std::string(context) + " has invalid " + std::string(key));
    }
    output = object.at(key).get<uint64_t>();
    return true;
}

bool NormalizePath(std::string value, std::string& output, std::string* error, std::string_view context) {
    if (value.empty() || value.front() == '/' || value.find('\\') != std::string::npos ||
        value.find('\0') != std::string::npos ||
        (value.size() >= 2 && std::isalpha(static_cast<unsigned char>(value[0])) && value[1] == ':')) {
        return Fail(error, std::string(context) + " is not a safe relative path: " + value);
    }

    std::vector<std::string> parts;
    std::size_t begin = 0;
    while (begin <= value.size()) {
        const std::size_t end = value.find('/', begin);
        const std::string part = value.substr(begin, end == std::string::npos ? end : end - begin);
        if (!part.empty() && part != ".") {
            if (part == "..") {
                return Fail(error, std::string(context) + " escapes its bundle root: " + value);
            }
            parts.push_back(part);
        }
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    if (parts.empty()) return Fail(error, std::string(context) + " resolves to an empty path");

    output.clear();
    for (const auto& part : parts) {
        if (!output.empty()) output.push_back('/');
        output += part;
    }
    return true;
}

bool KeysExactly(
    const Json& object,
    std::initializer_list<std::string_view> expected,
    std::string* error,
    std::string_view context) {
    if (!object.is_object()) return Fail(error, std::string(context) + " must be an object");
    std::unordered_set<std::string> allowed;
    for (const auto key : expected) allowed.emplace(key);
    for (const auto& [key, value] : object.items()) {
        (void)value;
        if (!allowed.contains(key)) return Fail(error, std::string(context) + " has unknown field " + key);
    }
    for (const auto key : expected) {
        if (!object.contains(key)) return Fail(error, std::string(context) + " is missing field " + std::string(key));
    }
    return true;
}

bool ReadFile(const std::filesystem::path& path, std::vector<uint8_t>& output, std::string* error) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) return Fail(error, "cannot open runtime artifact '" + path.generic_string() + "'");
    const std::streampos end = input.tellg();
    if (end < 0) return Fail(error, "cannot size runtime artifact '" + path.generic_string() + "'");
    const auto size = static_cast<uint64_t>(end);
    if (size > std::numeric_limits<std::size_t>::max()) {
        return Fail(error, "runtime artifact is too large: '" + path.generic_string() + "'");
    }
    output.resize(static_cast<std::size_t>(size));
    input.seekg(0, std::ios::beg);
    if (!output.empty()) {
        input.read(reinterpret_cast<char*>(output.data()), static_cast<std::streamsize>(output.size()));
        if (!input) return Fail(error, "short read of runtime artifact '" + path.generic_string() + "'");
    }
    return true;
}

bool IsWithin(const std::filesystem::path& root, const std::filesystem::path& candidate) {
    std::error_code ec;
    const auto relative = std::filesystem::relative(candidate, root, ec);
    if (ec || relative.empty() || relative.is_absolute()) return false;
    for (const auto& component : relative) {
        if (component == "..") return false;
    }
    return true;
}

std::optional<uint32_t> ReadShapeMarker(std::string_view text, std::string_view marker) {
    std::istringstream input{std::string(text)};
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream row(line);
        std::string directive;
        std::string token;
        if (!(row >> directive) || directive != marker || !(row >> token)) continue;
        const bool hexadecimal = token.starts_with("0x");
        if (hexadecimal) token.erase(0, 2);
        uint32_t value = 0;
        const auto result = std::from_chars(
            token.data(), token.data() + token.size(), value, hexadecimal ? 16 : 10);
        if (result.ec != std::errc{} || result.ptr != token.data() + token.size()) return std::nullopt;
        return value;
    }
    return std::nullopt;
}

bool MatchExpected(
    std::string_view field,
    std::string_view actual,
    const std::optional<std::string>& expected,
    std::string* error) {
    if (expected && Lower(*expected) != actual) {
        return Fail(error, std::string("runtime input ") + std::string(field) + " mismatch: expected " +
                    *expected + ", got " + std::string(actual));
    }
    return true;
}

bool ValidateSpirv(const RuntimeInputArtifact& artifact, std::string* error) {
    const std::string lowerKind = Lower(artifact.kind);
    const std::string lowerPath = Lower(artifact.relativePath);
    if (lowerKind.find("spirv") == std::string::npos && !lowerPath.ends_with(".spv")) return true;
    if (artifact.bytes.size() < sizeof(uint32_t) || artifact.bytes.size() % sizeof(uint32_t) != 0) {
        return Fail(error, "SPIR-V artifact has an invalid byte length: " + artifact.relativePath);
    }
    uint32_t magic = 0;
    std::memcpy(&magic, artifact.bytes.data(), sizeof(magic));
    if (magic != 0x07230203u) {
        return Fail(error, "SPIR-V artifact has an invalid magic: " + artifact.relativePath);
    }
    return true;
}

std::string_view ArtifactTextView(const RuntimeInputArtifact& artifact) {
    return std::string_view(reinterpret_cast<const char*>(artifact.bytes.data()), artifact.bytes.size());
}

bool ValidateStructuredReferences(
    const std::vector<RuntimeInputArtifact>& artifacts,
    std::string* error) {
    std::vector<const RuntimeInputArtifact*> frameArtifacts;
    std::vector<const RuntimeInputArtifact*> gpuArtifacts;
    std::vector<const RuntimeInputArtifact*> reflectionArtifacts;
    std::vector<const RuntimeInputArtifact*> spirvArtifacts;
    for (const auto& artifact : artifacts) {
        const std::string kind = Lower(artifact.kind);
        if (kind.find("frame-target") != std::string::npos || kind.find("frame_target") != std::string::npos)
            frameArtifacts.push_back(&artifact);
        if (kind.find("gpu-system") != std::string::npos || kind.find("gpu_system") != std::string::npos)
            gpuArtifacts.push_back(&artifact);
        if (kind.find("reflection") != std::string::npos) reflectionArtifacts.push_back(&artifact);
        if (kind.find("spirv") != std::string::npos || Lower(artifact.relativePath).ends_with(".spv"))
            spirvArtifacts.push_back(&artifact);
    }

    const auto dependsOnFamily = [](const RuntimeInputArtifact& source,
                                    const std::vector<const RuntimeInputArtifact*>& family) {
        return std::any_of(source.dependencies.begin(), source.dependencies.end(), [&](const auto& dependency) {
            return std::any_of(family.begin(), family.end(), [&](const auto* target) {
                return target->relativePath == dependency;
            });
        });
    };

    for (const auto* frame : frameArtifacts) {
        if (!gpuArtifacts.empty() && !dependsOnFamily(*frame, gpuArtifacts)) {
            return Fail(error, "frame-target artifact does not reference a GPU-system binding: " + frame->relativePath);
        }
    }
    for (const auto* gpu : gpuArtifacts) {
        if (!reflectionArtifacts.empty() && !dependsOnFamily(*gpu, reflectionArtifacts)) {
            return Fail(error, "GPU-system artifact does not reference shader reflection: " + gpu->relativePath);
        }
    }
    for (const auto* reflection : reflectionArtifacts) {
        if (!spirvArtifacts.empty() && !dependsOnFamily(*reflection, spirvArtifacts)) {
            return Fail(error, "shader reflection does not reference SPIR-V: " + reflection->relativePath);
        }
    }
    return true;
}

}  // namespace

RuntimeInputBundle::LoadResult RuntimeInputBundle::Load(
    const std::filesystem::path& indexOrBundleRoot,
    const RuntimeInputValidationOptions& options) {
    LoadResult result;
    try {
        std::error_code ec;
        std::filesystem::path indexPath = indexOrBundleRoot;
        if (std::filesystem::is_directory(indexPath, ec)) indexPath /= kIndexFileName;
        if (ec || !std::filesystem::is_regular_file(indexPath, ec) || ec) {
            result.error = "runtime-data index is not a regular file: " + indexPath.generic_string();
            return result;
        }
        indexPath = std::filesystem::weakly_canonical(indexPath, ec);
        if (ec) {
            result.error = "cannot canonicalize runtime-data index: " + ec.message();
            return result;
        }

        std::vector<uint8_t> indexBytes;
        if (!ReadFile(indexPath, indexBytes, &result.error)) return result;
        const Json document = Json::parse(indexBytes.begin(), indexBytes.end(), nullptr, false);
        if (document.is_discarded()) {
            result.error = "runtime-data index is malformed JSON: " + indexPath.generic_string();
            return result;
        }

        auto bundle = std::shared_ptr<RuntimeInputBundle>(new RuntimeInputBundle());
        bundle->indexPath_ = indexPath;
        bundle->rootDirectory_ = indexPath.parent_path();
        if (!KeysExactly(document, {"runtimeDataFormat", "productKey", "producer", "abi", "artifacts"},
                         &result.error, "runtime-data index")) return result;

        uint32_t format = 0;
        if (!ReadU32(document, "runtimeDataFormat", format, &result.error, "runtime-data index") ||
            format != kFormatVersion) {
            result.error = "runtime-data format mismatch: expected 2";
            return result;
        }
        if (!ReadDigest(document, "productKey", bundle->identity_.productKey, &result.error, "runtime-data index"))
            return result;
        if (!document.at("producer").is_object() || !document.at("abi").is_object() ||
            !document.at("artifacts").is_array()) {
            result.error = "runtime-data index has invalid producer, abi, or artifacts shape";
            return result;
        }

        const Json& producer = document.at("producer");
        if (!KeysExactly(producer, {"pins", "codegenToolKey", "facadeSchemaDigest", "contentDigest"},
                         &result.error, "producer")) return result;
        if (!producer.at("pins").is_array() || producer.at("pins").empty()) {
            result.error = "producer.pins must be a non-empty array";
            return result;
        }
        std::unordered_set<std::string> pinNames;
        for (std::size_t i = 0; i < producer.at("pins").size(); ++i) {
            const Json& pin = producer.at("pins").at(i);
            const std::string context = "producer.pins[" + std::to_string(i) + "]";
            if (!KeysExactly(pin, {"name", "value"}, &result.error, context)) return result;
            std::string name;
            std::string value;
            if (!ReadText(pin, "name", name, &result.error, context) ||
                !ReadText(pin, "value", value, &result.error, context)) return result;
            if (!pinNames.insert(name).second) {
                result.error = "duplicate producer pin: " + name;
                return result;
            }
            bundle->identity_.producerPins.push_back({std::move(name), std::move(value)});
        }
        std::sort(bundle->identity_.producerPins.begin(), bundle->identity_.producerPins.end(),
                  [](const auto& left, const auto& right) { return left.name < right.name; });
        if (!ReadDigest(producer, "codegenToolKey", bundle->identity_.codegenToolKey, &result.error, "producer") ||
            !ReadDigest(producer, "facadeSchemaDigest", bundle->identity_.facadeSchemaDigest, &result.error, "producer") ||
            !ReadDigest(producer, "contentDigest", bundle->identity_.contentDigest, &result.error, "producer")) return result;

        const Json& abi = document.at("abi");
        if (!KeysExactly(abi, {"cpuGpuLayoutDigest", "shaderCompilerIdentity", "shaderCompilerDigest", "artifactSchemaDigest"},
                         &result.error, "abi")) return result;
        if (!ReadDigest(abi, "cpuGpuLayoutDigest", bundle->identity_.cpuGpuLayoutDigest, &result.error, "abi") ||
            !ReadText(abi, "shaderCompilerIdentity", bundle->identity_.shaderCompilerIdentity, &result.error, "abi") ||
            !ReadDigest(abi, "shaderCompilerDigest", bundle->identity_.shaderCompilerDigest, &result.error, "abi") ||
            !ReadDigest(abi, "artifactSchemaDigest", bundle->identity_.artifactSchemaDigest, &result.error, "abi")) return result;

        if (!MatchExpected("productKey", bundle->identity_.productKey, options.productKey, &result.error) ||
            !MatchExpected("facadeSchemaDigest", bundle->identity_.facadeSchemaDigest, options.facadeSchemaDigest, &result.error) ||
            !MatchExpected("cpuGpuLayoutDigest", bundle->identity_.cpuGpuLayoutDigest, options.cpuGpuLayoutDigest, &result.error) ||
            !MatchExpected("shaderCompilerDigest", bundle->identity_.shaderCompilerDigest, options.shaderCompilerDigest, &result.error) ||
            !MatchExpected("artifactSchemaDigest", bundle->identity_.artifactSchemaDigest, options.artifactSchemaDigest, &result.error) ||
            !MatchExpected("shaderCompilerIdentity", bundle->identity_.shaderCompilerIdentity, options.shaderCompilerIdentity, &result.error)) return result;

        for (const auto& expectedPin : options.producerPins) {
            const auto pin = std::find_if(bundle->identity_.producerPins.begin(), bundle->identity_.producerPins.end(),
                                          [&](const auto& actual) { return actual.name == expectedPin.name; });
            if (pin == bundle->identity_.producerPins.end() || pin->value != expectedPin.value) {
                result.error = "producer pin mismatch: " + expectedPin.name;
                return result;
            }
        }

        std::unordered_set<std::string> artifactPaths;
        std::string previousSortKey;
        for (std::size_t i = 0; i < document.at("artifacts").size(); ++i) {
            const Json& source = document.at("artifacts").at(i);
            const std::string context = "artifacts[" + std::to_string(i) + "]";
            if (!KeysExactly(source, {"kind", "relativePath", "formatVersion", "bytes", "sha256", "dependencies"},
                             &result.error, context)) return result;
            RuntimeInputArtifact artifact;
            if (!ReadText(source, "kind", artifact.kind, &result.error, context) ||
                !ReadText(source, "relativePath", artifact.relativePath, &result.error, context) ||
                !NormalizePath(artifact.relativePath, artifact.relativePath, &result.error, context + ".relativePath") ||
                !ReadU32(source, "formatVersion", artifact.formatVersion, &result.error, context) ||
                artifact.formatVersion == 0 || !ReadU64(source, "bytes", artifact.declaredBytes, &result.error, context) ||
                !ReadDigest(source, "sha256", artifact.sha256, &result.error, context)) return result;
            if (!source.at("dependencies").is_array()) {
                result.error = context + ".dependencies must be an array";
                return result;
            }
            for (std::size_t dependencyIndex = 0; dependencyIndex < source.at("dependencies").size(); ++dependencyIndex) {
                const Json& dependency = source.at("dependencies").at(dependencyIndex);
                if (!dependency.is_string()) {
                    result.error = context + ".dependencies contains a non-string";
                    return result;
                }
                std::string normalized;
                if (!NormalizePath(dependency.get<std::string>(), normalized, &result.error,
                                   context + ".dependencies")) return result;
                artifact.dependencies.push_back(std::move(normalized));
            }
            if (!std::is_sorted(artifact.dependencies.begin(), artifact.dependencies.end()) ||
                std::adjacent_find(artifact.dependencies.begin(), artifact.dependencies.end()) != artifact.dependencies.end()) {
                result.error = context + ".dependencies is not unique and sorted";
                return result;
            }
            if (!artifactPaths.insert(artifact.relativePath).second) {
                result.error = "duplicate runtime artifact path: " + artifact.relativePath;
                return result;
            }
            const std::string sortKey = artifact.relativePath + "\x1f" + artifact.kind;
            if (!previousSortKey.empty() && sortKey < previousSortKey) {
                result.error = "runtime artifacts are not in canonical path/kind order";
                return result;
            }
            previousSortKey = sortKey;
            if (artifact.relativePath == kIndexFileName || artifact.relativePath == "manifest.json") {
                result.error = "runtime artifact path is reserved: " + artifact.relativePath;
                return result;
            }

            const std::filesystem::path memberPath = bundle->rootDirectory_ / artifact.relativePath;
            const auto canonicalMember = std::filesystem::weakly_canonical(memberPath, ec);
            if (ec || !std::filesystem::is_regular_file(canonicalMember, ec) || ec ||
                !IsWithin(bundle->rootDirectory_, canonicalMember)) {
                result.error = "runtime artifact is missing or escapes the bundle root: " + artifact.relativePath;
                return result;
            }
            if (!ReadFile(canonicalMember, artifact.bytes, &result.error)) return result;
            if (artifact.declaredBytes != artifact.bytes.size()) {
                result.error = "runtime artifact byte count mismatch at " + artifact.relativePath;
                return result;
            }
            if (Vixen::Hash::ComputeSHA256Hex(artifact.bytes.data(), artifact.bytes.size()) != artifact.sha256) {
                result.error = "runtime artifact digest mismatch at " + artifact.relativePath;
                return result;
            }
            if (!ValidateSpirv(artifact, &result.error)) return result;
            const std::string lowerPath = Lower(artifact.relativePath);
            if (lowerPath.ends_with(".json")) {
                const Json structured = Json::parse(artifact.bytes.begin(), artifact.bytes.end(), nullptr, false);
                if (structured.is_discarded()) {
                    result.error = "runtime JSON artifact is malformed at " + artifact.relativePath;
                    return result;
                }
            }
            if (lowerPath.ends_with(".appflow") || Lower(artifact.kind) == "ui.appflow") {
                bundle->appFlowShapeHash_ = ReadShapeMarker(ArtifactTextView(artifact), "shape");
                if (!bundle->appFlowShapeHash_) {
                    result.error = "AppFlow artifact has no valid shape marker at " + artifact.relativePath;
                    return result;
                }
            }
            if (lowerPath.ends_with(".viewblob")) {
                bundle->viewShapeHash_ = ReadShapeMarker(ArtifactTextView(artifact), "version");
                if (!bundle->viewShapeHash_) {
                    result.error = "viewblob artifact has no valid version marker at " + artifact.relativePath;
                    return result;
                }
            }
            bundle->artifacts_.push_back(std::move(artifact));
        }
        if (bundle->artifacts_.empty()) {
            result.error = "runtime-data index has no artifacts";
            return result;
        }
        for (const auto& artifact : bundle->artifacts_) {
            for (const auto& dependency : artifact.dependencies) {
                if (!artifactPaths.contains(dependency) || dependency == artifact.relativePath) {
                    result.error = "invalid artifact dependency from " + artifact.relativePath + ": " + dependency;
                    return result;
                }
            }
        }
        const auto schema = bundle->FindArtifactByPath("undertow/schemas.json");
        if (schema && schema->sha256 != bundle->identity_.facadeSchemaDigest) {
            result.error = "facade/schema catalog digest does not match producer identity";
            return result;
        }
        if (!ValidateStructuredReferences(bundle->artifacts_, &result.error)) return result;
        for (const auto& requiredKind : options.requiredArtifactKinds) {
            if (!bundle->HasArtifactKind(requiredKind)) {
                result.error = "required runtime artifact kind is missing: " + requiredKind;
                return result;
            }
        }
        for (const auto& requiredPath : options.requiredArtifactPaths) {
            if (!bundle->FindArtifactByPath(requiredPath)) {
                result.error = "required runtime artifact path is missing: " + requiredPath;
                return result;
            }
        }
        if (options.expectedAppFlowShapeHash && bundle->appFlowShapeHash_ != options.expectedAppFlowShapeHash) {
            result.error = "AppFlow shape hash mismatch before graph construction";
            return result;
        }
        if (options.expectedViewShapeHash && bundle->viewShapeHash_ != options.expectedViewShapeHash) {
            result.error = "view shape hash mismatch before graph construction";
            return result;
        }
        result.bundle = std::move(bundle);
        return result;
    } catch (const std::exception& exception) {
        result.error = std::string("runtime-data load failed: ") + exception.what();
        return result;
    } catch (...) {
        result.error = "runtime-data load failed with an unknown error";
        return result;
    }
}

const RuntimeInputArtifact* RuntimeInputBundle::FindArtifactByPath(std::string_view relativePath) const noexcept {
    for (const auto& artifact : artifacts_) {
        if (artifact.relativePath == relativePath) return &artifact;
    }
    return nullptr;
}

bool RuntimeInputBundle::HasArtifactKind(std::string_view kind) const noexcept {
    return std::any_of(artifacts_.begin(), artifacts_.end(), [&](const auto& artifact) {
        return artifact.kind == kind;
    });
}

bool RuntimeInputBundle::HasArtifactKindContaining(std::string_view fragment) const noexcept {
    return std::any_of(artifacts_.begin(), artifacts_.end(), [&](const auto& artifact) {
        return artifact.kind.find(fragment) != std::string::npos;
    });
}

std::span<const uint8_t> RuntimeInputBundle::ArtifactBytes(std::string_view relativePath) const noexcept {
    const auto* artifact = FindArtifactByPath(relativePath);
    return artifact ? std::span<const uint8_t>(artifact->bytes) : std::span<const uint8_t>{};
}

std::string_view RuntimeInputBundle::ArtifactText(std::string_view relativePath) const noexcept {
    const auto bytes = ArtifactBytes(relativePath);
    return std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

bool RuntimeInputCompatibilityAdapter::Compare(const RuntimeInputBundle& bundle, std::string* error) const {
    if (legacyAppFlowShapeHash_ && bundle.AppFlowShapeHash() != legacyAppFlowShapeHash_) {
        return Fail(error, "legacy/new AppFlow shape identity differs");
    }
    if (legacyViewShapeHash_ && bundle.ViewShapeHash() != legacyViewShapeHash_) {
        return Fail(error, "legacy/new view shape identity differs");
    }
    if (legacyCpuGpuLayoutDigest_ && bundle.Identity().cpuGpuLayoutDigest != *legacyCpuGpuLayoutDigest_) {
        return Fail(error, "legacy/new CPU/GPU layout identity differs");
    }
    return true;
}

bool RuntimeInputBundleStore::LoadBoot(
    const std::filesystem::path& indexOrBundleRoot,
    RuntimeInputValidationOptions options,
    std::string* error) {
    auto loaded = RuntimeInputBundle::Load(indexOrBundleRoot, options);
    if (!loaded) return Fail(error, std::move(loaded.error));
    std::lock_guard lock(mutex_);
    options_ = std::move(options);
    active_ = std::move(loaded.bundle);
    pending_.reset();
    return true;
}

bool RuntimeInputBundleStore::RequestReload(
    const std::filesystem::path& indexOrBundleRoot,
    std::string* error) {
    RuntimeInputValidationOptions options;
    std::shared_ptr<const RuntimeInputBundle> active;
    {
        std::lock_guard lock(mutex_);
        options = options_;
        active = active_;
    }
    if (!active) return Fail(error, "runtime-data reload requested without an active boot bundle");

    auto loaded = RuntimeInputBundle::Load(indexOrBundleRoot, options);
    if (!loaded) return Fail(error, std::move(loaded.error));
    const auto& candidateIdentity = loaded.bundle->Identity();
    const auto& activeIdentity = active->Identity();
    if (candidateIdentity.facadeSchemaDigest != activeIdentity.facadeSchemaDigest ||
        candidateIdentity.artifactSchemaDigest != activeIdentity.artifactSchemaDigest ||
        candidateIdentity.cpuGpuLayoutDigest != activeIdentity.cpuGpuLayoutDigest ||
        candidateIdentity.shaderCompilerIdentity != activeIdentity.shaderCompilerIdentity ||
        candidateIdentity.shaderCompilerDigest != activeIdentity.shaderCompilerDigest ||
        loaded.bundle->AppFlowShapeHash() != active->AppFlowShapeHash() ||
        loaded.bundle->ViewShapeHash() != active->ViewShapeHash()) {
        return Fail(error, "runtime-data reload rejected: shader/layout/facade identity is incompatible");
    }
    std::lock_guard lock(mutex_);
    pending_ = std::move(loaded.bundle);
    return true;
}

bool RuntimeInputBundleStore::CommitFrameBoundary() {
    std::lock_guard lock(mutex_);
    if (!pending_) return false;
    active_ = std::move(pending_);
    return true;
}

std::shared_ptr<const RuntimeInputBundle> RuntimeInputBundleStore::Active() const {
    std::lock_guard lock(mutex_);
    return active_;
}

bool RuntimeInputBundleStore::HasPendingReload() const {
    std::lock_guard lock(mutex_);
    return pending_ != nullptr;
}

}  // namespace Vixen
