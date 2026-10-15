#include "paint/Paint.h"
#include <algorithm>
#include <cmath>
#include <cstring>

// ---------------------------------------------------------------- tables (from Leorix's KINDS)
struct KindInfo { const char* label; const char* hint; uint32_t dot; uint32_t fill; };
static const KindInfo kKinds[] = {
    {"Base Color",        "Base colour - sRGB",              0xFF4B8AC9, 0xFFFFFFFF},
    {"Normal",            "Tangent-space normal - linear",   0xFFCE8F6F, 0xFFFF8080},
    {"Roughness",         "Grayscale - linear",              0xFFABA09A, 0xFF808080},
    {"Metallic",          "Grayscale - linear",              0xFFC9C4B9, 0xFF000000},
    {"Ambient Occlusion", "Grayscale - linear",              0xFF375F8A, 0xFFFFFFFF},
    {"Cavity",            "Grayscale - linear",              0xFF8A7A5C, 0xFFFFFFFF},
    {"Height",            "Displacement - linear",           0xFF8D9A6F, 0xFF808080},
    {"Emissive",          "Emission - sRGB",                 0xFF4B61C9, 0xFF000000},
    {"Opacity",           "Alpha - linear",                  0xFFD8E3E8, 0xFFFFFFFF},
    {"ORM (Packed)",      "AO / Roughness / Metallic",       0xFF0080FF, 0xFF0080FF},
    {"Custom",            "Custom map",                      0xFF685B55, 0x00000000},
};
const char* MapKindLabel(MapKind k) { return kKinds[std::clamp((int)k, 0, 10)].label; }
const char* MapKindHint(MapKind k)  { return kKinds[std::clamp((int)k, 0, 10)].hint; }
uint32_t MapKindDot(MapKind k)      { return kKinds[std::clamp((int)k, 0, 10)].dot; }
uint32_t MapKindFill(MapKind k)     { return kKinds[std::clamp((int)k, 0, 10)].fill; }

static const char* kBlendNames[] = {"Normal", "Multiply", "Screen", "Overlay", "Darken", "Lighten",
    "Color Dodge", "Color Burn", "Hard Light", "Soft Light", "Difference", "Exclusion",
    "Hue", "Saturation", "Color", "Luminosity"};
const char* BlendName(Blend b) { return kBlendNames[std::clamp((int)b, 0, 15)]; }

// ---------------------------------------------------------------- blending
static inline float Lum(float r, float g, float b) { return 0.3f * r + 0.59f * g + 0.11f * b; }
static void ClipColor(float& r, float& g, float& b) {
    float l = Lum(r, g, b);
    float mn = std::min({r, g, b}), mx = std::max({r, g, b});
    if (mn < 0) { r = l + (r - l) * l / (l - mn + 1e-6f); g = l + (g - l) * l / (l - mn + 1e-6f); b = l + (b - l) * l / (l - mn + 1e-6f); }
    if (mx > 1) { r = l + (r - l) * (1 - l) / (mx - l + 1e-6f); g = l + (g - l) * (1 - l) / (mx - l + 1e-6f); b = l + (b - l) * (1 - l) / (mx - l + 1e-6f); }
}
static void SetLum(float& r, float& g, float& b, float l) {
    float d = l - Lum(r, g, b); r += d; g += d; b += d; ClipColor(r, g, b);
}
static float Sat(float r, float g, float b) { return std::max({r, g, b}) - std::min({r, g, b}); }
static void SetSat(float& r, float& g, float& b, float s) {
    float* mx = &r; float* md = &g; float* mn = &b;
    // order the three channels
    float* v[3] = {&r, &g, &b};
    std::sort(v, v + 3, [](float* a, float* b2) { return *a < *b2; });
    mn = v[0]; md = v[1]; mx = v[2];
    if (*mx > *mn) { *md = (*md - *mn) * s / (*mx - *mn); *mx = s; } else { *md = *mx = 0; }
    *mn = 0;
}

