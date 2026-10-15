#pragma once
// The 2D side, ported from Leorix's model: a project holds several maps (Base Color, Normal,
// Roughness, ...), each map owns a stack of layers, and layers composite with blend modes.
#include "gpu/Texture.h"
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

// Same set, labels and default fills as Leorix's KINDS table.
enum class MapKind { Albedo, Normal, Roughness, Metallic, AO, Cavity, Height, Emissive, Opacity, ORM, Custom, Count };
const char* MapKindLabel(MapKind k);
const char* MapKindHint(MapKind k);
uint32_t    MapKindDot(MapKind k);      // list colour
uint32_t    MapKindFill(MapKind k);     // default background fill (0 = transparent)

// Leorix's 16 canvas blend modes.
enum class Blend { Normal, Multiply, Screen, Overlay, Darken, Lighten, ColorDodge, ColorBurn,
                   HardLight, SoftLight, Difference, Exclusion, Hue, Saturation, Color, Luminosity, Count };
const char* BlendName(Blend b);

struct Layer {
    std::string name = "Layer";
    bool    visible = true;
    bool    locked = false;
    float   opacity = 100.0f;
    Blend   blend = Blend::Normal;
    std::vector<uint8_t> pixels;      // RGBA8, width*height*4
    void Fill(uint32_t rgba, int w, int h);
};

struct DirtyRect {
    int x0 = 0, y0 = 0, x1 = -1, y1 = -1;
    bool Empty() const { return x1 < x0 || y1 < y0; }
    void Reset() { x0 = y0 = 0; x1 = y1 = -1; }
    void Add(int x, int y, int r, int w, int h);
    void Whole(int w, int h) { x0 = 0; y0 = 0; x1 = w - 1; y1 = h - 1; }
};

struct MapDoc {
    std::string name;
    MapKind kind = MapKind::Albedo;
    int width = 2048, height = 2048;
    std::vector<Layer> layers;
    int activeLayer = 0;
    std::vector<uint8_t> composite;   // RGBA8 result of the stack
    GpuTexture texture;               // composite on the GPU for display and for the 3D material
    DirtyRect dirty;
    bool textureDirty = true;

    Layer* Active() { return (activeLayer >= 0 && activeLayer < (int)layers.size()) ? &layers[activeLayer] : nullptr; }
    void Composite();                 // recomposite the dirty region
    void AddLayer(const std::string& name);
    void DuplicateLayer();
    void DeleteLayer();
    void MoveLayer(int from, int to);
};

// Brush settings, matching Leorix's State.brush / pencil / eraser.
struct BrushSettings {
    float size = 28.0f, opacity = 100.0f, hardness = 70.0f;
    float flow = 100.0f;
    float spacing = 0.12f;            // fraction of the radius between stamps
};

struct PaintProject {
    std::vector<MapDoc> maps;
    int   activeMap = 0;
    int   width = 2048, height = 2048;
    std::string name = "untitled";

    MapDoc* Active() { return (activeMap >= 0 && activeMap < (int)maps.size()) ? &maps[activeMap] : nullptr; }
    MapDoc* AddMap(MapKind kind, const std::string& name = "");
    void    RemoveMap(int index, VulkanContext& vk);
    void    New(int w, int h, MapKind first = MapKind::Albedo, uint32_t background = 0xFFFFFFFFu);
};

// Undo for painting. Copying a whole 2048x2048 layer per stroke would cost 16 MB, so only the
// 128x128 tiles a stroke actually touches are kept, before and after.
struct StrokeUndo {
    static const int kTile = 128;
    int mapIndex = -1, layerIndex = -1, width = 0, height = 0;
    std::vector<int> tiles;                       // tile ids, in touch order
    std::vector<std::vector<uint8_t>> before, after;

    void Begin(int map, int layer, int w, int h);
    void BackupRect(const Layer& l, int x0, int y0, int x1, int y1);
    void CaptureAfter(const Layer& l);
    void Restore(Layer& l, bool useBefore) const;
    size_t Bytes() const;
    bool Empty() const { return tiles.empty(); }
    void TileRect(int tile, int& x0, int& y0, int& x1, int& y1) const;
};

// Paints one dab of the brush into a layer. Colour is 0xAABBGGRR to match ImGui's packing.
void StampBrush(Layer& layer, int w, int h, float cx, float cy, const BrushSettings& b,
                uint32_t rgba, bool erase, DirtyRect& dirty);
// Draws a spaced line of dabs between two points, which is how a stroke is built.
void StrokeSegment(Layer& layer, int w, int h, float x0, float y0, float x1, float y1,
                   const BrushSettings& b, uint32_t rgba, bool erase, DirtyRect& dirty, float& carry);
// Flood-fill style bucket over the whole layer (tolerance-free, fills the layer rect).
void FillLayer(Layer& layer, int w, int h, uint32_t rgba, DirtyRect& dirty);
