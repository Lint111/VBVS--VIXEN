// VK_LAYER_VIXEN_gpu_ledger (sn-gpumem1): what is resident on the GPU and what is sent to it every frame.
//
// A Vulkan layer, benchmark tooling only (built with UNDERSET_NATIVE_BENCHMARK on Windows; never part of a normal build). It sees every
// allocation the process makes, whichever code path made it (the engine's own vkAllocateMemory sites, VMA, RmlUi, FSR), so the ledger is
// complete without editing the 35 allocation sites. Output is CSV into VIXEN_GPU_LEDGER_DIR; with the variable unset the layer only passes
// calls through.
//
// Enable:  VIXEN_GPU_LEDGER=1 VIXEN_GPU_LEDGER_DIR=<dir> VK_LAYER_PATH=<dir of the json> (the json's enable_environment loads it).
//
// Attribution comes from the engine, benchmark builds only (FrameTrace.h): it pushes the node's instance name around Setup/Compile/Execute
// (VixenLedgerPushScope), tells the layer the frame number (VixenLedgerSetFrame) and which mapped ring slot a node asked to write
// (VixenLedgerTouch). Those four exports are resolved with GetProcAddress, so an engine without the layer loaded pays nothing.
//
// Files (per run):
//   ledger-memory.csv     one row per vkAllocateMemory: id, scope, frames alive, bytes, memory type flags, mapped
//   ledger-resources.csv  one row per buffer/image: scope, usage / format / extent, required bytes, the allocation it is bound in
//   ledger-frames.csv     one row per frame: live bytes (device-local / host-visible / mapped) and the frame's upload totals
//   ledger-upload.csv     one row per (frame, scope, kind) with the bytes and calls
//   ledger-mapped.csv     one row per (frame, mapped allocation) that the engine touched or that changed: touched (slot capacity), written
//                         (copied in, where the site reports it) vs bytes that
//                         actually differ from the same slot's previous snapshot, and the 64-byte rows that differ (a dirty-row delta's size)
#if defined(_WIN32) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS 1   // getenv/fopen: tooling, not shipped code
#endif
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#define LEDGER_EXPORT extern "C" __declspec(dllexport)
#else
#define LEDGER_EXPORT extern "C" __attribute__((visibility("default")))
#endif

#if defined(_WIN32)
// vk_layer.h declares this entry point without dllexport; export it by name so the declaration and the definition agree.
#pragma comment(linker, "/EXPORT:vkNegotiateLoaderLayerInterfaceVersion")
#endif

