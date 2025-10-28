#include "PipelineCacher.h"
#include "Hash.h"

#include <functional>

namespace CashSystem {

using PtrT = PipelineCacher::PtrT;

PtrT PipelineCacher::Create(const PipelineCreateParams& ci) {
    // Placeholder creation: produce a simple PipelineWrapper instance.
    auto w = std::make_shared<PipelineWrapper>();
    w->placeholder = 1; // indicate created
    (void)ci;
    return w;
}

std::uint64_t PipelineCacher::ComputeKey(const PipelineCreateParams& ci) const {
    // Canonicalize CI into a deterministic byte sequence and compute SHA256 (hex)
    std::string blob;
    blob.reserve(ci.vertexShaderChecksum.size() + ci.fragmentShaderChecksum.size() + ci.layoutKey.size() + ci.renderPassKey.size() + 8);
    blob.append(ci.vertexShaderChecksum);
    blob.push_back('|');
    blob.append(ci.fragmentShaderChecksum);
    blob.push_back('|');
    blob.append(ci.layoutKey);
    blob.push_back('|');
    blob.append(ci.renderPassKey);

    auto hex = Vixen::Hash::ComputeSHA256Hex(reinterpret_cast<const void*>(blob.data()), blob.size());
    // Reduce SHA256 hex to a 64-bit key (std::hash on hex is deterministic)
    return static_cast<std::uint64_t>(std::hash<std::string>{}(hex));
}

bool PipelineCacher::SerializeToFile(const std::filesystem::path& path) const {
    (void)path; // implement later
    return true;
}

bool PipelineCacher::DeserializeFromFile(const std::filesystem::path& path, void* device) {
    (void)path; (void)device; // implement later
    return true;
}

} // namespace CashSystem
