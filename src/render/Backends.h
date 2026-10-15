#pragma once
#include <string>
#include <vector>

// Render backends Leorix plans to support. Milestone 1 only detects which GPU runtimes
// are installed on this PC; the engines themselves arrive in later milestones.
enum class BackendId { VulkanRT, CudaOptix, AmdHip };

struct BackendStatus {
    BackendId   id;
    std::string name;
    bool        runtimeFound = false;
    std::string detail;
};

struct GpuInfo;
std::vector<BackendStatus> DetectBackends(const GpuInfo& vulkanGpu);
