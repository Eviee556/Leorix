#pragma once
// Built-in environments, generated as equirectangular float images rather than shipped as files.
// They light the path tracer exactly like a loaded HDRI does.
#include "render/PathTracer.h"
#include <string>

enum class HdriPreset {
    StudioSoftbox, NoonSky, GoldenHour, Overcast, Night,
    InteriorWindow, Warehouse, ForestClearing, Count
};

const char* HdriPresetName(HdriPreset p);
const char* HdriPresetHint(HdriPreset p);
// Fills `out` with the preset at the given width (height is width / 2).
void GenerateHdri(HdriPreset preset, HdriImage& out, int width = 512);