namespace {

constexpr uint64_t kRowBytes = 64;                    // the diff granularity: one cache line
constexpr uint64_t kMaxShadowAllocation = 256ull << 20;  // a mapped allocation larger than this is not diffed (recorded as skipped)
constexpr uint64_t kMaxShadowTotal = 1024ull << 20;

struct InstanceData {
    PFN_vkGetInstanceProcAddr getInstanceProcAddr = nullptr;
    PFN_vkDestroyInstance destroyInstance = nullptr;
    PFN_vkCreateDevice createDevice = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties getMemoryProperties = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
};

struct DeviceData {
    PFN_vkGetDeviceProcAddr getDeviceProcAddr = nullptr;
    PFN_vkDestroyDevice destroyDevice = nullptr;
    PFN_vkAllocateMemory allocateMemory = nullptr;
    PFN_vkFreeMemory freeMemory = nullptr;
    PFN_vkCreateBuffer createBuffer = nullptr;
    PFN_vkDestroyBuffer destroyBuffer = nullptr;
    PFN_vkCreateImage createImage = nullptr;
    PFN_vkDestroyImage destroyImage = nullptr;
    PFN_vkBindBufferMemory bindBufferMemory = nullptr;
    PFN_vkBindImageMemory bindImageMemory = nullptr;
    PFN_vkBindBufferMemory2 bindBufferMemory2 = nullptr;
    PFN_vkBindImageMemory2 bindImageMemory2 = nullptr;
    PFN_vkMapMemory mapMemory = nullptr;
    PFN_vkUnmapMemory unmapMemory = nullptr;
    PFN_vkGetBufferMemoryRequirements getBufferRequirements = nullptr;
    PFN_vkGetImageMemoryRequirements getImageRequirements = nullptr;
    PFN_vkCmdCopyBuffer cmdCopyBuffer = nullptr;
    PFN_vkCmdCopyBuffer2 cmdCopyBuffer2 = nullptr;
    PFN_vkCmdCopyBufferToImage cmdCopyBufferToImage = nullptr;
    PFN_vkCmdCopyBufferToImage2 cmdCopyBufferToImage2 = nullptr;
    PFN_vkCmdUpdateBuffer cmdUpdateBuffer = nullptr;
    PFN_vkCmdPushConstants cmdPushConstants = nullptr;
    PFN_vkUpdateDescriptorSets updateDescriptorSets = nullptr;
    VkPhysicalDeviceMemoryProperties memory{};
    VkDevice device = VK_NULL_HANDLE;
};

struct Memory {
    uint64_t id = 0;
    std::string scope;
    uint64_t size = 0;
    uint32_t typeIndex = 0;
    uint32_t flags = 0;       // VkMemoryPropertyFlags of the type
    uint32_t heap = 0;
    uint64_t allocFrame = 0;
    int64_t freeFrame = -1;
    uint8_t* mapped = nullptr;  // current mapping base (offset applied), null when not mapped
    uint64_t mappedSize = 0;
    std::vector<uint8_t> shadow;  // the previous frame-end content of the mapped range
    bool shadowSkipped = false;
    uint64_t touchedThisFrame = 0;   // capacity of the slot a node asked the mapped pointer of
    uint64_t writtenThisFrame = 0;   // bytes a node reported it copied in (only the sites that report it)
};

struct Resource {
    bool image = false;
    std::string scope;
    uint64_t usage = 0;
    uint32_t format = 0;
    uint32_t w = 0, h = 0, d = 0, mips = 0, layers = 0, samples = 1, type = 0;
    uint64_t reqSize = 0;
    uint64_t memory = 0;        // VkDeviceMemory handle it is bound to
    uint64_t memoryOffset = 0;
    uint64_t allocFrame = 0;
    int64_t freeFrame = -1;
};

struct UploadKey {
    std::string scope;
    const char* kind;
    bool operator<(const UploadKey& o) const { return scope != o.scope ? scope < o.scope : std::strcmp(kind, o.kind) < 0; }
};
struct UploadValue { uint64_t bytes = 0; uint64_t calls = 0; };

std::mutex gMutex;
std::map<void*, InstanceData> gInstances;
std::map<void*, DeviceData> gDevices;
std::unordered_map<uint64_t, Memory> gMemory;
std::vector<Memory> gDeadMemory;
std::unordered_map<uint64_t, Resource> gResources;   // buffers and images share the handle space only per type: key = handle ^ (image ? 1<<63 : 0)
std::vector<Resource> gDeadResources;
std::map<UploadKey, UploadValue> gUpload;
uint64_t gNextMemoryId = 1;
uint64_t gFrame = 0;
bool gHaveFrame = false;
std::string gDir;
FILE* gFramesFile = nullptr;
FILE* gUploadFile = nullptr;
FILE* gMappedFile = nullptr;
uint64_t gShadowTotal = 0;
bool gEnabled = false;
bool gFlushed = false;

thread_local std::vector<std::string> tScope;

std::string currentScope() { return tScope.empty() ? std::string("(none)") : tScope.back(); }

std::string clean(const std::string& s) {
    std::string out = s;
    for (char& c : out) if (c == ',' || c == '\n' || c == '\r') c = '_';
    return out;
}

void* dispatchKey(const void* handle) { return *reinterpret_cast<void* const*>(handle); }

DeviceData deviceOf(const void* handle) {
    std::lock_guard<std::mutex> lock(gMutex);
    auto it = gDevices.find(dispatchKey(handle));
    return it == gDevices.end() ? DeviceData{} : it->second;
}

uint64_t resourceKey(uint64_t handle, bool image) { return image ? (handle ^ (1ull << 63)) : handle; }

// Bytes of one texel (or block-compressed texel) for the formats the engine uses; unknown formats count 4 and are flagged by the analysis.
double formatBytes(uint32_t f) {
    switch (f) {
        case VK_FORMAT_R8_UNORM: case VK_FORMAT_R8_UINT: case VK_FORMAT_R8_SNORM: case VK_FORMAT_S8_UINT: return 1;
        case VK_FORMAT_R8G8_UNORM: case VK_FORMAT_R16_UINT: case VK_FORMAT_R16_SFLOAT: case VK_FORMAT_R16_UNORM: case VK_FORMAT_D16_UNORM: return 2;
        case VK_FORMAT_R8G8B8_UNORM: return 3;
        case VK_FORMAT_R32_SFLOAT: case VK_FORMAT_R32_UINT: case VK_FORMAT_R32_SINT: case VK_FORMAT_R16G16_SFLOAT: case VK_FORMAT_R16G16_UINT:
        case VK_FORMAT_R8G8B8A8_UNORM: case VK_FORMAT_R8G8B8A8_SRGB: case VK_FORMAT_B8G8R8A8_UNORM: case VK_FORMAT_B8G8R8A8_SRGB:
        case VK_FORMAT_R8G8B8A8_UINT: case VK_FORMAT_A2B10G10R10_UNORM_PACK32: case VK_FORMAT_B10G11R11_UFLOAT_PACK32: case VK_FORMAT_D32_SFLOAT:
        case VK_FORMAT_D24_UNORM_S8_UINT: case VK_FORMAT_X8_D24_UNORM_PACK32: case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32: return 4;
        case VK_FORMAT_R16G16B16A16_SFLOAT: case VK_FORMAT_R16G16B16A16_UNORM: case VK_FORMAT_R16G16B16A16_UINT: case VK_FORMAT_R32G32_SFLOAT:
        case VK_FORMAT_R32G32_UINT: case VK_FORMAT_D32_SFLOAT_S8_UINT: return 8;
        case VK_FORMAT_R32G32B32_SFLOAT: return 12;
        case VK_FORMAT_R32G32B32A32_SFLOAT: case VK_FORMAT_R32G32B32A32_UINT: case VK_FORMAT_R32G32B32A32_SINT: return 16;
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK: case VK_FORMAT_BC1_RGBA_UNORM_BLOCK: case VK_FORMAT_BC4_UNORM_BLOCK: return 0.5;
        case VK_FORMAT_BC3_UNORM_BLOCK: case VK_FORMAT_BC5_UNORM_BLOCK: case VK_FORMAT_BC7_UNORM_BLOCK: return 1;
        default: return 4;
    }
}

void addUpload(const std::string& scope, const char* kind, uint64_t bytes, uint64_t calls = 1) {
    if (!gEnabled) return;
    std::lock_guard<std::mutex> lock(gMutex);
    UploadValue& v = gUpload[UploadKey{scope, kind}];
    v.bytes += bytes;
    v.calls += calls;
}

bool memoryIsHostVisible(uint64_t memory) {
    auto it = gMemory.find(memory);
    return it != gMemory.end() && (it->second.flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
}

bool bufferIsHostVisible(VkBuffer buffer) {
    auto it = gResources.find(resourceKey(reinterpret_cast<uint64_t>(buffer), false));
    return it != gResources.end() && it->second.memory != 0 && memoryIsHostVisible(it->second.memory);
}

// ---- frame end: the mapped diff and the frame row -------------------------------------------------------------------------------

void diffMappedLocked(Memory& m, uint64_t frame) {
    if (!m.mapped || m.mappedSize == 0) return;
    if (m.shadowSkipped) return;
    const bool first = m.shadow.empty();
    if (first) {
        if (m.mappedSize > kMaxShadowAllocation || gShadowTotal + m.mappedSize > kMaxShadowTotal) { m.shadowSkipped = true; return; }
        m.shadow.assign(m.mapped, m.mapped + m.mappedSize);
        gShadowTotal += m.mappedSize;
        if (m.touchedThisFrame == 0 && m.writtenThisFrame == 0) return;   // the baseline itself is not a change
    }
    uint64_t changedBytes = 0, dirtyRows = 0;
    if (!first) {
        for (uint64_t off = 0; off < m.mappedSize; off += kRowBytes) {
            const uint64_t n = std::min<uint64_t>(kRowBytes, m.mappedSize - off);
            if (std::memcmp(m.shadow.data() + off, m.mapped + off, n) == 0) continue;
            ++dirtyRows;
            for (uint64_t i = 0; i < n; ++i) changedBytes += (m.shadow[off + i] != m.mapped[off + i]) ? 1 : 0;
            std::memcpy(m.shadow.data() + off, m.mapped + off, n);
        }
    }
    if (gMappedFile && (m.touchedThisFrame > 0 || m.writtenThisFrame > 0 || changedBytes > 0)) {
        std::fprintf(gMappedFile, "%llu,%llu,%s,%llu,%llu,%llu,%llu,%llu,%llu\n", static_cast<unsigned long long>(frame), static_cast<unsigned long long>(m.id),
                     clean(m.scope).c_str(), static_cast<unsigned long long>(m.mappedSize), static_cast<unsigned long long>(m.touchedThisFrame),
                     static_cast<unsigned long long>(changedBytes), static_cast<unsigned long long>(dirtyRows), first ? 1ull : 0ull,
                     static_cast<unsigned long long>(m.writtenThisFrame));
    }
    m.touchedThisFrame = 0;
    m.writtenThisFrame = 0;
}

void openFilesLocked() {
    if (gFramesFile || gDir.empty()) return;
    auto open = [&](const char* name, const char* header) {
        FILE* f = std::fopen((gDir + "/" + name).c_str(), "w");
        if (f) std::fputs(header, f);
        return f;
    };
    gFramesFile = open("ledger-frames.csv", "frame,live_allocs,live_device_local_bytes,live_host_visible_bytes,live_mapped_bytes,live_resource_bytes\n");
    gUploadFile = open("ledger-upload.csv", "frame,scope,kind,bytes,calls\n");
    gMappedFile = open("ledger-mapped.csv", "frame,alloc_id,scope,alloc_bytes,touched_bytes,changed_bytes,dirty_rows64,baseline,written_bytes\n");
}

void finishFrameLocked(uint64_t frame) {
    openFilesLocked();
    uint64_t devLocal = 0, hostVisible = 0, mappedBytes = 0, resBytes = 0, live = 0;
    for (auto& [id, m] : gMemory) {
        ++live;
        if (m.flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) devLocal += m.size;
        if (m.flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) hostVisible += m.size;
        if (m.mapped) mappedBytes += m.size;
        diffMappedLocked(m, frame);
    }
    for (auto& [h, r] : gResources) resBytes += r.reqSize;
    if (gFramesFile) std::fprintf(gFramesFile, "%llu,%llu,%llu,%llu,%llu,%llu\n", static_cast<unsigned long long>(frame), static_cast<unsigned long long>(live),
                                  static_cast<unsigned long long>(devLocal), static_cast<unsigned long long>(hostVisible), static_cast<unsigned long long>(mappedBytes),
                                  static_cast<unsigned long long>(resBytes));
    if (gUploadFile) {
        for (auto& [k, v] : gUpload) {
            std::fprintf(gUploadFile, "%llu,%s,%s,%llu,%llu\n", static_cast<unsigned long long>(frame), clean(k.scope).c_str(), k.kind,
                         static_cast<unsigned long long>(v.bytes), static_cast<unsigned long long>(v.calls));
        }
    }
    gUpload.clear();
}

void writeResidentFilesLocked() {
    if (gDir.empty()) return;
    if (FILE* f = std::fopen((gDir + "/ledger-memory.csv").c_str(), "w")) {
        std::fputs("id,scope,alloc_frame,free_frame,bytes,type_index,heap,flags,host_visible,device_local,mapped_at_end\n", f);
        auto row = [&](const Memory& m) {
            std::fprintf(f, "%llu,%s,%llu,%lld,%llu,%u,%u,%u,%d,%d,%d\n", static_cast<unsigned long long>(m.id), clean(m.scope).c_str(),
                         static_cast<unsigned long long>(m.allocFrame), static_cast<long long>(m.freeFrame), static_cast<unsigned long long>(m.size), m.typeIndex,
                         m.heap, m.flags, (m.flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) ? 1 : 0, (m.flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ? 1 : 0, m.mapped ? 1 : 0);
        };
        for (const Memory& m : gDeadMemory) row(m);
        for (const auto& [id, m] : gMemory) row(m);
        std::fclose(f);
    }
    if (FILE* f = std::fopen((gDir + "/ledger-resources.csv").c_str(), "w")) {
        std::fputs("kind,scope,usage,format,width,height,depth,mips,layers,samples,image_type,req_bytes,memory_handle,offset,alloc_frame,free_frame\n", f);
        auto row = [&](const Resource& r) {
            std::fprintf(f, "%s,%s,%llu,%u,%u,%u,%u,%u,%u,%u,%u,%llu,%llu,%llu,%llu,%lld\n", r.image ? "image" : "buffer", clean(r.scope).c_str(),
                         static_cast<unsigned long long>(r.usage), r.format, r.w, r.h, r.d, r.mips, r.layers, r.samples, r.type, static_cast<unsigned long long>(r.reqSize),
                         static_cast<unsigned long long>(r.memory), static_cast<unsigned long long>(r.memoryOffset), static_cast<unsigned long long>(r.allocFrame),
                         static_cast<long long>(r.freeFrame));
        };
        for (const Resource& r : gDeadResources) row(r);
        for (const auto& [h, r] : gResources) row(r);
        std::fclose(f);
    }
}

void flushAllLocked() {
    if (gFlushed || !gEnabled) return;
    gFlushed = true;
    if (gHaveFrame) finishFrameLocked(gFrame);
    writeResidentFilesLocked();
    for (FILE* f : {gFramesFile, gUploadFile, gMappedFile}) if (f) std::fclose(f);
    gFramesFile = gUploadFile = gMappedFile = nullptr;
}

// ---- loader plumbing --------------------------------------------------------------------------------------------------------------

VkLayerInstanceCreateInfo* findInstanceLink(const VkInstanceCreateInfo* info) {
    auto* p = reinterpret_cast<const VkLayerInstanceCreateInfo*>(info->pNext);
    while (p && !(p->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO && p->function == VK_LAYER_LINK_INFO)) p = reinterpret_cast<const VkLayerInstanceCreateInfo*>(p->pNext);
    return const_cast<VkLayerInstanceCreateInfo*>(p);
}

VkLayerDeviceCreateInfo* findDeviceLink(const VkDeviceCreateInfo* info) {
    auto* p = reinterpret_cast<const VkLayerDeviceCreateInfo*>(info->pNext);
    while (p && !(p->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO && p->function == VK_LAYER_LINK_INFO)) p = reinterpret_cast<const VkLayerDeviceCreateInfo*>(p->pNext);
    return const_cast<VkLayerDeviceCreateInfo*>(p);
}

VKAPI_ATTR VkResult VKAPI_CALL layerCreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance) {
    VkLayerInstanceCreateInfo* link = findInstanceLink(pCreateInfo);
    if (!link) return VK_ERROR_INITIALIZATION_FAILED;
    PFN_vkGetInstanceProcAddr next = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    link->u.pLayerInfo = link->u.pLayerInfo->pNext;
    auto create = reinterpret_cast<PFN_vkCreateInstance>(next(VK_NULL_HANDLE, "vkCreateInstance"));
    if (!create) return VK_ERROR_INITIALIZATION_FAILED;
    const VkResult result = create(pCreateInfo, pAllocator, pInstance);
    if (result != VK_SUCCESS) return result;
    InstanceData data;
    data.getInstanceProcAddr = next;
    data.instance = *pInstance;
    data.destroyInstance = reinterpret_cast<PFN_vkDestroyInstance>(next(*pInstance, "vkDestroyInstance"));
    data.createDevice = reinterpret_cast<PFN_vkCreateDevice>(next(*pInstance, "vkCreateDevice"));
    data.getMemoryProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(next(*pInstance, "vkGetPhysicalDeviceMemoryProperties"));
    std::lock_guard<std::mutex> lock(gMutex);
    gInstances[dispatchKey(*pInstance)] = data;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL layerDestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator) {
    PFN_vkDestroyInstance next = nullptr;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        auto it = gInstances.find(dispatchKey(instance));
        if (it != gInstances.end()) { next = it->second.destroyInstance; gInstances.erase(it); }
    }
    if (next) next(instance, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL layerCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) {
    VkLayerDeviceCreateInfo* link = findDeviceLink(pCreateInfo);
    if (!link) return VK_ERROR_INITIALIZATION_FAILED;
    PFN_vkGetInstanceProcAddr nextGipa = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr nextGdpa = link->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    link->u.pLayerInfo = link->u.pLayerInfo->pNext;
    InstanceData inst{};
    {
        std::lock_guard<std::mutex> lock(gMutex);
        auto it = gInstances.find(dispatchKey(physicalDevice));
        if (it != gInstances.end()) inst = it->second;
    }
    auto create = reinterpret_cast<PFN_vkCreateDevice>(nextGipa(inst.instance, "vkCreateDevice"));
    if (!create) return VK_ERROR_INITIALIZATION_FAILED;
    const VkResult result = create(physicalDevice, pCreateInfo, pAllocator, pDevice);
    if (result != VK_SUCCESS) return result;
    DeviceData d;
    d.device = *pDevice;
    d.getDeviceProcAddr = nextGdpa;
#define LOAD(field, type, name) d.field = reinterpret_cast<type>(nextGdpa(*pDevice, name))
    LOAD(destroyDevice, PFN_vkDestroyDevice, "vkDestroyDevice");
    LOAD(allocateMemory, PFN_vkAllocateMemory, "vkAllocateMemory");
    LOAD(freeMemory, PFN_vkFreeMemory, "vkFreeMemory");
    LOAD(createBuffer, PFN_vkCreateBuffer, "vkCreateBuffer");
    LOAD(destroyBuffer, PFN_vkDestroyBuffer, "vkDestroyBuffer");
    LOAD(createImage, PFN_vkCreateImage, "vkCreateImage");
    LOAD(destroyImage, PFN_vkDestroyImage, "vkDestroyImage");
    LOAD(bindBufferMemory, PFN_vkBindBufferMemory, "vkBindBufferMemory");
    LOAD(bindImageMemory, PFN_vkBindImageMemory, "vkBindImageMemory");
    LOAD(bindBufferMemory2, PFN_vkBindBufferMemory2, "vkBindBufferMemory2");
    LOAD(bindImageMemory2, PFN_vkBindImageMemory2, "vkBindImageMemory2");
    LOAD(mapMemory, PFN_vkMapMemory, "vkMapMemory");
    LOAD(unmapMemory, PFN_vkUnmapMemory, "vkUnmapMemory");
    LOAD(getBufferRequirements, PFN_vkGetBufferMemoryRequirements, "vkGetBufferMemoryRequirements");
    LOAD(getImageRequirements, PFN_vkGetImageMemoryRequirements, "vkGetImageMemoryRequirements");
    LOAD(cmdCopyBuffer, PFN_vkCmdCopyBuffer, "vkCmdCopyBuffer");
    LOAD(cmdCopyBuffer2, PFN_vkCmdCopyBuffer2, "vkCmdCopyBuffer2");
    LOAD(cmdCopyBufferToImage, PFN_vkCmdCopyBufferToImage, "vkCmdCopyBufferToImage");
    LOAD(cmdCopyBufferToImage2, PFN_vkCmdCopyBufferToImage2, "vkCmdCopyBufferToImage2");
    LOAD(cmdUpdateBuffer, PFN_vkCmdUpdateBuffer, "vkCmdUpdateBuffer");
    LOAD(cmdPushConstants, PFN_vkCmdPushConstants, "vkCmdPushConstants");
    LOAD(updateDescriptorSets, PFN_vkUpdateDescriptorSets, "vkUpdateDescriptorSets");
#undef LOAD
    // A device that exposes the promoted calls only through their extension names.
    if (!d.bindBufferMemory2) d.bindBufferMemory2 = reinterpret_cast<PFN_vkBindBufferMemory2>(nextGdpa(*pDevice, "vkBindBufferMemory2KHR"));
    if (!d.bindImageMemory2) d.bindImageMemory2 = reinterpret_cast<PFN_vkBindImageMemory2>(nextGdpa(*pDevice, "vkBindImageMemory2KHR"));
    if (!d.cmdCopyBuffer2) d.cmdCopyBuffer2 = reinterpret_cast<PFN_vkCmdCopyBuffer2>(nextGdpa(*pDevice, "vkCmdCopyBuffer2KHR"));
    if (!d.cmdCopyBufferToImage2) d.cmdCopyBufferToImage2 = reinterpret_cast<PFN_vkCmdCopyBufferToImage2>(nextGdpa(*pDevice, "vkCmdCopyBufferToImage2KHR"));
    if (inst.getMemoryProperties) inst.getMemoryProperties(physicalDevice, &d.memory);
    std::lock_guard<std::mutex> lock(gMutex);
    gDevices[dispatchKey(*pDevice)] = d;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL layerDestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator) {
    PFN_vkDestroyDevice next = nullptr;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        flushAllLocked();
        auto it = gDevices.find(dispatchKey(device));
        if (it != gDevices.end()) { next = it->second.destroyDevice; gDevices.erase(it); }
    }
    if (next) next(device, pAllocator);
}

// ---- allocation and resource tracking ---------------------------------------------------------------------------------------------

VKAPI_ATTR VkResult VKAPI_CALL layerAllocateMemory(VkDevice device, const VkMemoryAllocateInfo* pInfo, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMemory) {
    const DeviceData d = deviceOf(device);
    const VkResult r = d.allocateMemory(device, pInfo, pAllocator, pMemory);
    if (r != VK_SUCCESS || !gEnabled) return r;
    Memory m;
    m.scope = currentScope();
    m.size = pInfo->allocationSize;
    m.typeIndex = pInfo->memoryTypeIndex;
    if (pInfo->memoryTypeIndex < d.memory.memoryTypeCount) {
        m.flags = d.memory.memoryTypes[pInfo->memoryTypeIndex].propertyFlags;
        m.heap = d.memory.memoryTypes[pInfo->memoryTypeIndex].heapIndex;
    }
    std::lock_guard<std::mutex> lock(gMutex);
    m.id = gNextMemoryId++;
    m.allocFrame = gFrame;
    gMemory[reinterpret_cast<uint64_t>(*pMemory)] = std::move(m);
    return r;
}

VKAPI_ATTR void VKAPI_CALL layerFreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks* pAllocator) {
    const DeviceData d = deviceOf(device);
    if (gEnabled && memory != VK_NULL_HANDLE) {
        std::lock_guard<std::mutex> lock(gMutex);
        auto it = gMemory.find(reinterpret_cast<uint64_t>(memory));
        if (it != gMemory.end()) {
            Memory m = std::move(it->second);
            gMemory.erase(it);
            m.freeFrame = static_cast<int64_t>(gFrame);
            gShadowTotal -= std::min<uint64_t>(gShadowTotal, m.shadow.size());
            m.shadow.clear();
            m.shadow.shrink_to_fit();
            m.mapped = nullptr;
            gDeadMemory.push_back(std::move(m));
        }
    }
    d.freeMemory(device, memory, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL layerCreateBuffer(VkDevice device, const VkBufferCreateInfo* pInfo, const VkAllocationCallbacks* pAllocator, VkBuffer* pBuffer) {
    const DeviceData d = deviceOf(device);
    const VkResult r = d.createBuffer(device, pInfo, pAllocator, pBuffer);
    if (r != VK_SUCCESS || !gEnabled) return r;
    Resource res;
    res.scope = currentScope();
    res.usage = pInfo->usage;
    res.reqSize = pInfo->size;
    VkMemoryRequirements req{};
    if (d.getBufferRequirements) { d.getBufferRequirements(device, *pBuffer, &req); res.reqSize = req.size; }
    std::lock_guard<std::mutex> lock(gMutex);
    res.allocFrame = gFrame;
    gResources[resourceKey(reinterpret_cast<uint64_t>(*pBuffer), false)] = std::move(res);
    return r;
}

VKAPI_ATTR VkResult VKAPI_CALL layerCreateImage(VkDevice device, const VkImageCreateInfo* pInfo, const VkAllocationCallbacks* pAllocator, VkImage* pImage) {
    const DeviceData d = deviceOf(device);
    const VkResult r = d.createImage(device, pInfo, pAllocator, pImage);
    if (r != VK_SUCCESS || !gEnabled) return r;
    Resource res;
    res.image = true;
    res.scope = currentScope();
    res.usage = pInfo->usage;
    res.format = pInfo->format;
    res.w = pInfo->extent.width; res.h = pInfo->extent.height; res.d = pInfo->extent.depth;
    res.mips = pInfo->mipLevels; res.layers = pInfo->arrayLayers; res.samples = pInfo->samples; res.type = pInfo->imageType;
    VkMemoryRequirements req{};
    if (d.getImageRequirements) { d.getImageRequirements(device, *pImage, &req); res.reqSize = req.size; }
    std::lock_guard<std::mutex> lock(gMutex);
    res.allocFrame = gFrame;
    gResources[resourceKey(reinterpret_cast<uint64_t>(*pImage), true)] = std::move(res);
    return r;
}

void retireResource(uint64_t handle, bool image) {
    if (!gEnabled) return;
    std::lock_guard<std::mutex> lock(gMutex);
    auto it = gResources.find(resourceKey(handle, image));
    if (it == gResources.end()) return;
    Resource r = std::move(it->second);
    gResources.erase(it);
    r.freeFrame = static_cast<int64_t>(gFrame);
    gDeadResources.push_back(std::move(r));
}

VKAPI_ATTR void VKAPI_CALL layerDestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks* pAllocator) {
    const DeviceData d = deviceOf(device);
    retireResource(reinterpret_cast<uint64_t>(buffer), false);
    d.destroyBuffer(device, buffer, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL layerDestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks* pAllocator) {
    const DeviceData d = deviceOf(device);
    retireResource(reinterpret_cast<uint64_t>(image), true);
    d.destroyImage(device, image, pAllocator);
}

void bindNote(uint64_t handle, bool image, VkDeviceMemory memory, VkDeviceSize offset) {
    if (!gEnabled) return;
    std::lock_guard<std::mutex> lock(gMutex);
    auto it = gResources.find(resourceKey(handle, image));
    if (it == gResources.end()) return;
    it->second.memory = reinterpret_cast<uint64_t>(memory);
    it->second.memoryOffset = offset;
}

VKAPI_ATTR VkResult VKAPI_CALL layerBindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize offset) {
    const VkResult r = deviceOf(device).bindBufferMemory(device, buffer, memory, offset);
    if (r == VK_SUCCESS) bindNote(reinterpret_cast<uint64_t>(buffer), false, memory, offset);
    return r;
}

VKAPI_ATTR VkResult VKAPI_CALL layerBindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize offset) {
    const VkResult r = deviceOf(device).bindImageMemory(device, image, memory, offset);
    if (r == VK_SUCCESS) bindNote(reinterpret_cast<uint64_t>(image), true, memory, offset);
    return r;
}

