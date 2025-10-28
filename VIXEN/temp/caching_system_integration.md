# Caching System Integration

This document describes a proposed caching system for all "cacheable" Vulkan elements in the VIXEN engine. It describes an architecture that lets RenderGraph nodes request and reuse previously created Vulkan objects such as pipelines, descriptor-set layouts, pipeline layouts, shader modules, render passes, framebuffers, samplers, descriptor pools and other expensive-to-create objects. The design centers on a `MainCacher` (singleton-like orchestrator) and an abstract `CacherBase` for each cache type. The `MainCacher` also exposes serialization to save/load caches to disk so startup/run-time warm-up is fast.

Notes & conventions
- Follow project coding guidelines: C++23, PascalCase types, camelCase methods, smart pointers for ownership, RAII, no exceptions.
- Do not serialize raw VkHandles. Serialize the *creation parameters* and assets (SPIR-V blobs, descriptor schema, flags) needed to recreate Vulkan objects at runtime for the target device.
- Use stable, deterministic keys/hashes for cache entries. Include shader SPIR-V checksums, renderpass attachments/ops, descriptor layout bindings, and relevant device features.

## Goals
- Provide a single access point (`MainCacher`) for nodes to request cache entries.
- Allow adding new cache types via an abstract `CacherBase` interface.
- Support in-memory lifecycle management and optional disk-backed persistence/serialization.
- Provide safe, thread-aware access (lightweight locking) and eviction policies for memory control.

## Cacheable Vulkan element list (initial)
- ShaderModule (SPIR-V blobs + compile options)
- PipelineLayout
- DescriptorSetLayout
- VkPipeline (graphics and compute)
- RenderPass
- Framebuffer
- Sampler
- DescriptorPool (precreated pools for reusing sets)
- Buffer/BufferView (for cached static resources)
- Image/ ImageView (for cached assets)
- (Optional) Memory allocations / memory pools

## Design overview
- `CacherBase`: abstract base type for a cache type. Provides interface for lookup, insert, serialize, deserialize, and prune.
- Per-cache-type class (e.g., `PipelineCacher`, `ShaderModuleCacher`) implements `CacherBase`.
- `MainCacher`: registry of `CacherBase` implementations, exposes typed APIs for nodes, handles serialization across all caches, global config (paths, versioning, max sizes), and simple eviction.
- Cache entries hold metadata (creation parameters, timestamps, size), a stable key, and a shared pointer to the live Vulkan wrapper object.
- Serialization stores creation parameters + binary assets (SPIR-V) and metadata; on load, the `Cacher` rebuilds Vulkan objects using the saved parameters.

## Cache key and hashing strategy
- Keys must be deterministic and include every input that affects Vulkan object creation.
- Example: pipeline key = hash(deviceFeatureMask + renderpassKey + subpassIndex + pipelineLayoutKey + shaderModuleChecksums + rasterization state + blend state + dynamic states ...)
- Use a strong/fixed hash (e.g., xxHash64 or SHA-1). If an external hash lib is undesirable, provide an internal combination function over std::uint64_t.
- Always record the content checksum for SPIR-V to detect shader changes.

## Serialization format
- Use a binary format with a small header for versioning and type identification.
- File layout options:
  - Single monolithic cache file: `caches.bin` with table-of-contents; or
  - Per-cache-type files: `pipeline_cache.bin`, `shader_cache.bin` etc. (recommended: per-type because it isolates changes and is simpler to migrate)
- Each file uses this simple layout:
  - Header: magic bytes (e.g., "VIXENCACH"), format version (uint32), engine commit/tag string length+bytes, device identifier string length+bytes (optional), timestamp
  - Entry index: N entries, for each entry store key (fixed 8/16 bytes), offset, length
  - Blobs: [entryData] repeated. entryData = serialized JSON/Binary structure containing parameters + binary asset(s) (SPIR-V)
- Keep version and migration code paths. Serialization must be resilient to missing/unknown fields.

Security / portability notes
- Cache files are device-dependent (VkDevice/physical device features matter). Either only load caches on the same device/driver or store a device fingerprint and refuse mismatches.
- Do not store raw pointers or handles. Recreate objects from serialized parameters.

## API sketches (header-style)

### CacherBase (abstract)
- File: `include/Cache/cacher_base.h`

