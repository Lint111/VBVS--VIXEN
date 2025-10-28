#include "PipelineCacher.h"

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
    std::uint64_t key = 1469598103934665603ull;
    auto h1 = std::hash<std::string>{}(ci.vertexShaderChecksum);
    auto h2 = std::hash<std::string>{}(ci.fragmentShaderChecksum);
    auto h3 = std::hash<std::string>{}(ci.layoutKey);
    auto h4 = std::hash<std::string>{}(ci.renderPassKey);

    key ^= (h1 + 0x9e3779b97f4a7c15ULL + (key<<6) + (key>>2));
    key ^= (h2 + 0x9e3779b97f4a7c15ULL + (key<<6) + (key>>2));
    key ^= (h3 + 0x9e3779b97f4a7c15ULL + (key<<6) + (key>>2));
    key ^= (h4 + 0x9e3779b97f4a7c15ULL + (key<<6) + (key>>2));
    return key;
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
