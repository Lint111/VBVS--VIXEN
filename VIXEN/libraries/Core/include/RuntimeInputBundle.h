#pragma once

#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Vixen {

struct RuntimeInputProducerPin {
    std::string name;
    std::string value;
};

struct RuntimeInputIdentity {
    std::string productKey;
    std::vector<RuntimeInputProducerPin> producerPins;
    std::string codegenToolKey;
    std::string facadeSchemaDigest;
    std::string contentDigest;
    std::string cpuGpuLayoutDigest;
    std::string shaderCompilerIdentity;
    std::string shaderCompilerDigest;
    std::string artifactSchemaDigest;
};

struct RuntimeInputArtifact {
    std::string kind;
    std::string relativePath;
    uint32_t formatVersion = 0;
    uint64_t declaredBytes = 0;
    std::string sha256;
    std::vector<std::string> dependencies;
    std::vector<uint8_t> bytes;
};

// Values supplied by the product resolver or the native host. Empty optionals mean that the
// index's self-describing value is accepted after its syntax and member bytes have been checked.
struct RuntimeInputValidationOptions {
    std::optional<std::string> productKey;
    std::optional<std::string> facadeSchemaDigest;
    std::optional<std::string> cpuGpuLayoutDigest;
    std::optional<std::string> shaderCompilerDigest;
    std::optional<std::string> artifactSchemaDigest;
    std::optional<std::string> shaderCompilerIdentity;
    std::vector<RuntimeInputProducerPin> producerPins;
    std::vector<std::string> requiredArtifactKinds;
    std::vector<std::string> requiredArtifactPaths;
    std::optional<uint32_t> expectedAppFlowShapeHash;
    std::optional<uint32_t> expectedViewShapeHash;
};

class RuntimeInputBundle {
public:
    static constexpr uint32_t kFormatVersion = 2;

    struct LoadResult {
        std::shared_ptr<const RuntimeInputBundle> bundle;
        std::string error;

        explicit operator bool() const noexcept { return bundle != nullptr; }
    };

    static LoadResult Load(
        const std::filesystem::path& indexOrBundleRoot,
        const RuntimeInputValidationOptions& options = {});

    const std::filesystem::path& RootDirectory() const noexcept { return rootDirectory_; }
    const std::filesystem::path& IndexPath() const noexcept { return indexPath_; }
    const RuntimeInputIdentity& Identity() const noexcept { return identity_; }
    std::size_t ArtifactCount() const noexcept { return artifacts_.size(); }

    const RuntimeInputArtifact* FindArtifactByPath(std::string_view relativePath) const noexcept;
    bool HasArtifactKind(std::string_view kind) const noexcept;
    bool HasArtifactKindContaining(std::string_view fragment) const noexcept;
    std::span<const uint8_t> ArtifactBytes(std::string_view relativePath) const noexcept;
    std::string_view ArtifactText(std::string_view relativePath) const noexcept;

    // The existing .appflow/.viewblob readers use these shape markers. They are exposed so a
    // compatibility adapter can compare the old compiled path with a bundle without asking the
    // renderer to infer a new layout.
    std::optional<uint32_t> AppFlowShapeHash() const noexcept { return appFlowShapeHash_; }
    std::optional<uint32_t> ViewShapeHash() const noexcept { return viewShapeHash_; }

private:
    std::filesystem::path rootDirectory_;
    std::filesystem::path indexPath_;
    RuntimeInputIdentity identity_;
    std::vector<RuntimeInputArtifact> artifacts_;
    std::optional<uint32_t> appFlowShapeHash_;
    std::optional<uint32_t> viewShapeHash_;
};

// The migration adapter is intentionally small: the old generated/header path remains the
// fallback for members not yet exported by the producer. CI or a host may provide the old
// shape/layout identities and compare them before opting into the bundle.
class RuntimeInputCompatibilityAdapter {
public:
    RuntimeInputCompatibilityAdapter() = default;
    RuntimeInputCompatibilityAdapter(
        std::optional<uint32_t> legacyAppFlowShapeHash,
        std::optional<uint32_t> legacyViewShapeHash,
        std::optional<std::string> legacyCpuGpuLayoutDigest)
        : legacyAppFlowShapeHash_(legacyAppFlowShapeHash),
          legacyViewShapeHash_(legacyViewShapeHash),
          legacyCpuGpuLayoutDigest_(std::move(legacyCpuGpuLayoutDigest)) {}

    bool Compare(const RuntimeInputBundle& bundle, std::string* error = nullptr) const;
    bool HasLegacyIdentity() const noexcept {
        return legacyAppFlowShapeHash_.has_value() || legacyViewShapeHash_.has_value() ||
               legacyCpuGpuLayoutDigest_.has_value();
    }

private:
    std::optional<uint32_t> legacyAppFlowShapeHash_;
    std::optional<uint32_t> legacyViewShapeHash_;
    std::optional<std::string> legacyCpuGpuLayoutDigest_;
};

// Owns the active immutable generation. LoadBoot and RequestReload fully materialize a separate
// bundle; only CommitFrameBoundary changes the generation visible to the frame path.
class RuntimeInputBundleStore {
public:
    bool LoadBoot(
        const std::filesystem::path& indexOrBundleRoot,
        RuntimeInputValidationOptions options = {},
        std::string* error = nullptr);

    bool RequestReload(const std::filesystem::path& indexOrBundleRoot, std::string* error = nullptr);
    bool CommitFrameBoundary();
    std::shared_ptr<const RuntimeInputBundle> Active() const;
    bool HasPendingReload() const;

private:
    mutable std::mutex mutex_;
    RuntimeInputValidationOptions options_;
    std::shared_ptr<const RuntimeInputBundle> active_;
    std::shared_ptr<const RuntimeInputBundle> pending_;
};

}  // namespace Vixen