static float BlendChannel(Blend mode, float b, float s) {   // b = backdrop, s = source
    switch (mode) {
        case Blend::Multiply:   return b * s;
        case Blend::Screen:     return b + s - b * s;
        case Blend::Overlay:    return b <= 0.5f ? 2 * b * s : 1 - 2 * (1 - b) * (1 - s);
        case Blend::Darken:     return std::min(b, s);
        case Blend::Lighten:    return std::max(b, s);
        case Blend::ColorDodge: return s >= 1 ? 1 : std::min(1.0f, b / (1 - s));
        case Blend::ColorBurn:  return s <= 0 ? 0 : 1 - std::min(1.0f, (1 - b) / s);
        case Blend::HardLight:  return s <= 0.5f ? 2 * s * b : 1 - 2 * (1 - s) * (1 - b);
        case Blend::SoftLight: {
            if (s <= 0.5f) return b - (1 - 2 * s) * b * (1 - b);
            float d = b <= 0.25f ? ((16 * b - 12) * b + 4) * b : std::sqrt(b);
            return b + (2 * s - 1) * (d - b);
        }
        case Blend::Difference: return std::fabs(b - s);
        case Blend::Exclusion:  return b + s - 2 * b * s;
        default:                return s;
    }
}

// ---------------------------------------------------------------- Layer / DirtyRect
void Layer::Fill(uint32_t rgba, int w, int h) {
    pixels.assign((size_t)w * h * 4, 0);
    if ((rgba & 0xFF000000u) == 0) return;          // transparent
    uint8_t r = rgba & 0xFF, g = (rgba >> 8) & 0xFF, b = (rgba >> 16) & 0xFF, a = (rgba >> 24) & 0xFF;
    for (size_t i = 0; i < pixels.size(); i += 4) { pixels[i] = r; pixels[i+1] = g; pixels[i+2] = b; pixels[i+3] = a; }
}

void DirtyRect::Add(int x, int y, int r, int w, int h) {
    int nx0 = std::max(0, x - r), ny0 = std::max(0, y - r);
    int nx1 = std::min(w - 1, x + r), ny1 = std::min(h - 1, y + r);
    if (Empty()) { x0 = nx0; y0 = ny0; x1 = nx1; y1 = ny1; return; }
    x0 = std::min(x0, nx0); y0 = std::min(y0, ny0);
    x1 = std::max(x1, nx1); y1 = std::max(y1, ny1);
}

// ---------------------------------------------------------------- MapDoc
void MapDoc::AddLayer(const std::string& n) {
    Layer l; l.name = n;
    l.Fill(0, width, height);
    layers.insert(layers.begin() + std::clamp(activeLayer + 1, 0, (int)layers.size()), l);
    activeLayer = std::clamp(activeLayer + 1, 0, (int)layers.size() - 1);
    dirty.Whole(width, height);
}
void MapDoc::DuplicateLayer() {
    Layer* src = Active();
    if (!src) return;
    Layer copy = *src;
    copy.name = src->name + " copy";
    layers.insert(layers.begin() + activeLayer + 1, copy);
    ++activeLayer;
    dirty.Whole(width, height);
}
void MapDoc::DeleteLayer() {
    if (layers.size() <= 1) return;
    layers.erase(layers.begin() + activeLayer);
    activeLayer = std::clamp(activeLayer, 0, (int)layers.size() - 1);
    dirty.Whole(width, height);
}
void MapDoc::MoveLayer(int from, int to) {
    if (from < 0 || to < 0 || from >= (int)layers.size() || to >= (int)layers.size() || from == to) return;
    Layer l = layers[from];
    layers.erase(layers.begin() + from);
    layers.insert(layers.begin() + to, l);
    activeLayer = to;
    dirty.Whole(width, height);
}

