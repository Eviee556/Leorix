#pragma once
// UV unwrapping. The mesh is cut into islands along marked seams, each island is flattened with
// LSCM (least squares conformal maps - the same family Blender's Unwrap uses), then the islands
// are packed into the 0..1 square.
#include "mesh/Mesh.h"
#include <string>

struct UnwrapResult {
    bool ok = false;
    size_t islands = 0;
    std::string message;
};

struct UnwrapOptions {
    float margin = 0.02f;          // gap between islands, in UV units
    bool  packIslands = true;
    bool  averageScale = true;     // keep islands' relative real-world scale
};

// Writes per-face-corner UVs into mesh.loopUVs.
UnwrapResult UnwrapMesh(Mesh& mesh, const UnwrapOptions& opts = UnwrapOptions{});
