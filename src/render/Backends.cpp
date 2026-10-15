#include "render/Backends.h"
#include "gpu/VulkanContext.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static bool DllPresent(const char* name) {
    // Only checks that the driver runtime is installed; nothing is initialized.
    HMODULE h = LoadLibraryA(name);
    if (!h) return false;
    FreeLibrary(h);
    return true;
}
#else
static bool DllPresent(const char*) { return false; }
#endif

std::vector<BackendStatus> DetectBackends(const GpuInfo& g) {
    std::vector<BackendStatus> out;

    BackendStatus vk{ BackendId::VulkanRT, "Vulkan RT" };
    vk.runtimeFound = g.HardwareRayTracing();
    vk.detail = vk.runtimeFound
        ? "Hardware ray tracing available on " + g.name
        : "This GPU has no hardware ray tracing (compute fallback planned)";
    out.push_back(vk);

    BackendStatus cuda{ BackendId::CudaOptix, "CUDA + OptiX" };
    bool cudaRt = DllPresent("nvcuda.dll");
    bool optix  = DllPresent("nvoptix.dll");
    cuda.runtimeFound = cudaRt;
    cuda.detail = cudaRt ? (optix ? "NVIDIA CUDA and OptiX runtimes found" : "NVIDIA CUDA runtime found (no OptiX)")
                         : "No NVIDIA driver runtime (expected on AMD systems)";
    out.push_back(cuda);

    BackendStatus hip{ BackendId::AmdHip, "AMD HIP (ROCm)" };
    hip.runtimeFound = DllPresent("amdhip64_6.dll") || DllPresent("amdhip64.dll");
    hip.detail = hip.runtimeFound ? "AMD HIP runtime found in the Radeon driver"
                                  : "No AMD HIP runtime (install a recent Radeon driver)";
    out.push_back(hip);
    return out;
}
