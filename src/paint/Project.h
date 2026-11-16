#pragma once
// The .leorix container, per ENGINE_SPEC v1.0: a ZIP holding
//   manifest.json  - canvas metadata, project settings, full layer hierarchy
//   layers/        - one image per layer per map, named L{ID}_{Map}.png
//   brushes/       - brush definitions
// Loading is tolerant: unknown fields are ignored so newer files still open.
#include "paint/Paint.h"
#include <string>

struct Scene;
struct PlaybackState;

struct ProjectSaveInfo {
    bool ok = false;
    std::string message;
    size_t bytes = 0, layerFiles = 0;
};

// `scene`/`playback`, when given, are saved as the "scene" section (objects, meshes, materials,
// modifiers, armatures, keyframes, cameras). Loading replaces the scene only when the file has one,
// so projects saved before the scene was stored still open without touching it.
// `blocks` is an optional Visual Scripting graph (SaveGraph text), stored as blocks.json.
ProjectSaveInfo SaveLeorixProject(const std::string& path, PaintProject& project,
                                  const BrushSettings& brush, const std::string& blocks = {},
                                  const Scene* scene = nullptr, const PlaybackState* playback = nullptr);
ProjectSaveInfo LoadLeorixProject(const std::string& path, PaintProject& project,
                                  BrushSettings& brush, VulkanContext& vk, std::string* blocks = nullptr,
                                  Scene* scene = nullptr, PlaybackState* playback = nullptr);
