#pragma once
#include <volk.h>
#include <cstdint>
#include <string>
#include <vector>

// What a GPU can do. Filled for every GPU in the system so the UI can show the user
// which card Leorix picked and why.
struct GpuInfo {
    std::string name;
    std::string type;            // "Discrete", "Integrated", ...
    uint32_t    apiVersion = 0;
    uint64_t    vramBytes  = 0;
    bool        accelerationStructure = false;  // VK_KHR_acceleration_structure
    bool        rayTracingPipeline    = false;  // VK_KHR_ray_tracing_pipeline
    bool        rayQuery              = false;  // VK_KHR_ray_query
    bool HardwareRayTracing() const { return accelerationStructure && rayTracingPipeline; }
};

struct VulkanContext {
    VkInstance       instance       = VK_NULL_HANDLE;
    VkPhysicalDevice physical       = VK_NULL_HANDLE;
    VkDevice         device         = VK_NULL_HANDLE;
    uint32_t         queueFamily    = UINT32_MAX;
    VkQueue          queue          = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkPipelineCache  pipelineCache  = VK_NULL_HANDLE;

    GpuInfo              gpu;       // the selected device
    std::vector<GpuInfo> allGpus;   // every device found

    bool Init(const std::vector<const char*>& instanceExtensions, std::string& error);
    void Shutdown();
};

void CheckVk(VkResult result);
