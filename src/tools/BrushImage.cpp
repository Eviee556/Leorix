#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#include "stb_image.h"
#include "tools/BrushImage.h"
#include <algorithm>
#include <cstdio>

bool BrushImage::Load(VulkanContext& vk, const std::string& filePath, std::string& error) {
    int w = 0, h = 0, channels = 0;
    unsigned char* pixels = stbi_load(filePath.c_str(), &w, &h, &channels, 4);
    if (!pixels) { error = std::string("Could not read image: ") + stbi_failure_reason(); return false; }

    // Sculpt stamps don't need huge resolutions; cap the GPU preview so 16K images don't stall.
    const int maxSide = 4096;
    if (w > maxSide || h > maxSide) {
        stbi_image_free(pixels);
        error = "Image is larger than 4096 px on a side. Please resize it first.";
        return false;
    }

    Release(vk);
    width = w; height = h; path = filePath;
    size_t slash = filePath.find_last_of("/\\");
    name = slash == std::string::npos ? filePath : filePath.substr(slash + 1);

    heights.resize(static_cast<size_t>(w) * h);
    std::vector<unsigned char> gray(static_cast<size_t>(w) * h * 4);
    for (int i = 0; i < w * h; ++i) {
        const unsigned char* p = pixels + i * 4;
        float lum = (p[0] * 0.299f + p[1] * 0.587f + p[2] * 0.114f) / 255.0f;
        float v = lum * (p[3] / 255.0f);
        heights[i] = v;
        unsigned char g = static_cast<unsigned char>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
        gray[i * 4] = gray[i * 4 + 1] = gray[i * 4 + 2] = g; gray[i * 4 + 3] = 255;
    }
    bool ok = original.Create(vk, pixels, w, h) && heightPreview.Create(vk, gray.data(), w, h);
    stbi_image_free(pixels);
    if (!ok) { Release(vk); error = "Could not upload the image to the GPU."; return false; }
    return true;
}

void BrushImage::Release(VulkanContext& vk) {
    original.Destroy(vk);
    heightPreview.Destroy(vk);
    heights.clear();
    width = height = 0;
}