VKAPI_ATTR VkResult VKAPI_CALL layerBindBufferMemory2(VkDevice device, uint32_t count, const VkBindBufferMemoryInfo* pInfos) {
    const VkResult r = deviceOf(device).bindBufferMemory2(device, count, pInfos);
    if (r == VK_SUCCESS) for (uint32_t i = 0; i < count; ++i) bindNote(reinterpret_cast<uint64_t>(pInfos[i].buffer), false, pInfos[i].memory, pInfos[i].memoryOffset);
    return r;
}

VKAPI_ATTR VkResult VKAPI_CALL layerBindImageMemory2(VkDevice device, uint32_t count, const VkBindImageMemoryInfo* pInfos) {
    const VkResult r = deviceOf(device).bindImageMemory2(device, count, pInfos);
    if (r == VK_SUCCESS) for (uint32_t i = 0; i < count; ++i) bindNote(reinterpret_cast<uint64_t>(pInfos[i].image), true, pInfos[i].memory, pInfos[i].memoryOffset);
    return r;
}

VKAPI_ATTR VkResult VKAPI_CALL layerMapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void** ppData) {
    const VkResult r = deviceOf(device).mapMemory(device, memory, offset, size, flags, ppData);
    if (r != VK_SUCCESS || !gEnabled) return r;
    std::lock_guard<std::mutex> lock(gMutex);
    auto it = gMemory.find(reinterpret_cast<uint64_t>(memory));
    if (it != gMemory.end()) {
        it->second.mapped = static_cast<uint8_t*>(*ppData);
        it->second.mappedSize = (size == VK_WHOLE_SIZE) ? it->second.size - offset : size;
    }
    return r;
}

