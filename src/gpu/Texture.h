#pragma once
#include "gpu/VulkanContext.h"
#include "imgui.h"

// A sampled RGBA8 texture that ImGui can draw (brush previews, thumbnails, render results).
struct GpuTexture {
    VkImage        image   = VK_NULL_HANDLE;
    VkDeviceMemory memory  = VK_NULL_HANDLE;
    VkImageView    view    = VK_NULL_HANDLE;
    VkSampler      sampler = VK_NULL_HANDLE;
    VkDescriptorSet descriptor = VK_NULL_HANDLE;
    int width = 0, height = 0;

    bool Create(VulkanContext& vk, const unsigned char* rgba, int w, int h);
    // Re-uploads the whole image into an existing texture of the same size (used while painting).
    bool Update(VulkanContext& vk, const unsigned char* rgba, int w, int h);
    void Destroy(VulkanContext& vk);
    bool Valid() const { return descriptor != VK_NULL_HANDLE; }
    ImTextureID Id() const { return (ImTextureID)descriptor; }
};
