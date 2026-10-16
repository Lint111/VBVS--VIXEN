// VK_LAYER_VIXEN_gpu_witness: records which test processes touch the GPU and fails CPU-classified
// tests that do. It sits below the engine (as a Vulkan implicit layer), so it sees raw vk* calls from
// every code path, including test fixtures that bypass the engine's device wrapper.
//
//   VIXEN_GPU_WITNESS_DIR=<dir>  record mode: append one line per first-seen GPU action to
//                                <dir>/<pid>.tsv, with the process ancestry. Never fails the process.
//   VIXEN_TEST_CLASS=cpu         guard mode: the first GPU action prints a named diagnostic and
//                                exits with status 86.
//
// GPU actions: vkCreateInstance, vkCreateDevice, vkAllocateMemory, vkQueueSubmit/2/2KHR.
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include <time.h>
#include <unistd.h>

namespace {

struct InstanceData {
    PFN_vkGetInstanceProcAddr getInstanceProcAddr = nullptr;
    PFN_vkDestroyInstance destroyInstance = nullptr;
    PFN_vkCreateDevice createDevice = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
};

struct DeviceData {
    PFN_vkGetDeviceProcAddr getDeviceProcAddr = nullptr;
    PFN_vkDestroyDevice destroyDevice = nullptr;
    PFN_vkAllocateMemory allocateMemory = nullptr;
    PFN_vkQueueSubmit queueSubmit = nullptr;
    PFN_vkQueueSubmit2 queueSubmit2 = nullptr;
    PFN_vkQueueSubmit2KHR queueSubmit2KHR = nullptr;
};

std::mutex gMutex;
std::map<void*, InstanceData> gInstances;
std::map<void*, DeviceData> gDevices;

void* dispatchKey(const void* handle) { return *reinterpret_cast<void* const*>(handle); }

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

std::string cmdlineOf(long pid) {
    std::string raw = readFile("/proc/" + std::to_string(pid) + "/cmdline");
    for (char& c : raw) {
        if (c == '\0') c = '\x1f';
    }
    if (!raw.empty() && raw.back() == '\x1f') raw.pop_back();
    return raw;
}

long parentOf(long pid) {
    const std::string stat = readFile("/proc/" + std::to_string(pid) + "/stat");
    const size_t closeParen = stat.rfind(')');
    if (closeParen == std::string::npos) return 0;
    long ppid = 0;
    char state = 0;
    if (std::sscanf(stat.c_str() + closeParen + 1, " %c %ld", &state, &ppid) != 2) return 0;
    return ppid;
}

bool envIs(const char* name, const char* value) {
    const char* v = std::getenv(name);
    return v && std::strcmp(v, value) == 0;
}

std::atomic<unsigned> gSeenMask{0};

void witness(unsigned bit, const char* action) {
    const char* dir = std::getenv("VIXEN_GPU_WITNESS_DIR");
    const bool recording = dir && *dir;

    if (!recording && envIs("VIXEN_TEST_CLASS", "cpu")) {
        std::fprintf(stderr,
                     "\n[VIXEN_TEST_NO_GPU] MISCLASSIFIED TEST: this test is classified 'cpu' but "
                     "called %s (pid %ld: %s).\n"
                     "[VIXEN_TEST_NO_GPU] A CPU-classified test must not create a Vulkan instance or "
                     "device. Run tools/classify-tests-gpu.py to relabel it 'gpu'.\n",
                     action, static_cast<long>(getpid()),
                     [] {
                         std::string c = cmdlineOf(getpid());
                         for (char& ch : c) {
                             if (ch == '\x1f') ch = ' ';
                         }
                         return c;
                     }()
                         .c_str());
        std::fflush(stderr);
        _exit(86);
    }
    if (!recording) return;

    const unsigned prior = gSeenMask.fetch_or(bit);
    if (prior & bit) return;

    const long pid = getpid();
    std::ostringstream line;
    if (prior == 0) {
        // First event in this process: name the test (inherited VIXEN_TEST_NAME) and emit the
        // ancestry once, so the classifier can attribute the process to the ctest test that
        // (transitively) launched it even when a shell exec'd away its own command line.
        if (const char* test = std::getenv("VIXEN_TEST_NAME"); test && *test) {
            line << "test\t" << pid << '\t' << test << '\n';
        }
        for (long p = pid; p > 1;) {
            line << "proc\t" << p << '\t' << cmdlineOf(p) << '\n';
            p = parentOf(p);
        }
    }
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    line << "event\t" << pid << '\t' << action << '\t' << ts.tv_sec << '.' << ts.tv_nsec / 1000000 << '\n';
    const std::string path = std::string(dir) + "/" + std::to_string(pid) + ".tsv";
    if (FILE* f = std::fopen(path.c_str(), "a")) {
        std::fputs(line.str().c_str(), f);
        std::fclose(f);
    }
}

constexpr unsigned kInstance = 1, kDevice = 2, kAlloc = 4, kSubmit = 8;

VkLayerInstanceCreateInfo* findInstanceLink(const VkInstanceCreateInfo* info) {
    auto* p = reinterpret_cast<const VkLayerInstanceCreateInfo*>(info->pNext);
    while (p && !(p->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO && p->function == VK_LAYER_LINK_INFO)) {
        p = reinterpret_cast<const VkLayerInstanceCreateInfo*>(p->pNext);
    }
    return const_cast<VkLayerInstanceCreateInfo*>(p);
}

VkLayerDeviceCreateInfo* findDeviceLink(const VkDeviceCreateInfo* info) {
    auto* p = reinterpret_cast<const VkLayerDeviceCreateInfo*>(info->pNext);
    while (p && !(p->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO && p->function == VK_LAYER_LINK_INFO)) {
        p = reinterpret_cast<const VkLayerDeviceCreateInfo*>(p->pNext);
    }
    return const_cast<VkLayerDeviceCreateInfo*>(p);
}

VKAPI_ATTR VkResult VKAPI_CALL layerCreateInstance(const VkInstanceCreateInfo* pCreateInfo,
                                                   const VkAllocationCallbacks* pAllocator,
                                                   VkInstance* pInstance) {
    witness(kInstance, "vkCreateInstance");
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
    std::lock_guard<std::mutex> lock(gMutex);
    gInstances[dispatchKey(*pInstance)] = data;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL layerDestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator) {
    PFN_vkDestroyInstance next = nullptr;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        auto it = gInstances.find(dispatchKey(instance));
        if (it != gInstances.end()) {
            next = it->second.destroyInstance;
            gInstances.erase(it);
        }
    }
    if (next) next(instance, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL layerCreateDevice(VkPhysicalDevice physicalDevice,
                                                 const VkDeviceCreateInfo* pCreateInfo,
                                                 const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) {
    witness(kDevice, "vkCreateDevice");
    VkLayerDeviceCreateInfo* link = findDeviceLink(pCreateInfo);
    if (!link) return VK_ERROR_INITIALIZATION_FAILED;
    PFN_vkGetInstanceProcAddr nextGipa = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr nextGdpa = link->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    link->u.pLayerInfo = link->u.pLayerInfo->pNext;

    VkInstance instance = VK_NULL_HANDLE;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        auto it = gInstances.find(dispatchKey(physicalDevice));
        if (it != gInstances.end()) instance = it->second.instance;
    }
    auto create = reinterpret_cast<PFN_vkCreateDevice>(nextGipa(instance, "vkCreateDevice"));
    if (!create) return VK_ERROR_INITIALIZATION_FAILED;
    const VkResult result = create(physicalDevice, pCreateInfo, pAllocator, pDevice);
    if (result != VK_SUCCESS) return result;

    DeviceData data;
    data.getDeviceProcAddr = nextGdpa;
    data.destroyDevice = reinterpret_cast<PFN_vkDestroyDevice>(nextGdpa(*pDevice, "vkDestroyDevice"));
    data.allocateMemory = reinterpret_cast<PFN_vkAllocateMemory>(nextGdpa(*pDevice, "vkAllocateMemory"));
    data.queueSubmit = reinterpret_cast<PFN_vkQueueSubmit>(nextGdpa(*pDevice, "vkQueueSubmit"));
    data.queueSubmit2 = reinterpret_cast<PFN_vkQueueSubmit2>(nextGdpa(*pDevice, "vkQueueSubmit2"));
    data.queueSubmit2KHR = reinterpret_cast<PFN_vkQueueSubmit2KHR>(nextGdpa(*pDevice, "vkQueueSubmit2KHR"));
    std::lock_guard<std::mutex> lock(gMutex);
    gDevices[dispatchKey(*pDevice)] = data;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL layerDestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator) {
    PFN_vkDestroyDevice next = nullptr;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        auto it = gDevices.find(dispatchKey(device));
        if (it != gDevices.end()) {
            next = it->second.destroyDevice;
            gDevices.erase(it);
        }
    }
    if (next) next(device, pAllocator);
}