VKAPI_ATTR void VKAPI_CALL layerUnmapMemory(VkDevice device, VkDeviceMemory memory) {
    if (gEnabled) {
        std::lock_guard<std::mutex> lock(gMutex);
        auto it = gMemory.find(reinterpret_cast<uint64_t>(memory));
        if (it != gMemory.end()) { it->second.mapped = nullptr; it->second.mappedSize = 0; }
    }
    deviceOf(device).unmapMemory(device, memory);
}

// ---- per-frame uploads ------------------------------------------------------------------------------------------------------------

VKAPI_ATTR void VKAPI_CALL layerCmdCopyBuffer(VkCommandBuffer cb, VkBuffer src, VkBuffer dst, uint32_t count, const VkBufferCopy* regions) {
    if (gEnabled) {
        uint64_t bytes = 0;
        for (uint32_t i = 0; i < count; ++i) bytes += regions[i].size;
        bool host;
        { std::lock_guard<std::mutex> lock(gMutex); host = bufferIsHostVisible(src); }
        addUpload(currentScope(), host ? "copy_buffer_from_host" : "copy_buffer_gpu_only", bytes);
    }
    deviceOf(cb).cmdCopyBuffer(cb, src, dst, count, regions);
}

VKAPI_ATTR void VKAPI_CALL layerCmdCopyBuffer2(VkCommandBuffer cb, const VkCopyBufferInfo2* pInfo) {
    if (gEnabled) {
        uint64_t bytes = 0;
        for (uint32_t i = 0; i < pInfo->regionCount; ++i) bytes += pInfo->pRegions[i].size;
        bool host;
        { std::lock_guard<std::mutex> lock(gMutex); host = bufferIsHostVisible(pInfo->srcBuffer); }
        addUpload(currentScope(), host ? "copy_buffer_from_host" : "copy_buffer_gpu_only", bytes);
    }
    deviceOf(cb).cmdCopyBuffer2(cb, pInfo);
}