// Reoriented Normal Mapping. Alpha-blending two normal maps flattens the detail, so vector maps
// are combined by reorienting the detail normal into the base normal's frame (ENGINE_SPEC 2).
static void BlendNormalRNM(float br, float bg, float bb, float sr, float sg, float sb,
                           float& outR, float& outG, float& outB) {
    // to [-1,1], with the base pushed into +z
    float tx = br * 2.0f - 1.0f, ty = bg * 2.0f - 1.0f, tz = bb * 2.0f;          // t = base * 2 - (1,1,0)
    float ux = -(sr * 2.0f - 1.0f), uy = -(sg * 2.0f - 1.0f), uz = sb * 2.0f - 1.0f; // u = detail * (-1,-1,1)
    float d = tx * ux + ty * uy + tz * uz;
    float rx = tx * d - ux * tz, ry = ty * d - uy * tz, rz = tz * d - uz * tz;
    float len = std::sqrt(rx * rx + ry * ry + rz * rz);
    if (len < 1e-6f) { outR = br; outG = bg; outB = bb; return; }
    outR = (rx / len) * 0.5f + 0.5f;
    outG = (ry / len) * 0.5f + 0.5f;
    outB = (rz / len) * 0.5f + 0.5f;
}

void MapDoc::Composite() {
    if (composite.size() != (size_t)width * height * 4) { composite.assign((size_t)width * height * 4, 0); dirty.Whole(width, height); }
    if (dirty.Empty()) return;
    const int x0 = std::max(0, dirty.x0), y0 = std::max(0, dirty.y0);
    const int x1 = std::min(width - 1, dirty.x1), y1 = std::min(height - 1, dirty.y1);

    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            size_t i = ((size_t)y * width + x) * 4;
            float br = 0, bg = 0, bb = 0, ba = 0;           // backdrop, premultiplied-ish
            for (const Layer& l : layers) {
                if (!l.visible || l.opacity <= 0.0f || l.pixels.size() != composite.size()) continue;
                float sa = l.pixels[i + 3] / 255.0f * (l.opacity / 100.0f);
                if (sa <= 0) continue;
                float sr = l.pixels[i] / 255.0f, sg = l.pixels[i + 1] / 255.0f, sb = l.pixels[i + 2] / 255.0f;
                float mr = sr, mg = sg, mb = sb;
                if (l.blend >= Blend::Hue) {                 // the four HSL modes
                    float r = sr, g = sg, b = sb;
                    switch (l.blend) {
                        case Blend::Hue:        SetSat(r, g, b, Sat(br, bg, bb)); SetLum(r, g, b, Lum(br, bg, bb)); break;
                        case Blend::Saturation: r = br; g = bg; b = bb; SetSat(r, g, b, Sat(sr, sg, sb)); SetLum(r, g, b, Lum(br, bg, bb)); break;
                        case Blend::Color:      SetLum(r, g, b, Lum(br, bg, bb)); break;
                        default:                r = br; g = bg; b = bb; SetLum(r, g, b, Lum(sr, sg, sb)); break;  // Luminosity
                    }
                    mr = r; mg = g; mb = b;
                } else if (kind == MapKind::Normal && l.blend == Blend::Normal && ba > 0.001f) {
                    BlendNormalRNM(br, bg, bb, sr, sg, sb, mr, mg, mb);
                } else if (l.blend != Blend::Normal) {
                    mr = BlendChannel(l.blend, br, sr);
                    mg = BlendChannel(l.blend, bg, sg);
                    mb = BlendChannel(l.blend, bb, sb);
                }
                // mix blended colour by backdrop alpha, then source-over
                mr = sr * (1 - ba) + mr * ba;
                mg = sg * (1 - ba) + mg * ba;
                mb = sb * (1 - ba) + mb * ba;
                br = mr * sa + br * (1 - sa);
                bg = mg * sa + bg * (1 - sa);
                bb = mb * sa + bb * (1 - sa);
                ba = sa + ba * (1 - sa);
            }
            composite[i]     = (uint8_t)std::clamp(br * 255.0f, 0.0f, 255.0f);
            composite[i + 1] = (uint8_t)std::clamp(bg * 255.0f, 0.0f, 255.0f);
            composite[i + 2] = (uint8_t)std::clamp(bb * 255.0f, 0.0f, 255.0f);
            composite[i + 3] = (uint8_t)std::clamp(ba * 255.0f, 0.0f, 255.0f);
        }
    }
    textureDirty = true;
    dirty.Reset();
}

