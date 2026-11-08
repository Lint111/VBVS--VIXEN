#include "ShaderReflectionCache.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace ShaderManagement {

namespace {

constexpr uint32_t kPayloadMagic = 0x4C464552;  // 'REFL'
constexpr int kMaxMemberDepth = 32;
constexpr uint64_t kMaxElements = 1u << 20;     // sanity bound against corrupt counts

class Writer {
public:
    template <typename T>
    void Pod(T value) {
        static_assert(std::is_trivially_copyable_v<T>);
        const auto* p = reinterpret_cast<const uint8_t*>(&value);
        bytes_.insert(bytes_.end(), p, p + sizeof(T));
    }
    void Str(const std::string& s) {
        Pod<uint32_t>(static_cast<uint32_t>(s.size()));
        bytes_.insert(bytes_.end(), s.begin(), s.end());
    }
    template <typename E>
    void Enum(E e) { Pod<uint32_t>(static_cast<uint32_t>(e)); }
    const std::vector<uint8_t>& Bytes() const { return bytes_; }

private:
    std::vector<uint8_t> bytes_;
};

class Reader {
public:
    Reader(const uint8_t* data, size_t size) : data_(data), size_(size) {}
    bool Ok() const { return ok_; }
    bool AtEnd() const { return pos_ == size_; }
    void Fail() { ok_ = false; }

    template <typename T>
    T Pod() {
        T value{};
        if (!Take(sizeof(T))) return value;
        std::memcpy(&value, data_ + pos_ - sizeof(T), sizeof(T));
        return value;
    }
    template <typename E>
    E Enum() { return static_cast<E>(Pod<uint32_t>()); }
    std::string Str() {
        const uint32_t n = Pod<uint32_t>();
        if (!Take(n)) return {};
        return std::string(reinterpret_cast<const char*>(data_ + pos_ - n), n);
    }
    uint32_t Count() {
        const uint32_t n = Pod<uint32_t>();
        if (n > kMaxElements) { ok_ = false; return 0; }
        return n;
    }

private:
    bool Take(size_t n) {
        if (!ok_ || n > size_ - pos_) { ok_ = false; return false; }
        pos_ += n;
        return true;
    }
    const uint8_t* data_;
    size_t size_;
    size_t pos_ = 0;
    bool ok_ = true;
};

void WriteType(Writer& w, const SpirvTypeInfo& t) {
    w.Enum(t.baseType);
    w.Pod(t.width); w.Pod(t.vecSize); w.Pod(t.columns); w.Pod(t.rows); w.Pod(t.arraySize);
    w.Enum(t.componentType);
    w.Str(t.structName);
    w.Pod(t.sizeInBytes); w.Pod(t.alignment);
}

SpirvTypeInfo ReadType(Reader& r) {
    SpirvTypeInfo t;
    t.baseType = r.Enum<SpirvTypeInfo::BaseType>();
    t.width = r.Pod<uint32_t>(); t.vecSize = r.Pod<uint32_t>(); t.columns = r.Pod<uint32_t>();
    t.rows = r.Pod<uint32_t>(); t.arraySize = r.Pod<uint32_t>();
    t.componentType = r.Enum<SpirvTypeInfo::BaseType>();
    t.structName = r.Str();
    t.sizeInBytes = r.Pod<uint32_t>(); t.alignment = r.Pod<uint32_t>();
    return t;
}

void WriteMember(Writer& w, const SpirvStructMember& m) {
    w.Str(m.name);
    WriteType(w, m.type);
    w.Pod(m.offset); w.Pod(m.arrayStride); w.Pod(m.matrixStride);
    w.Pod<uint8_t>(m.isRowMajor);
    w.Pod<uint32_t>(static_cast<uint32_t>(m.members.size()));
    for (const auto& sub : m.members) WriteMember(w, sub);
}

SpirvStructMember ReadMember(Reader& r, int depth) {
    SpirvStructMember m;
    m.name = r.Str();
    m.type = ReadType(r);
    m.offset = r.Pod<uint32_t>(); m.arrayStride = r.Pod<uint32_t>();
    m.matrixStride = r.Pod<uint32_t>();
    m.isRowMajor = r.Pod<uint8_t>() != 0;
    const uint32_t n = r.Count();
    if (n != 0 && depth >= kMaxMemberDepth) { r.Fail(); return m; }
    for (uint32_t i = 0; i < n && r.Ok(); ++i) m.members.push_back(ReadMember(r, depth + 1));
    return m;
}

void WriteStruct(Writer& w, const SpirvStructDefinition& s) {
    w.Str(s.name);
    w.Pod<uint32_t>(static_cast<uint32_t>(s.members.size()));
    for (const auto& m : s.members) WriteMember(w, m);
    w.Pod(s.sizeInBytes); w.Pod(s.alignment);
}

SpirvStructDefinition ReadStruct(Reader& r) {
    SpirvStructDefinition s;
    s.name = r.Str();
    const uint32_t n = r.Count();
    for (uint32_t i = 0; i < n && r.Ok(); ++i) s.members.push_back(ReadMember(r, 0));
    s.sizeInBytes = r.Pod<uint32_t>(); s.alignment = r.Pod<uint32_t>();
    return s;
}

template <typename Map, typename WriteValue>
void WriteStageMap(Writer& w, const Map& map, WriteValue writeValue) {
    // Deterministic bytes: the maps are unordered, order is not part of their meaning.
    std::vector<const typename Map::value_type*> sorted;
    for (const auto& kv : map) sorted.push_back(&kv);
    std::sort(sorted.begin(), sorted.end(),
              [](const auto* a, const auto* b) { return a->first < b->first; });
    w.Pod<uint32_t>(static_cast<uint32_t>(sorted.size()));
    for (const auto* kv : sorted) { w.Enum(kv->first); writeValue(kv->second); }
}

} // namespace

