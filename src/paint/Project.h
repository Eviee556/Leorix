#pragma once
// The .leorix container, per ENGINE_SPEC v1.0: a ZIP holding
//   manifest.json  - canvas metadata, project settings, full layer hierarchy
//   layers/        - one image per layer per map, named L{ID}_{Map}.png
//   brushes/       - brush definitions
// Loading is tolerant: unknown fields are ignored so newer files still open.
#include "paint/Paint.h"
#include <string>

struct ProjectSaveInfo {
    bool ok = false;
    std::string message;
    size_t bytes = 0, layerFiles = 0;
};

ProjectSaveInfo SaveLeorixProject(const std::string& path, PaintProject& project,
                                  const BrushSettings& brush);
ProjectSaveInfo LoadLeorixProject(const std::string& path, PaintProject& project,
                                  BrushSettings& brush, VulkanContext& vk);