// ---------------------------------------------------------------- project
MapDoc* PaintProject::AddMap(MapKind kind, const std::string& name) {
    MapDoc m;
    m.kind = kind;
    m.name = name.empty() ? MapKindLabel(kind) : name;
    m.width = width; m.height = height;
    Layer bg; bg.name = "Background";
    bg.Fill(MapKindFill(kind), width, height);
    m.layers.push_back(bg);
    m.activeLayer = 0;
    m.dirty.Whole(width, height);
    maps.push_back(std::move(m));
    activeMap = (int)maps.size() - 1;
    return &maps.back();
}

void PaintProject::RemoveMap(int index, VulkanContext& vk) {
    if (index < 0 || index >= (int)maps.size()) return;
    maps[index].texture.Destroy(vk);
    maps.erase(maps.begin() + index);
    activeMap = std::clamp(activeMap, 0, (int)maps.size() - 1);
}

void PaintProject::New(int w, int h, MapKind first, uint32_t background) {
    maps.clear();
    width = w; height = h;
    MapDoc* m = AddMap(first);
    if (m && !m->layers.empty()) m->layers[0].Fill(background, w, h);
    activeMap = 0;
}

// ---------------------------------------------------------------- brush
void StampBrush(Layer& layer, int w, int h, float cx, float cy, const BrushSettings& b,
                uint32_t rgba, bool erase, DirtyRect& dirty) {
    if (layer.locked || layer.pixels.size() != (size_t)w * h * 4) return;
    float radius = std::max(0.5f, b.size * 0.5f);
    int x0 = std::max(0, (int)std::floor(cx - radius)), x1 = std::min(w - 1, (int)std::ceil(cx + radius));
    int y0 = std::max(0, (int)std::floor(cy - radius)), y1 = std::min(h - 1, (int)std::ceil(cy + radius));
    if (x0 > x1 || y0 > y1) return;

    const float hard = std::clamp(b.hardness / 100.0f, 0.0f, 1.0f);
    const float alpha = std::clamp(b.opacity / 100.0f, 0.0f, 1.0f) * std::clamp(b.flow / 100.0f, 0.0f, 1.0f);
    const uint8_t cr = rgba & 0xFF, cg = (rgba >> 8) & 0xFF, cb = (rgba >> 16) & 0xFF;

    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            float d = std::sqrt(dx * dx + dy * dy) / radius;
            if (d > 1.0f) continue;
            // hardness: flat core, smooth falloff to the rim
            float fall = d <= hard ? 1.0f : 1.0f - (d - hard) / std::max(1e-4f, 1.0f - hard);
            float a = alpha * fall * fall * (3.0f - 2.0f * fall) / std::max(0.001f, 1.0f);
            a = std::clamp(alpha * (fall * fall * (3.0f - 2.0f * fall)), 0.0f, 1.0f);
            if (a <= 0.001f) continue;
            size_t i = ((size_t)y * w + x) * 4;
            if (erase) {
                float dstA = layer.pixels[i + 3] / 255.0f;
                layer.pixels[i + 3] = (uint8_t)std::clamp(dstA * (1 - a) * 255.0f, 0.0f, 255.0f);
            } else {
                float dstA = layer.pixels[i + 3] / 255.0f;
                float outA = a + dstA * (1 - a);
                if (outA <= 0.0001f) continue;
                for (int c = 0; c < 3; ++c) {
                    float src = (c == 0 ? cr : c == 1 ? cg : cb) / 255.0f;
                    float dst = layer.pixels[i + c] / 255.0f;
                    float out = (src * a + dst * dstA * (1 - a)) / outA;
                    layer.pixels[i + c] = (uint8_t)std::clamp(out * 255.0f, 0.0f, 255.0f);
                }
                layer.pixels[i + 3] = (uint8_t)std::clamp(outA * 255.0f, 0.0f, 255.0f);
            }
        }
    }
    dirty.Add((int)cx, (int)cy, (int)std::ceil(radius) + 1, w, h);
}