ShaderReflectionCache::ShaderReflectionCache(const ShaderCacheManager& spirvCache)
    : store_([&] {
          ShaderCacheConfig config;
          config.cacheDirectory = spirvCache.GetCacheDirectory() / "reflection";
          config.enabled = spirvCache.IsEnabled();
          config.validateCache = false;  // payload is not SPIR-V; the file checksum still applies
          return config;
      }()) {}

std::string ShaderReflectionCache::MakeKey(const CompiledProgram& program) {
    // Two FNV-1a lanes with different seeds -> 128 bits; the reflection depends on exactly
    // these inputs (SpirvReflector::Reflect reads program.name, pipelineType, stage, entry point
    // and SPIR-V words).
    uint64_t lane[2] = {14695981039346656037ULL, 0x9E3779B97F4A7C15ULL};
    auto mix = [&](const void* data, size_t len) {
        const auto* p = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < len; ++i)
            for (uint64_t& h : lane) { h ^= p[i]; h *= 1099511628211ULL; }
    };
    auto mixU32 = [&](uint32_t v) { mix(&v, sizeof(v)); };
    auto mixStr = [&](const std::string& s) { mixU32(static_cast<uint32_t>(s.size())); mix(s.data(), s.size()); };

    mixU32(kReflectionCacheVersion);
    mixStr(program.name);
    mixU32(static_cast<uint32_t>(program.pipelineType));
    mixU32(static_cast<uint32_t>(program.stages.size()));
    for (const auto& stage : program.stages) {
        mixU32(static_cast<uint32_t>(stage.stage));
        mixStr(stage.entryPoint);
        mixU32(static_cast<uint32_t>(stage.spirvCode.size()));
        mix(stage.spirvCode.data(), stage.spirvCode.size() * sizeof(uint32_t));
    }
    char buf[40];
    std::snprintf(buf, sizeof(buf), "refl-%016llx%016llx",
                  static_cast<unsigned long long>(lane[0]), static_cast<unsigned long long>(lane[1]));
    return buf;
}