uint64_t imageRegionBytes(VkImage image, const VkBufferImageCopy& r) {
    uint32_t format = VK_FORMAT_R8G8B8A8_UNORM;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        auto it = gResources.find(resourceKey(reinterpret_cast<uint64_t>(image), true));
        if (it != gResources.end()) format = it->second.format;
    }
    const double texel = formatBytes(format);
    return static_cast<uint64_t>(r.imageExtent.width * static_cast<double>(r.imageExtent.height) * r.imageExtent.depth * std::max(1u, r.imageSubresource.layerCount) * texel);
}

VKAPI_ATTR void VKAPI_CALL layerCmdCopyBufferToImage(VkCommandBuffer cb, VkBuffer src, VkImage dst, VkImageLayout layout, uint32_t count, const VkBufferImageCopy* regions) {
    if (gEnabled) {
        uint64_t bytes = 0;
        for (uint32_t i = 0; i < count; ++i) bytes += imageRegionBytes(dst, regions[i]);
        addUpload(currentScope(), "copy_buffer_to_image", bytes);
    }
    deviceOf(cb).cmdCopyBufferToImage(cb, src, dst, layout, count, regions);
}

VKAPI_ATTR void VKAPI_CALL layerCmdCopyBufferToImage2(VkCommandBuffer cb, const VkCopyBufferToImageInfo2* pInfo) {
    if (gEnabled) {
        uint64_t bytes = 0;
        for (uint32_t i = 0; i < pInfo->regionCount; ++i) {
            const VkBufferImageCopy2& r2 = pInfo->pRegions[i];
            VkBufferImageCopy r{};
            r.imageSubresource = r2.imageSubresource;
            r.imageExtent = r2.imageExtent;
            bytes += imageRegionBytes(pInfo->dstImage, r);
        }
        addUpload(currentScope(), "copy_buffer_to_image", bytes);
    }
    deviceOf(cb).cmdCopyBufferToImage2(cb, pInfo);
}