DeviceData deviceOf(const void* handle) {
    std::lock_guard<std::mutex> lock(gMutex);
    auto it = gDevices.find(dispatchKey(handle));
    return it == gDevices.end() ? DeviceData{} : it->second;
}

VKAPI_ATTR VkResult VKAPI_CALL layerAllocateMemory(VkDevice device, const VkMemoryAllocateInfo* pAllocateInfo,
                                                   const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMemory) {
    witness(kAlloc, "vkAllocateMemory");
    return deviceOf(device).allocateMemory(device, pAllocateInfo, pAllocator, pMemory);
}

VKAPI_ATTR VkResult VKAPI_CALL layerQueueSubmit(VkQueue queue, uint32_t count, const VkSubmitInfo* pSubmits, VkFence fence) {
    witness(kSubmit, "vkQueueSubmit");
    return deviceOf(queue).queueSubmit(queue, count, pSubmits, fence);
}

VKAPI_ATTR VkResult VKAPI_CALL layerQueueSubmit2(VkQueue queue, uint32_t count, const VkSubmitInfo2* pSubmits, VkFence fence) {
    witness(kSubmit, "vkQueueSubmit2");
    return deviceOf(queue).queueSubmit2(queue, count, pSubmits, fence);
}

VKAPI_ATTR VkResult VKAPI_CALL layerQueueSubmit2KHR(VkQueue queue, uint32_t count, const VkSubmitInfo2* pSubmits, VkFence fence) {
    witness(kSubmit, "vkQueueSubmit2KHR");
    return deviceOf(queue).queueSubmit2KHR(queue, count, pSubmits, fence);
}

