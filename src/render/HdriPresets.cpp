#include "render/HdriPresets.h"
#include <algorithm>
#include <cmath>

namespace {
const float kPi = 3.14159265f;

struct Col { float r = 0, g = 0, b = 0; };
inline Col operator+(const Col& a, const Col& b) { return {a.r + b.r, a.g + b.g, a.b + b.b}; }
inline Col operator*(const Col& a, float s) { return {a.r * s, a.g * s, a.b * s}; }
inline Col Mix(const Col& a, const Col& b, float t) { return a * (1 - t) + b * t; }

// A direction on the unit sphere for an equirectangular pixel.
void DirFor(float u, float v, float& x, float& y, float& z) {
    float phi = u * 2.0f * kPi;
    float theta = v * kPi;
    y = std::cos(theta);
    float s = std::sin(theta);
    x = s * std::cos(phi);
    z = s * std::sin(phi);
}

// Soft circular light, given its direction and angular radius.
float Disc(float dx, float dy, float dz, float lx, float ly, float lz, float cosSize, float softness) {
    float d = dx * lx + dy * ly + dz * lz;
    if (d <= cosSize - softness) return 0.0f;
    if (d >= cosSize) return 1.0f;
    float t = (d - (cosSize - softness)) / softness;
    return t * t * (3 - 2 * t);
}

// Axis-aligned rectangle in the equirect image, used for softboxes and strip lights.
float Panel(float u, float v, float u0, float u1, float v0, float v1, float feather) {
    auto band = [&](float x, float a, float b) {
        if (x < a - feather || x > b + feather) return 0.0f;
        if (x < a) return (x - (a - feather)) / feather;
        if (x > b) return ((b + feather) - x) / feather;
        return 1.0f;
    };
    return band(u, u0, u1) * band(v, v0, v1);
}
} // namespace

const char* HdriPresetName(HdriPreset p) {
    switch (p) {
        case HdriPreset::StudioSoftbox:  return "Studio Softbox";
        case HdriPreset::NoonSky:        return "Noon Sky";
        case HdriPreset::GoldenHour:     return "Golden Hour";
        case HdriPreset::Overcast:       return "Overcast";
        case HdriPreset::Night:          return "Night";
        case HdriPreset::InteriorWindow: return "Interior Window";
        case HdriPreset::Warehouse:      return "Warehouse";
        case HdriPreset::ForestClearing: return "Forest Clearing";
        case HdriPreset::Dusk:           return "Dusk";
        default: return "";
    }
}

const char* HdriPresetHint(HdriPreset p) {
    switch (p) {
        case HdriPreset::StudioSoftbox:  return "Three softboxes on neutral grey - product and turntable shots";
        case HdriPreset::NoonSky:        return "Hard midday sun, deep blue sky, bright ground bounce";
        case HdriPreset::GoldenHour:     return "Low warm sun with an orange sky and long shadows";
        case HdriPreset::Overcast:       return "Even white dome, no visible sun, very soft shadows";
        case HdriPreset::Night:          return "Deep blue dome with a small moon and stars";
        case HdriPreset::InteriorWindow: return "Dark room lit by one large window - strong directional falloff";
        case HdriPreset::Warehouse:      return "Grey interior with ceiling strip lights";
        case HdriPreset::ForestClearing: return "Green bounce from below, dappled light from above";
        case HdriPreset::Dusk:           return "Navy zenith, teal sky, a sunset glow on the horizon and the first stars";
        default: return "";
    }
}