std::vector<uint32_t> ShaderReflectionCache::Serialize(const SpirvReflectionData& d,
                                                       const DescriptorLayoutSpec* layout) {
    Writer w;
    w.Pod(kPayloadMagic);
    w.Pod(kReflectionCacheVersion);
    w.Str(d.programName);
    w.Enum(d.pipelineType);
    w.Pod<uint32_t>(static_cast<uint32_t>(d.stages.size()));
    for (auto s : d.stages) w.Enum(s);

    // Descriptor sets are written in the live map's iteration order: the SDI generator iterates
    // it, so Deserialize must rebuild that exact order (and verifies it did).
    w.Pod<uint32_t>(static_cast<uint32_t>(d.descriptorSets.size()));
    for (const auto& [setIndex, bindings] : d.descriptorSets) {
        w.Pod(setIndex);
        w.Pod<uint32_t>(static_cast<uint32_t>(bindings.size()));
        for (const auto& b : bindings) {
            w.Pod(b.set); w.Pod(b.binding); w.Str(b.name);
            w.Pod<int32_t>(static_cast<int32_t>(b.descriptorType));
            w.Pod(b.descriptorCount); w.Pod(b.stageFlags);
            w.Pod<uint8_t>(static_cast<uint8_t>(b.access));
            WriteType(w, b.typeInfo);
            w.Pod(b.structDefIndex);
            w.Pod<int32_t>(static_cast<int32_t>(b.imageFormat));
            w.Pod(b.imageDimension);
        }
    }

    w.Pod<uint32_t>(static_cast<uint32_t>(d.pushConstants.size()));
    for (const auto& pc : d.pushConstants) {
        w.Str(pc.name); w.Pod(pc.offset); w.Pod(pc.size); w.Pod(pc.stageFlags);
        WriteStruct(w, pc.structDef);
    }

    w.Pod<uint32_t>(static_cast<uint32_t>(d.vertexInputs.size()));
    for (const auto& v : d.vertexInputs) {
        w.Pod(v.location); w.Str(v.name); w.Pod<int32_t>(static_cast<int32_t>(v.format));
        WriteType(w, v.type);
    }

    auto writeIo = [&](const std::vector<SpirvStageIO>& ios) {
        w.Pod<uint32_t>(static_cast<uint32_t>(ios.size()));
        for (const auto& io : ios) { w.Pod(io.location); w.Str(io.name); WriteType(w, io.type); }
    };
    WriteStageMap(w, d.stageInputs, writeIo);
    WriteStageMap(w, d.stageOutputs, writeIo);

    w.Pod<uint32_t>(static_cast<uint32_t>(d.specializationConstants.size()));
    for (const auto& sc : d.specializationConstants) {
        w.Pod(sc.constantId); w.Str(sc.name); WriteType(w, sc.type); w.Pod(sc.defaultValue);
    }

    w.Pod<uint32_t>(static_cast<uint32_t>(d.structDefinitions.size()));
    for (const auto& s : d.structDefinitions) WriteStruct(w, s);

    w.Str(d.interfaceHash);
    WriteStageMap(w, d.entryPoints, [&](const std::string& e) { w.Str(e); });

    w.Pod<uint8_t>(layout != nullptr);
    if (layout) {
        w.Pod(layout->maxSets);
        w.Pod<uint32_t>(static_cast<uint32_t>(layout->bindings.size()));
        for (const auto& b : layout->bindings) {
            w.Pod(b.binding); w.Pod<int32_t>(static_cast<int32_t>(b.descriptorType));
            w.Pod(b.descriptorCount); w.Pod(b.stageFlags); w.Str(b.name);
        }
    }

    // Payload words: [byte length][bytes zero-padded to a word]
    const auto& bytes = w.Bytes();
    std::vector<uint32_t> words(1 + (bytes.size() + 3) / 4, 0);
    words[0] = static_cast<uint32_t>(bytes.size());
    std::memcpy(words.data() + 1, bytes.data(), bytes.size());
    return words;
}

