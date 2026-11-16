#pragma once
// A PBR material slot set, matching the maps Leorix paints: the same list the 2D side exports.
// Textures are uploaded to the GPU so the viewport can draw them in Material Preview.
#include "gpu/Texture.h"
#include <cmath>
#include <string>
#include <vector>

enum class MapSlot { BaseColor, Normal, Roughness, Metallic, AO, Height, Emissive,
                     Specular, Opacity, Subsurface, Count };

inline const char* MapSlotName(MapSlot s) {
    switch (s) {
        case MapSlot::BaseColor: return "Base Color";
        case MapSlot::Normal:    return "Normal";
        case MapSlot::Roughness: return "Roughness";
        case MapSlot::Metallic:  return "Metallic";
        case MapSlot::AO:        return "Ambient Occlusion";
        case MapSlot::Height:    return "Height";
        case MapSlot::Emissive:  return "Emissive";
        case MapSlot::Specular:  return "Specular";
        case MapSlot::Opacity:   return "Opacity";
        case MapSlot::Subsurface:return "Subsurface";
        default: return "";
    }
}

// A texture kept on the CPU as well as the GPU: the viewport draws from the GPU copy, while the
// path tracer and anything else that shades per hit samples the CPU copy.
struct TextureData {
    int width = 0, height = 0;
    std::vector<uint8_t> pixels;                  // RGBA8
    bool Valid() const { return width > 0 && height > 0 && !pixels.empty(); }
    void Clear() { width = height = 0; pixels.clear(); }
    // Bilinear sample, tiling outside 0..1. Returns linear-ish values; sRGB decode is the
    // caller's business because only colour maps need it.
    void Sample(float u, float v, float out[4]) const {
        if (!Valid()) { out[0] = out[1] = out[2] = out[3] = 1.0f; return; }
        u = u - std::floor(u);
        v = v - std::floor(v);
        float fx = u * width - 0.5f, fy = (1.0f - v) * height - 0.5f;
        int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
        float tx = fx - x0, ty = fy - y0;
        auto texel = [&](int x, int y, int c) {
            x = ((x % width) + width) % width;
            y = ((y % height) + height) % height;
            return pixels[((size_t)y * width + x) * 4 + c] / 255.0f;
        };
        for (int c = 0; c < 4; ++c) {
            float a = texel(x0, y0, c) * (1 - tx) + texel(x0 + 1, y0, c) * tx;
            float b = texel(x0, y0 + 1, c) * (1 - tx) + texel(x0 + 1, y0 + 1, c) * tx;
            out[c] = a * (1 - ty) + b * ty;
        }
    }
    float SampleScalar(float u, float v) const { float c[4]; Sample(u, v, c); return c[0]; }
};

struct Material {
    GpuTexture  maps[(int)MapSlot::Count];
    TextureData cpu[(int)MapSlot::Count];         // same images, sampleable while shading
    std::string paths[(int)MapSlot::Count];
    float baseColor[3] = {0.78f, 0.76f, 0.72f};
    float roughness = 0.5f, metallic = 0.0f, emissiveStrength = 1.0f;

    // Subsurface scattering. The viewport approximates it with wrapped diffuse plus back-lit
    // transmission tinted by the radius, which is what makes skin, wax and marble read correctly
    // at a glance; the real per-channel diffusion profile belongs to the path tracer.
    // used by the path tracer
    // Emission works like Blender's: a colour (black by default, so nothing glows) times a
    // strength. Loading an Emissive map replaces the colour, so the map lights up immediately.
    float emissionColor[3] = {0.0f, 0.0f, 0.0f};
    float emissiveStrengthRT = 1.0f;
    float transmission = 0.0f;         // 1 = glass
    float transmissionIOR = 1.45f;
    // Off: the surface still glows, but the tracer doesn't aim shadow rays at it. For large, dim
    // emitters (sky cards, distant clouds) that would otherwise take samples from the real lights.
    bool  sampleAsLight = true;

    float sssWeight = 0.0f;                       // 0 = off
    float sssRadius[3] = {1.0f, 0.2f, 0.1f};      // per-channel mean free path, Blender's skin default
    float sssScale = 0.05f;                       // metres
    float sssAnisotropy = 0.0f;
    float sssIOR = 1.4f;
    int   sssMethod = 0;                          // Christensen-Burley, Random Walk
    bool  useTextures = true;

    // Parallax occlusion, driven by the Height map.
    bool  parallax = false;
    float parallaxScale = 0.04f;
    int   parallaxSteps = 16;

    float specular = 0.5f;          // dielectric reflectance when there is no Specular map
    float opacity = 1.0f;

    bool Has(MapSlot s) const { return maps[(int)s].Valid(); }
    bool HasCpu(MapSlot s) const { return cpu[(int)s].Valid(); }
    void Release(VulkanContext& vk) {
        for (auto& m : maps) m.Destroy(vk);
        for (auto& c : cpu) c.Clear();
        for (auto& p : paths) p.clear();
    }
};

// Copies every setting of src into dst except the textures (maps, cpu pixels, paths), which stay
// as they were in dst. Undo snapshots use it so a slider drag never copies hundreds of MB.
void CopyMaterialParams(Material& dst, Material& src);

// Node Wrangler style: work out which slot a file belongs in from its name
// ("rock_Normal_DX.png" -> Normal). Returns MapSlot::Count when nothing matches.
MapSlot GuessMapSlot(const std::string& filename);

// Loads one image file into a slot of the material (GPU + CPU copies).
bool LoadMaterialMap(VulkanContext& vk, Material& mat, MapSlot slot, const std::string& path, std::string& error);
// Assigns raw RGBA pixels to a slot, used when a painted map is dropped onto it.
bool SetMaterialMap(VulkanContext& vk, Material& mat, MapSlot slot, const unsigned char* rgba,
                    int width, int height, const std::string& label);