```cpp
// PascalCase classes, camelCase methods
class CacherBase {
public:
    virtual ~CacherBase() = default;

    // Return true if an entry exists for key
    virtual bool Has(std::uint64_t key) const noexcept = 0;

    // Get shared pointer to cached object or nullptr
    // The returned type is opaque to the MainCacher; use template helper in derived classes.
    virtual std::shared_ptr<void> Get(std::uint64_t key) = 0;

    // Insert a new entry given key and creation params; returns shared_ptr to cached object
    virtual std::shared_ptr<void> Insert(std::uint64_t key, const std::any& creationParams) = 0;

    // Remove, clear, or prune
    virtual void Erase(std::uint64_t key) = 0;
    virtual void Clear() = 0;

    // Persist in-memory cache to disk at path
    virtual bool SerializeToFile(const std::filesystem::path& path) const = 0;

    // Load cache from disk; recreate live Vulkan objects where possible
    virtual bool DeserializeFromFile(const std::filesystem::path& path, VulkanDevice& device) = 0;

    // Return human readable name for diagnostics
    virtual std::string_view name() const noexcept = 0;
};
```

Notes:
- `std::any` above is an example: prefer explicit param structs for each Cacher (e.g., PipelineCreateParams).
- `std::shared_ptr<void>` is a generic holder; derived caches should provide typed getters via templates.

### PipelineCacher example
- File: `include/Cache/pipeline_cacher.h`

```cpp
struct PipelineCreateParams {
    // store pipeline create info parts needed to recreate
    std::string vertexShaderSpvKey; // reference into shader cache or inline bytes
    std::string fragmentShaderSpvKey;
    RenderPassKey renderPassKey;
    PipelineLayoutKey layoutKey;
    // plus rasterization, input assembly, vertex bindings, attribute descriptions etc.
};

class PipelineCacher : public CacherBase {
public:
    std::shared_ptr<PipelineWrapper> GetPipeline(std::uint64_t key);
    std::shared_ptr<PipelineWrapper> CreateOrGet(const PipelineCreateParams& params);

    // CacherBase overrides
    bool Has(std::uint64_t key) const noexcept override;
    std::shared_ptr<void> Get(std::uint64_t key) override;
    std::shared_ptr<void> Insert(std::uint64_t key, const std::any& creationParams) override;
    void Erase(std::uint64_t key) override;
    void Clear() override;
    bool SerializeToFile(const std::filesystem::path& path) const override;
    bool DeserializeFromFile(const std::filesystem::path& path, VulkanDevice& device) override;
    std::string_view name() const noexcept override { return "PipelineCacher"; }
private:
    // internal map
    std::unordered_map<std::uint64_t, CachedPipelineEntry> m_entries;
    mutable std::shared_mutex m_lock;
};
```

`PipelineWrapper` is an engine wrapper that holds VkPipeline and any helper metadata and performs RAII on destruction.

### MainCacher
- File: `include/Cache/main_cacher.h`

```cpp
class MainCacher {
public:
    static MainCacher& Instance();

    // Register a cacher for a type name; called during engine init
    void RegisterCacher(std::unique_ptr<CacherBase> cacher);

    // Typed access convenience methods
    template<typename T>
    std::shared_ptr<T> GetOrCreate(std::uint64_t key, const typename T::CreateParams& params);

    // Serialization across all registered caches
    bool SaveAll(const std::filesystem::path& dir) const;
    bool LoadAll(const std::filesystem::path& dir, VulkanDevice& device);

    void ClearAll();

private:
    std::vector<std::unique_ptr<CacherBase>> m_cachers;
    std::mutex m_registryLock;
};
```

Usage example (in a Node)

```cpp
// Inside a RenderGraph node when creating pipelines:
PipelineCreateParams pParams = {/* fill from node config and shaders */};
std::uint64_t pipelineKey = MakePipelineKey(pParams);
auto pipeline = MainCacher::Instance().GetOrCreate<PipelineCacher>(pipelineKey, pParams);
// pipeline is a shared_ptr<PipelineWrapper>
```

## Node integration details
- Nodes must compute deterministic keys for the objects they need to reuse. Provide helper functions in the engine for key composition.
- Nodes should not directly own long-lived Vk objects when those objects can be cached. Instead, store a weak reference or `std::shared_ptr` to the cached wrapper.
- On event bus invalidation (e.g., shader reload), the relevant `Cacher` should detect shader checksum mismatch and mark entries invalid; either erase or update entries.

