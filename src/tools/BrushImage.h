#pragma once
#include "gpu/Texture.h"
#include <string>
#include <vector>

// A user-imported image used as a sculpt brush. Stored as a height field (luminance x alpha,
// the same rule Leorix's 2D texture brushes use): white raises the surface, black leaves it.
struct BrushImage {
    std::string        name;
    std::string        path;
    int                width = 0, height = 0;
    std::vector<float> heights;      // width*height, 0..1, sampled by the sculpt engine
    GpuTexture         original;     // the image as imported (shown in the 2D viewport)
    GpuTexture         heightPreview;// grayscale height interpretation

    bool Load(VulkanContext& vk, const std::string& filePath, std::string& error);
    void Release(VulkanContext& vk);
    bool Loaded() const { return !heights.empty(); }
};
