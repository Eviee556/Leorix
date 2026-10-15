#include "gpu/VulkanContext.h"
#include "imgui_impl_vulkan.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

void CheckVk(VkResult result) {
    if (result == VK_SUCCESS) return;
    std::fprintf(stderr, "[vulkan] VkResult = %d\n", result);
    if (result < 0) std::abort();
}

static bool HasExtension(const std::vector<VkExtensionProperties>& props, const char* name) {
    for (const auto& p : props)
        if (std::strcmp(p.extensionName, name) == 0) return true;
    return false;
}

static std::vector<VkExtensionProperties> DeviceExtensions(VkPhysicalDevice dev) {
    uint32_t n = 0;
    vkEnumerateDeviceExtensionProperties(dev, nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> props(n);
    vkEnumerateDeviceExtensionProperties(dev, nullptr, &n, props.data());
    return props;
}

static GpuInfo Describe(VkPhysicalDevice dev) {
    VkPhysicalDeviceProperties p{};
    vkGetPhysicalDeviceProperties(dev, &p);
    GpuInfo info;
    info.name = p.deviceName;
    info.apiVersion = p.apiVersion;
    switch (p.deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   info.type = "Discrete";   break;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: info.type = "Integrated"; break;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    info.type = "Virtual";    break;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:            info.type = "CPU";        break;
        default:                                     info.type = "Other";      break;
    }
    VkPhysicalDeviceMemoryProperties mem{};
    vkGetPhysicalDeviceMemoryProperties(dev, &mem);
    for (uint32_t i = 0; i < mem.memoryHeapCount; ++i)
        if (mem.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
            info.vramBytes = std::max<uint64_t>(info.vramBytes, mem.memoryHeaps[i].size);

    auto ext = DeviceExtensions(dev);
    info.accelerationStructure = HasExtension(ext, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
    info.rayTracingPipeline    = HasExtension(ext, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
    info.rayQuery              = HasExtension(ext, VK_KHR_RAY_QUERY_EXTENSION_NAME);
    return info;
}

// Prefer discrete over integrated, then hardware ray tracing, then VRAM.
// This avoids the dual-GPU trap Electron had (landing on the integrated chip).
static int64_t Score(const GpuInfo& g) {
    int64_t s = 0;
    if (g.type == "Discrete") s += 1'000'000'000'000LL;
    else if (g.type == "Integrated") s += 100'000'000'000LL;
    if (g.HardwareRayTracing()) s += 10'000'000'000LL;
    s += static_cast<int64_t>(g.vramBytes / (1024 * 1024));
    return s;
}

bool VulkanContext::Init(const std::vector<const char*>& instanceExtensions, std::string& error) {
    if (volkInitialize() != VK_SUCCESS) {
        error = "Vulkan is not available. Update your GPU driver (vulkan-1.dll was not found).";
        return false;
    }

    // ---- instance
    {
        std::vector<const char*> exts = instanceExtensions;
        uint32_t n = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &n, nullptr);
        std::vector<VkExtensionProperties> props(n);
        vkEnumerateInstanceExtensionProperties(nullptr, &n, props.data());
        if (HasExtension(props, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
            exts.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

        VkApplicationInfo app{};
        app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app.pApplicationName = "Leorix";
        app.applicationVersion = VK_MAKE_VERSION(2, 0, 0);
        app.pEngineName = "Leorix Render";
        app.apiVersion = VK_API_VERSION_1_3;

        VkInstanceCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ci.pApplicationInfo = &app;
        ci.enabledExtensionCount = static_cast<uint32_t>(exts.size());
        ci.ppEnabledExtensionNames = exts.data();
        VkResult r = vkCreateInstance(&ci, nullptr, &instance);
        if (r != VK_SUCCESS) { error = "vkCreateInstance failed (" + std::to_string(r) + ")"; return false; }
        volkLoadInstance(instance);
    }

    // ---- pick the best GPU
    {
        uint32_t n = 0;
        vkEnumeratePhysicalDevices(instance, &n, nullptr);
        if (n == 0) { error = "No Vulkan-capable GPU found."; return false; }
        std::vector<VkPhysicalDevice> devs(n);
        vkEnumeratePhysicalDevices(instance, &n, devs.data());

        int64_t best = -1;
        for (VkPhysicalDevice d : devs) {
            GpuInfo g = Describe(d);
            allGpus.push_back(g);
            if (Score(g) > best) { best = Score(g); physical = d; gpu = g; }
        }
    }

    queueFamily = ImGui_ImplVulkanH_SelectQueueFamilyIndex(physical);
    if (queueFamily == UINT32_MAX) { error = "Selected GPU has no graphics queue."; return false; }

    // ---- logical device (ray tracing extensions get enabled in Milestone 2 with their feature structs)
    {
        std::vector<const char*> exts = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
        const float priority = 1.0f;
        VkDeviceQueueCreateInfo q{};
        q.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        q.queueFamilyIndex = queueFamily;
        q.queueCount = 1;
        q.pQueuePriorities = &priority;

        VkDeviceCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        ci.queueCreateInfoCount = 1;
        ci.pQueueCreateInfos = &q;
        ci.enabledExtensionCount = static_cast<uint32_t>(exts.size());
        ci.ppEnabledExtensionNames = exts.data();
        VkResult r = vkCreateDevice(physical, &ci, nullptr, &device);
        if (r != VK_SUCCESS) { error = "vkCreateDevice failed (" + std::to_string(r) + ")"; return false; }
        volkLoadDevice(device);
        vkGetDeviceQueue(device, queueFamily, 0, &queue);
    }

    return true;
}

void VulkanContext::Shutdown() {
    if (descriptorPool) vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    if (device) vkDestroyDevice(device, nullptr);
    if (instance) vkDestroyInstance(instance, nullptr);
    descriptorPool = VK_NULL_HANDLE; device = VK_NULL_HANDLE; instance = VK_NULL_HANDLE;
}