VKAPI_ATTR void VKAPI_CALL layerCmdUpdateBuffer(VkCommandBuffer cb, VkBuffer dst, VkDeviceSize offset, VkDeviceSize size, const void* pData) {
    if (gEnabled) addUpload(currentScope(), "update_buffer", size);
    deviceOf(cb).cmdUpdateBuffer(cb, dst, offset, size, pData);
}

VKAPI_ATTR void VKAPI_CALL layerCmdPushConstants(VkCommandBuffer cb, VkPipelineLayout layout, VkShaderStageFlags stages, uint32_t offset, uint32_t size, const void* pValues) {
    if (gEnabled) addUpload(currentScope(), "push_constants", size);
    deviceOf(cb).cmdPushConstants(cb, layout, stages, offset, size, pValues);
}

VKAPI_ATTR void VKAPI_CALL layerUpdateDescriptorSets(VkDevice device, uint32_t writeCount, const VkWriteDescriptorSet* pWrites, uint32_t copyCount, const VkCopyDescriptorSet* pCopies) {
    if (gEnabled) {
        // Descriptor payload size by type: buffer and image infos are 24 bytes each on the CPU side, a texel-buffer view handle 8,
        // an acceleration structure handle 8. The count of writes is the figure that matters; the bytes are an estimate.
        uint64_t descriptors = 0;
        for (uint32_t i = 0; i < writeCount; ++i) descriptors += pWrites[i].descriptorCount;
        addUpload(currentScope(), "descriptor_writes_bytes_est", descriptors * 24, writeCount);
        if (copyCount) addUpload(currentScope(), "descriptor_copies", copyCount, copyCount);
    }
    deviceOf(device).updateDescriptorSets(device, writeCount, pWrites, copyCount, pCopies);
}