void StrokeSegment(Layer& layer, int w, int h, float x0, float y0, float x1, float y1,
                   const BrushSettings& b, uint32_t rgba, bool erase, DirtyRect& dirty, float& carry) {
    float dx = x1 - x0, dy = y1 - y0;
    float len = std::sqrt(dx * dx + dy * dy);
    float step = std::max(1.0f, b.size * 0.5f * std::max(0.02f, b.spacing));
    if (len < 1e-4f) { StampBrush(layer, w, h, x1, y1, b, rgba, erase, dirty); return; }
    float t = carry;
    while (t <= len) {
        float f = t / len;
        StampBrush(layer, w, h, x0 + dx * f, y0 + dy * f, b, rgba, erase, dirty);
        t += step;
    }
    carry = t - len;
}

void FillLayer(Layer& layer, int w, int h, uint32_t rgba, DirtyRect& dirty) {
    if (layer.locked) return;
    layer.Fill(rgba, w, h);
    dirty.Whole(w, h);
}

// ---------------------------------------------------------------- stroke undo (tile backups)
void StrokeUndo::Begin(int map, int layer, int w, int h) {
    mapIndex = map; layerIndex = layer; width = w; height = h;
    tiles.clear(); before.clear(); after.clear();
}

void StrokeUndo::TileRect(int tile, int& x0, int& y0, int& x1, int& y1) const {
    int cols = (width + kTile - 1) / kTile;
    int tx = tile % cols, ty = tile / cols;
    x0 = tx * kTile; y0 = ty * kTile;
    x1 = std::min(width - 1, x0 + kTile - 1);
    y1 = std::min(height - 1, y0 + kTile - 1);
}

static void CopyTile(const Layer& l, int w, int x0, int y0, int x1, int y1, std::vector<uint8_t>& out) {
    out.resize((size_t)(x1 - x0 + 1) * (y1 - y0 + 1) * 4);
    size_t o = 0;
    for (int y = y0; y <= y1; ++y) {
        const uint8_t* row = &l.pixels[((size_t)y * w + x0) * 4];
        std::memcpy(&out[o], row, (size_t)(x1 - x0 + 1) * 4);
        o += (size_t)(x1 - x0 + 1) * 4;
    }
}

void StrokeUndo::BackupRect(const Layer& l, int rx0, int ry0, int rx1, int ry1) {
    if (l.pixels.size() != (size_t)width * height * 4) return;
    int cols = (width + kTile - 1) / kTile;
    int t0x = std::max(0, rx0) / kTile, t1x = std::min(width - 1, rx1) / kTile;
    int t0y = std::max(0, ry0) / kTile, t1y = std::min(height - 1, ry1) / kTile;
    for (int ty = t0y; ty <= t1y; ++ty) {
        for (int tx = t0x; tx <= t1x; ++tx) {
            int id = ty * cols + tx;
            if (std::find(tiles.begin(), tiles.end(), id) != tiles.end()) continue;  // already saved
            int x0, y0, x1, y1;
            tiles.push_back(id);
            TileRect(id, x0, y0, x1, y1);
            before.emplace_back();
            CopyTile(l, width, x0, y0, x1, y1, before.back());
        }
    }
}

void StrokeUndo::CaptureAfter(const Layer& l) {
    after.clear();
    after.reserve(tiles.size());
    for (int id : tiles) {
        int x0, y0, x1, y1;
        TileRect(id, x0, y0, x1, y1);
        after.emplace_back();
        CopyTile(l, width, x0, y0, x1, y1, after.back());
    }
}

void StrokeUndo::Restore(Layer& l, bool useBefore) const {
    const std::vector<std::vector<uint8_t>>& src = useBefore ? before : after;
    if (src.size() != tiles.size() || l.pixels.size() != (size_t)width * height * 4) return;
    for (size_t t = 0; t < tiles.size(); ++t) {
        int x0, y0, x1, y1;
        TileRect(tiles[t], x0, y0, x1, y1);
        size_t o = 0;
        for (int y = y0; y <= y1; ++y) {
            std::memcpy(&l.pixels[((size_t)y * width + x0) * 4], &src[t][o], (size_t)(x1 - x0 + 1) * 4);
            o += (size_t)(x1 - x0 + 1) * 4;
        }
    }
}

size_t StrokeUndo::Bytes() const {
    size_t n = 0;
    for (const auto& b : before) n += b.size();
    for (const auto& a : after) n += a.size();
    return n;
}
