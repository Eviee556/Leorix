#include "render/Material.h"
#include "stb_image.h"          // implementation lives in tools/BrushImage.cpp
#include <algorithm>
#include <cctype>
#include <vector>

// Suffix tables, ordered so the more specific names win (e.g. "_basecolor" before "_base").
MapSlot GuessMapSlot(const std::string& filename) {
    std::string n = filename;
    std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return std::tolower(c); });
    size_t slash = n.find_last_of("/\\");
    if (slash != std::string::npos) n = n.substr(slash + 1);

    struct Rule { const char* token; MapSlot slot; };
    static const Rule rules[] = {
        {"basecolor", MapSlot::BaseColor}, {"base_color", MapSlot::BaseColor}, {"albedo", MapSlot::BaseColor},
        {"diffuse", MapSlot::BaseColor},   {"_diff", MapSlot::BaseColor},      {"_col", MapSlot::BaseColor},
        {"normalgl", MapSlot::Normal},     {"normaldx", MapSlot::Normal},      {"normal", MapSlot::Normal},
        {"_nrm", MapSlot::Normal},         {"_nor", MapSlot::Normal},
        {"roughness", MapSlot::Roughness}, {"_rough", MapSlot::Roughness},     {"_rgh", MapSlot::Roughness},
        {"metallic", MapSlot::Metallic},   {"metalness", MapSlot::Metallic},   {"_metal", MapSlot::Metallic},
        {"ambientocclusion", MapSlot::AO}, {"occlusion", MapSlot::AO},         {"_ao", MapSlot::AO},
        {"height", MapSlot::Height},       {"displacement", MapSlot::Height},  {"_disp", MapSlot::Height},
        {"emissive", MapSlot::Emissive},   {"emission", MapSlot::Emissive},    {"_emit", MapSlot::Emissive},
        {"specular", MapSlot::Specular},   {"_spec", MapSlot::Specular},
        {"opacity", MapSlot::Opacity},     {"alpha", MapSlot::Opacity},        {"_mask", MapSlot::Opacity},
        {"subsurface", MapSlot::Subsurface}, {"_sss", MapSlot::Subsurface},    {"scatter", MapSlot::Subsurface},
    };
    for (const auto& r : rules)
        if (n.find(r.token) != std::string::npos) return r.slot;
    return MapSlot::Count;
}

bool LoadMaterialMap(VulkanContext& vk, Material& mat, MapSlot slot, const std::string& path, std::string& error) {
    int w = 0, h = 0, channels = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!pixels) { error = std::string("Could not read ") + path + ": " + stbi_failure_reason(); return false; }
    if (w > 8192 || h > 8192) {
        stbi_image_free(pixels);
        error = "Texture is larger than 8192 px; resize it first.";
        return false;
    }
    bool ok = mat.maps[(int)slot].Create(vk, pixels, w, h);
    if (ok) {
        TextureData& cpu = mat.cpu[(int)slot];
        cpu.width = w; cpu.height = h;
        cpu.pixels.assign(pixels, pixels + (size_t)w * h * 4);
    }
    stbi_image_free(pixels);
    if (!ok) { error = "Could not upload the texture to the GPU."; return false; }
    mat.paths[(int)slot] = path;
    return true;
}

bool SetMaterialMap(VulkanContext& vk, Material& mat, MapSlot slot, const unsigned char* rgba,
                    int width, int height, const std::string& label) {
    if (!rgba || width <= 0 || height <= 0) return false;
    mat.maps[(int)slot].Destroy(vk);
    if (!mat.maps[(int)slot].Create(vk, rgba, width, height)) return false;
    TextureData& cpu = mat.cpu[(int)slot];
    cpu.width = width; cpu.height = height;
    cpu.pixels.assign(rgba, rgba + (size_t)width * height * 4);
    mat.paths[(int)slot] = label;
    return true;
}