// ---- engine-facing exports --------------------------------------------------------------------------------------------------------

struct Intercept { const char* name; PFN_vkVoidFunction fn; bool deviceLevel; };

const Intercept kIntercepts[] = {
    {"vkCreateInstance", reinterpret_cast<PFN_vkVoidFunction>(layerCreateInstance), false},
    {"vkDestroyInstance", reinterpret_cast<PFN_vkVoidFunction>(layerDestroyInstance), false},
    {"vkCreateDevice", reinterpret_cast<PFN_vkVoidFunction>(layerCreateDevice), false},
    {"vkDestroyDevice", reinterpret_cast<PFN_vkVoidFunction>(layerDestroyDevice), true},
    {"vkAllocateMemory", reinterpret_cast<PFN_vkVoidFunction>(layerAllocateMemory), true},
    {"vkFreeMemory", reinterpret_cast<PFN_vkVoidFunction>(layerFreeMemory), true},
    {"vkCreateBuffer", reinterpret_cast<PFN_vkVoidFunction>(layerCreateBuffer), true},
    {"vkDestroyBuffer", reinterpret_cast<PFN_vkVoidFunction>(layerDestroyBuffer), true},
    {"vkCreateImage", reinterpret_cast<PFN_vkVoidFunction>(layerCreateImage), true},
    {"vkDestroyImage", reinterpret_cast<PFN_vkVoidFunction>(layerDestroyImage), true},
    {"vkBindBufferMemory", reinterpret_cast<PFN_vkVoidFunction>(layerBindBufferMemory), true},
    {"vkBindImageMemory", reinterpret_cast<PFN_vkVoidFunction>(layerBindImageMemory), true},
    {"vkBindBufferMemory2", reinterpret_cast<PFN_vkVoidFunction>(layerBindBufferMemory2), true},
    {"vkBindBufferMemory2KHR", reinterpret_cast<PFN_vkVoidFunction>(layerBindBufferMemory2), true},
    {"vkBindImageMemory2", reinterpret_cast<PFN_vkVoidFunction>(layerBindImageMemory2), true},
    {"vkBindImageMemory2KHR", reinterpret_cast<PFN_vkVoidFunction>(layerBindImageMemory2), true},
    {"vkMapMemory", reinterpret_cast<PFN_vkVoidFunction>(layerMapMemory), true},
    {"vkUnmapMemory", reinterpret_cast<PFN_vkVoidFunction>(layerUnmapMemory), true},
    {"vkCmdCopyBuffer", reinterpret_cast<PFN_vkVoidFunction>(layerCmdCopyBuffer), true},
    {"vkCmdCopyBuffer2", reinterpret_cast<PFN_vkVoidFunction>(layerCmdCopyBuffer2), true},
    {"vkCmdCopyBuffer2KHR", reinterpret_cast<PFN_vkVoidFunction>(layerCmdCopyBuffer2), true},
    {"vkCmdCopyBufferToImage", reinterpret_cast<PFN_vkVoidFunction>(layerCmdCopyBufferToImage), true},
    {"vkCmdCopyBufferToImage2", reinterpret_cast<PFN_vkVoidFunction>(layerCmdCopyBufferToImage2), true},
    {"vkCmdCopyBufferToImage2KHR", reinterpret_cast<PFN_vkVoidFunction>(layerCmdCopyBufferToImage2), true},
    {"vkCmdUpdateBuffer", reinterpret_cast<PFN_vkVoidFunction>(layerCmdUpdateBuffer), true},
    {"vkCmdPushConstants", reinterpret_cast<PFN_vkVoidFunction>(layerCmdPushConstants), true},
    {"vkUpdateDescriptorSets", reinterpret_cast<PFN_vkVoidFunction>(layerUpdateDescriptorSets), true},
};