struct Intercept {
    const char* name;
    PFN_vkVoidFunction fn;
    bool deviceLevel;
};

const Intercept kIntercepts[] = {
    {"vkCreateInstance", reinterpret_cast<PFN_vkVoidFunction>(layerCreateInstance), false},
    {"vkDestroyInstance", reinterpret_cast<PFN_vkVoidFunction>(layerDestroyInstance), false},
    {"vkCreateDevice", reinterpret_cast<PFN_vkVoidFunction>(layerCreateDevice), false},
    {"vkDestroyDevice", reinterpret_cast<PFN_vkVoidFunction>(layerDestroyDevice), true},
    {"vkAllocateMemory", reinterpret_cast<PFN_vkVoidFunction>(layerAllocateMemory), true},
    {"vkQueueSubmit", reinterpret_cast<PFN_vkVoidFunction>(layerQueueSubmit), true},
    {"vkQueueSubmit2", reinterpret_cast<PFN_vkVoidFunction>(layerQueueSubmit2), true},
    {"vkQueueSubmit2KHR", reinterpret_cast<PFN_vkVoidFunction>(layerQueueSubmit2KHR), true},
};

}  // namespace

extern "C" {

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vixenGpuWitnessGetDeviceProcAddr(VkDevice device, const char* pName) {
    for (const Intercept& i : kIntercepts) {
        if (i.deviceLevel && std::strcmp(i.name, pName) == 0) return i.fn;
    }
    PFN_vkGetDeviceProcAddr next = deviceOf(device).getDeviceProcAddr;
    return next ? next(device, pName) : nullptr;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vixenGpuWitnessGetInstanceProcAddr(VkInstance instance, const char* pName) {
    for (const Intercept& i : kIntercepts) {
        if (std::strcmp(i.name, pName) == 0) return i.fn;
    }
    if (std::strcmp(pName, "vkGetInstanceProcAddr") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vixenGpuWitnessGetInstanceProcAddr);
    if (std::strcmp(pName, "vkGetDeviceProcAddr") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vixenGpuWitnessGetDeviceProcAddr);
    if (!instance) return nullptr;
    PFN_vkGetInstanceProcAddr next = nullptr;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        auto it = gInstances.find(dispatchKey(instance));
        if (it != gInstances.end()) next = it->second.getInstanceProcAddr;
    }
    return next ? next(instance, pName) : nullptr;
}

__attribute__((visibility("default"))) VKAPI_ATTR VkResult VKAPI_CALL
vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* pVersionStruct) {
    if (!pVersionStruct || pVersionStruct->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (pVersionStruct->loaderLayerInterfaceVersion > 2) pVersionStruct->loaderLayerInterfaceVersion = 2;
    pVersionStruct->pfnGetInstanceProcAddr = vixenGpuWitnessGetInstanceProcAddr;
    pVersionStruct->pfnGetDeviceProcAddr = vixenGpuWitnessGetDeviceProcAddr;
    pVersionStruct->pfnGetPhysicalDeviceProcAddr = nullptr;
    return VK_SUCCESS;
}

}  // extern "C"