void GenerateHdri(HdriPreset preset, HdriImage& out, int width) {
    width = std::clamp(width, 64, 4096);
    int height = width / 2;
    out.width = width;
    out.height = height;
    out.pixels.assign((size_t)width * height * 3, 0.0f);

    for (int y = 0; y < height; ++y) {
        float v = (y + 0.5f) / height;
        for (int x = 0; x < width; ++x) {
            float u = (x + 0.5f) / width;
            float dx, dy, dz;
            DirFor(u, v, dx, dy, dz);
            float up = std::clamp(dy * 0.5f + 0.5f, 0.0f, 1.0f);
            Col c{0, 0, 0};

            switch (preset) {
                case HdriPreset::StudioSoftbox: {
                    c = Mix(Col{0.05f, 0.05f, 0.06f}, Col{0.32f, 0.33f, 0.35f}, up);
                    // key, fill and rim, all above the horizon
                    c = c + Col{7.0f, 6.9f, 6.7f} * Panel(u, v, 0.08f, 0.26f, 0.16f, 0.40f, 0.03f);
                    c = c + Col{2.2f, 2.3f, 2.6f} * Panel(u, v, 0.55f, 0.70f, 0.22f, 0.44f, 0.04f);
                    c = c + Col{3.0f, 2.9f, 2.7f} * Panel(u, v, 0.80f, 0.92f, 0.10f, 0.30f, 0.03f);
                    break;
                }
                case HdriPreset::NoonSky: {
                    Col sky = Mix(Col{0.55f, 0.70f, 0.95f}, Col{0.12f, 0.28f, 0.75f}, up);
                    Col ground{0.30f, 0.28f, 0.25f};
                    c = dy > 0 ? sky : Mix(ground, sky, std::clamp(1.0f + dy * 4.0f, 0.0f, 1.0f));
                    c = c + Col{45.0f, 42.0f, 36.0f} * Disc(dx, dy, dz, 0.35f, 0.88f, 0.32f, 0.9995f, 0.0009f);
                    break;
                }
                case HdriPreset::GoldenHour: {
                    Col high{0.16f, 0.22f, 0.42f}, low{1.05f, 0.55f, 0.22f};
                    Col sky = Mix(low, high, std::pow(up, 0.7f));
                    Col ground{0.18f, 0.13f, 0.10f};
                    c = dy > 0 ? sky : Mix(ground, sky, std::clamp(1.0f + dy * 5.0f, 0.0f, 1.0f));
                    c = c + Col{30.0f, 14.0f, 5.0f} * Disc(dx, dy, dz, 0.86f, 0.10f, 0.50f, 0.9990f, 0.0016f);
                    break;
                }
                case HdriPreset::Overcast: {
                    Col sky = Mix(Col{0.42f, 0.44f, 0.47f}, Col{1.15f, 1.17f, 1.20f}, std::pow(up, 0.8f));
                    Col ground{0.22f, 0.22f, 0.23f};
                    c = dy > 0 ? sky : Mix(ground, sky, std::clamp(1.0f + dy * 3.0f, 0.0f, 1.0f));
                    break;
                }
                case HdriPreset::Night: {
                    c = Mix(Col{0.010f, 0.014f, 0.028f}, Col{0.020f, 0.030f, 0.075f}, up);
                    c = c + Col{6.0f, 6.0f, 5.6f} * Disc(dx, dy, dz, -0.45f, 0.72f, 0.52f, 0.99965f, 0.0004f);
                    // a sparse, deterministic star field
                    uint32_t h = (uint32_t)(x * 73856093u) ^ (uint32_t)(y * 19349663u);
                    h ^= h >> 13; h *= 1274126177u; h ^= h >> 16;
                    if (dy > 0.02f && (h & 1023u) == 0u) { float s = 0.6f + (h >> 10 & 15) * 0.25f; c = c + Col{s, s, s * 1.1f}; }
                    break;
                }
                case HdriPreset::InteriorWindow: {
                    c = Mix(Col{0.020f, 0.019f, 0.018f}, Col{0.075f, 0.073f, 0.070f}, up);
                    c = c + Col{9.0f, 9.3f, 10.5f} * Panel(u, v, 0.14f, 0.30f, 0.30f, 0.62f, 0.02f);
                    c = c + Col{0.35f, 0.33f, 0.30f} * Panel(u, v, 0.0f, 1.0f, 0.62f, 1.0f, 0.10f);
                    break;
                }
                case HdriPreset::Warehouse: {
                    c = Mix(Col{0.055f, 0.055f, 0.058f}, Col{0.22f, 0.22f, 0.23f}, up);
                    for (int lamp = 0; lamp < 4; ++lamp) {
                        float u0 = 0.06f + lamp * 0.24f;
                        c = c + Col{5.2f, 5.0f, 4.5f} * Panel(u, v, u0, u0 + 0.10f, 0.05f, 0.14f, 0.02f);
                    }
                    break;
                }
                case HdriPreset::ForestClearing: {
                    Col canopy{0.10f, 0.20f, 0.09f}, sky{0.60f, 0.78f, 0.95f};
                    c = Mix(Col{0.07f, 0.10f, 0.05f}, Mix(canopy, sky, std::pow(std::max(0.0f, dy), 1.5f)), up);
                    // gaps in the canopy, as a handful of soft bright discs overhead
                    static const float gaps[5][3] = {{0.2f, 0.9f, 0.3f}, {-0.5f, 0.8f, 0.3f}, {0.1f, 0.95f, -0.3f},
                                                     {-0.2f, 0.85f, -0.45f}, {0.6f, 0.75f, -0.2f}};
                    for (auto& g : gaps) {
                        float len = std::sqrt(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
                        c = c + Col{7.0f, 6.6f, 5.4f} * Disc(dx, dy, dz, g[0] / len, g[1] / len, g[2] / len, 0.994f, 0.006f);
                    }
                    break;
                }
                case HdriPreset::Dusk: {
                    // vertical: haze at the horizon -> teal -> navy, and a dark blue haze below
                    Col zenith{0.008f, 0.014f, 0.070f}, teal{0.025f, 0.150f, 0.190f}, haze{0.20f, 0.15f, 0.22f};
                    float h = std::max(dy, 0.0f);
                    Col sky = h < 0.09f ? Mix(haze, teal, std::pow(h / 0.09f, 0.8f))
                                        : Mix(teal, zenith, std::min(1.0f, std::pow((h - 0.09f) / 0.3f, 0.7f)));
                    c = dy >= 0 ? sky : Mix(haze, Col{0.035f, 0.045f, 0.10f}, std::clamp(-dy * 2.5f, 0.0f, 1.0f));
                    // the sunset glow, around +X and hugging the horizon: a hot core and a wide pink skirt
                    float hl = std::sqrt(dx * dx + dz * dz) + 1e-6f;
                    float toward = (dx / hl + 1.0f) * 0.5f;                     // 1 facing the glow, 0 opposite
                    float band = std::exp(-std::fabs(dy) * 14.0f);
                    float wide = std::exp(-std::fabs(dy) * 9.0f);
                    c = c + Col{2.4f, 0.85f, 0.30f} * (std::pow(toward, 14.0f) * band);
                    c = c + Col{0.40f, 0.14f, 0.20f} * (std::pow(toward, 6.0f) * wide);
                    // the sun itself, just above the horizon (the tracer's sun light does the actual lighting)
                    c = c + Col{4.0f, 1.9f, 0.8f} * Disc(dx, dy, dz, 0.99995f, 0.0100f, 0.0f, 0.99993f, 0.00015f);   // kept dim: a tiny hot disc only adds fireflies
                    // stars, only where the sky is already dark
                    uint32_t hs = (uint32_t)(x * 73856093u) ^ (uint32_t)(y * 19349663u);
                    hs ^= hs >> 13; hs *= 1274126177u; hs ^= hs >> 16;
                    if (dy > 0.2f && (hs & 511u) == 0u) {
                        float s = (0.4f + (hs >> 10 & 15) * 0.12f) * std::min(1.0f, (dy - 0.2f) * 5.0f);
                        c = c + Col{s, s, s * 1.1f};
                    }
                    break;
                }
                default: break;
            }

            size_t i = ((size_t)y * width + x) * 3;
            out.pixels[i] = std::max(0.0f, c.r);
            out.pixels[i + 1] = std::max(0.0f, c.g);
            out.pixels[i + 2] = std::max(0.0f, c.b);
        }
    }
}