void enableFromEnvironment() {
    static bool done = false;
    if (done) return;
    done = true;
    const char* dir = std::getenv("VIXEN_GPU_LEDGER_DIR");
    if (dir && *dir) { gDir = dir; gEnabled = true; }
}

}  // namespace

LEDGER_EXPORT void VixenLedgerPushScope(const char* name) { tScope.emplace_back(name ? name : "(null)"); }
LEDGER_EXPORT void VixenLedgerPopScope() { if (!tScope.empty()) tScope.pop_back(); }

// The engine calls this once per tick with the new frame number: the frame that just ended is closed (mapped diff, upload rows).
LEDGER_EXPORT void VixenLedgerSetFrame(uint64_t frame) {
    if (!gEnabled) return;
    std::lock_guard<std::mutex> lock(gMutex);
    if (gHaveFrame && frame != gFrame) finishFrameLocked(gFrame);
    gFrame = frame;
    gHaveFrame = true;
}

// A node asked for the mapped pointer of a ring slot: the slot is one the CPU may write this frame.
LEDGER_EXPORT void VixenLedgerTouch(const void* mapped, uint64_t bytes) {
    if (!gEnabled || !mapped) return;
    std::lock_guard<std::mutex> lock(gMutex);
    const uint8_t* p = static_cast<const uint8_t*>(mapped);
    for (auto& [h, m] : gMemory) {
        if (m.mapped && p >= m.mapped && p < m.mapped + m.mappedSize) { m.touchedThisFrame += bytes; return; }
    }
}

// A node copied `bytes` into the mapped slot at `mapped` (the real write volume, where the site reports it).
LEDGER_EXPORT void VixenLedgerWritten(const void* mapped, uint64_t bytes) {
    if (!gEnabled || !mapped) return;
    std::lock_guard<std::mutex> lock(gMutex);
    const uint8_t* p = static_cast<const uint8_t*>(mapped);
    for (auto& [h, m] : gMemory) {
        if (m.mapped && p >= m.mapped && p < m.mapped + m.mappedSize) { m.writtenThisFrame += bytes; return; }
    }
}

LEDGER_EXPORT void VixenLedgerFlush() {
    std::lock_guard<std::mutex> lock(gMutex);
    flushAllLocked();
}

extern "C" {

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vixenGpuLedgerGetDeviceProcAddr(VkDevice device, const char* pName) {
    for (const Intercept& i : kIntercepts) if (i.deviceLevel && std::strcmp(i.name, pName) == 0) return i.fn;
    PFN_vkGetDeviceProcAddr next = deviceOf(device).getDeviceProcAddr;
    return next ? next(device, pName) : nullptr;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vixenGpuLedgerGetInstanceProcAddr(VkInstance instance, const char* pName) {
    enableFromEnvironment();
    for (const Intercept& i : kIntercepts) if (std::strcmp(i.name, pName) == 0) return i.fn;
    if (std::strcmp(pName, "vkGetInstanceProcAddr") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vixenGpuLedgerGetInstanceProcAddr);
    if (std::strcmp(pName, "vkGetDeviceProcAddr") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vixenGpuLedgerGetDeviceProcAddr);
    if (!instance) return nullptr;
    PFN_vkGetInstanceProcAddr next = nullptr;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        auto it = gInstances.find(dispatchKey(instance));
        if (it != gInstances.end()) next = it->second.getInstanceProcAddr;
    }
    return next ? next(instance, pName) : nullptr;
}

VKAPI_ATTR VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* pVersionStruct) {
    if (!pVersionStruct || pVersionStruct->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT) return VK_ERROR_INITIALIZATION_FAILED;
    if (pVersionStruct->loaderLayerInterfaceVersion > 2) pVersionStruct->loaderLayerInterfaceVersion = 2;
    pVersionStruct->pfnGetInstanceProcAddr = vixenGpuLedgerGetInstanceProcAddr;
    pVersionStruct->pfnGetDeviceProcAddr = vixenGpuLedgerGetDeviceProcAddr;
    pVersionStruct->pfnGetPhysicalDeviceProcAddr = nullptr;
    return VK_SUCCESS;
}

}  // extern "C"