## Migration and invalidation
- Add a version field to serialized files. If version mismatches, either run migration code or ignore old entries.
- Provide a simple invalidation API: `InvalidateByKey(key)`, `InvalidateAll()` and `InvalidateByPredicate(fn)` for conditional invalidations.

## Eviction & memory management
- Provide a max-memory or max-entries policy per cacher.
- Track approximate memory usage per entry.
- Use LRU or TTL eviction strategies.
- Allow explicit hints from nodes for pinning important objects (e.g., currently bound pipeline) to avoid eviction.

## Thread-safety
- `MainCacher` should use a mutex for registrar operations and allow per-cacher shared_mutex for fast concurrent reads and guarded writes.

## Example: pipeline key helper (concept)

```cpp
std::uint64_t MakePipelineKey(const PipelineCreateParams& p) {
    // Example simple combiner; replace with stable hash (xxhash) in implementation
    std::uint64_t h = 1469598103934665603ull; // FNV offset
    // combine fields deterministically
    h = HashCombine(h, std::hash<std::string_view>{}(p.vertexShaderSpvKey));
    h = HashCombine(h, std::hash<std::string_view>{}(p.fragmentShaderSpvKey));
    h = HashCombine(h, p.renderPassKey.value);
    // combine pipeline states similarly
    return h;
}
```

## Serialization pseudo-workflow for a pipeline entry
1. Collect creation parameters and any binary assets (SPIR-V) used by the pipeline.
2. Pack them into an `EntryBlob` struct (binary):
   - uint64_t key
   - JSON/Binary param block (serialized)
   - SPIR-V blobs concatenated with offsets
   - metadata: size, timestamp, version
3. Append the entry to the per-type file and update the index.
4. On load, read the index and for each entry run the per-cacher `DeserializeEntry` which reconstructs required ShaderModules from the stored SPIR-V, then create PipelineLayout and VkPipeline with saved parameters.

## Edge cases and gotchas
- Device/driver changes: caches may be invalid if the underlying Vulkan device differs in features/driver behavior.
- Shader recompilation: if runtime shader recompilation occurs, invalidation must propagate to pipelines referencing that shader.
- Binary compatibility: caches stored on one version of the engine or format may not be loadable in a newer/older engine; handle versioning.
- Performance: disk I/O at load can be heavy—consider streaming, background loading, or per-asset lazy loading.

## Diagnostics & tooling
- Provide `--dump-caches` developer option that prints cache sizes, entries, last-used timestamps, memory use.
- Provide a small utility to inspect cache files and list keys and metadata.

## Implementation roadmap (next steps)
1. Add headers and scaffolding under `include/Cache/` and `source/Cache/` (low-risk): `cacher_base.h`, `main_cacher.h`, `pipeline_cacher.h`, `shader_module_cacher.h`.
2. Implement `ShaderModuleCacher` first (it provides SPIR-V blobs and checksums used by other caches).
3. Implement `PipelineCacher` (reads shader module cache, recreates pipeline).
4. Add serialization helpers and tests for write/read round-trip.
5. Integrate into RenderGraph nodes: replace direct pipeline creation with `MainCacher` lookups.
6. Add unit tests for key generation, serialization integrity, and invalidation.

## Minimal tests to add
- Create shader module, serialize & deserialize, verify checksum and recreate success.
- Create pipeline with known params, serialize & deserialize, recreate and bind.
- Invalidate shader: ensure dependent pipeline entries are invalidated.

## Example file names & placement
- `include/Cache/cacher_base.h` — abstract base class and utility types
- `include/Cache/main_cacher.h` — orchestrator
- `include/Cache/pipeline_cacher.h` — pipeline cache
- `include/Cache/shader_module_cacher.h` — shader cache
- `source/Cache/*.cpp` — implementations
- `tests/cache_tests/*` — unit tests

## Final notes
- Keep serialization robust and conservative: prefer explicit fields and version tags rather than tightly packed, undocumented binary structures.
- Prefer per-type cache files to simplify migrations and partial loads.
- Use device fingerprinting to avoid silently loading incompatible caches.

If you'd like, I can also:
- Create the header skeletons under `include/Cache/` and small unit test stubs under `tests/`.
- Implement the `ShaderModuleCacher` minimal implementation and a `MainCacher` registry to prove the architecture.


---
Created: temp/caching_system_integration.md