std::optional<CachedReflection> ShaderReflectionCache::Deserialize(const std::vector<uint32_t>& payload) {
    if (payload.size() < 2) return std::nullopt;
    const size_t byteLen = payload[0];
    if (byteLen > (payload.size() - 1) * sizeof(uint32_t)) return std::nullopt;

    Reader r(reinterpret_cast<const uint8_t*>(payload.data() + 1), byteLen);
    if (r.Pod<uint32_t>() != kPayloadMagic || r.Pod<uint32_t>() != kReflectionCacheVersion) {
        return std::nullopt;
    }

    auto d = std::make_unique<SpirvReflectionData>();
    d->programName = r.Str();
    d->pipelineType = r.Enum<PipelineTypeConstraint>();
    for (uint32_t i = 0, n = r.Count(); i < n && r.Ok(); ++i) d->stages.push_back(r.Enum<ShaderStage>());

    struct SetRecord { uint32_t index; std::vector<SpirvDescriptorBinding> bindings; };
    std::vector<SetRecord> setRecords;
    for (uint32_t i = 0, n = r.Count(); i < n && r.Ok(); ++i) {
        SetRecord rec;
        rec.index = r.Pod<uint32_t>();
        for (uint32_t j = 0, m = r.Count(); j < m && r.Ok(); ++j) {
            SpirvDescriptorBinding b;
            b.set = r.Pod<uint32_t>(); b.binding = r.Pod<uint32_t>(); b.name = r.Str();
            b.descriptorType = static_cast<VkDescriptorType>(r.Pod<int32_t>());
            b.descriptorCount = r.Pod<uint32_t>(); b.stageFlags = r.Pod<uint32_t>();
            b.access = static_cast<SpirvResourceAccess>(r.Pod<uint8_t>());
            b.typeInfo = ReadType(r);
            b.structDefIndex = r.Pod<int32_t>();
            b.imageFormat = static_cast<VkFormat>(r.Pod<int32_t>());
            b.imageDimension = r.Pod<uint32_t>();
            rec.bindings.push_back(std::move(b));
        }
        setRecords.push_back(std::move(rec));
    }

    for (uint32_t i = 0, n = r.Count(); i < n && r.Ok(); ++i) {
        SpirvPushConstantRange pc;
        pc.name = r.Str(); pc.offset = r.Pod<uint32_t>(); pc.size = r.Pod<uint32_t>();
        pc.stageFlags = r.Pod<uint32_t>();
        pc.structDef = ReadStruct(r);
        d->pushConstants.push_back(std::move(pc));
    }

    for (uint32_t i = 0, n = r.Count(); i < n && r.Ok(); ++i) {
        SpirvVertexInput v;
        v.location = r.Pod<uint32_t>(); v.name = r.Str();
        v.format = static_cast<VkFormat>(r.Pod<int32_t>());
        v.type = ReadType(r);
        d->vertexInputs.push_back(std::move(v));
    }

    auto readIoMap = [&](std::unordered_map<ShaderStage, std::vector<SpirvStageIO>>& map) {
        for (uint32_t i = 0, n = r.Count(); i < n && r.Ok(); ++i) {
            const auto stage = r.Enum<ShaderStage>();
            auto& ios = map[stage];
            for (uint32_t j = 0, m = r.Count(); j < m && r.Ok(); ++j) {
                SpirvStageIO io;
                io.location = r.Pod<uint32_t>(); io.name = r.Str(); io.type = ReadType(r);
                ios.push_back(std::move(io));
            }
        }
    };
    readIoMap(d->stageInputs);
    readIoMap(d->stageOutputs);

    for (uint32_t i = 0, n = r.Count(); i < n && r.Ok(); ++i) {
        SpirvSpecializationConstant sc;
        sc.constantId = r.Pod<uint32_t>(); sc.name = r.Str(); sc.type = ReadType(r);
        sc.defaultValue = r.Pod<uint32_t>();
        d->specializationConstants.push_back(std::move(sc));
    }

    for (uint32_t i = 0, n = r.Count(); i < n && r.Ok(); ++i) d->structDefinitions.push_back(ReadStruct(r));

    d->interfaceHash = r.Str();
    for (uint32_t i = 0, n = r.Count(); i < n && r.Ok(); ++i) {
        const auto stage = r.Enum<ShaderStage>();
        d->entryPoints[stage] = r.Str();
    }

    std::unique_ptr<DescriptorLayoutSpec> layout;
    if (r.Pod<uint8_t>() != 0) {
        layout = std::make_unique<DescriptorLayoutSpec>();
        layout->maxSets = r.Pod<uint32_t>();
        for (uint32_t i = 0, n = r.Count(); i < n && r.Ok(); ++i) {
            DescriptorBindingSpec b;
            b.binding = r.Pod<uint32_t>();
            b.descriptorType = static_cast<VkDescriptorType>(r.Pod<int32_t>());
            b.descriptorCount = r.Pod<uint32_t>(); b.stageFlags = r.Pod<uint32_t>();
            b.name = r.Str();
            layout->bindings.push_back(std::move(b));
        }
    }
    if (!r.Ok() || !r.AtEnd()) return std::nullopt;

    // Rebuild the descriptor-set map so its iteration order equals the recorded order. Inserting
    // in reverse recorded order reproduces it for libstdc++/MSVC singly/doubly linked node lists
    // in the common distinct-bucket case; anything else is verified below and rejected (the
    // caller then reflects from scratch).
    for (auto it = setRecords.rbegin(); it != setRecords.rend(); ++it) {
        d->descriptorSets[it->index] = std::move(it->bindings);
    }
    if (d->descriptorSets.size() != setRecords.size()) return std::nullopt;
    size_t pos = 0;
    for (const auto& kv : d->descriptorSets) {
        if (kv.first != setRecords[pos++].index) return std::nullopt;
    }
    return CachedReflection{std::move(d), std::move(layout)};
}

std::optional<CachedReflection> ShaderReflectionCache::Lookup(const CompiledProgram& program) {
    auto payload = store_.Lookup(MakeKey(program));
    if (!payload) return std::nullopt;
    return Deserialize(*payload);
}

bool ShaderReflectionCache::Store(const CompiledProgram& program, const SpirvReflectionData& reflection,
                                  const DescriptorLayoutSpec* descriptorLayout) {
    return store_.Store(MakeKey(program), Serialize(reflection, descriptorLayout));
}

} // namespace ShaderManagement
