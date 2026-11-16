#pragma once
// UV unwrapping. The mesh is cut into islands along marked seams, each island is flattened with
// LSCM (least squares conformal maps - the same family Blender's Unwrap uses), then the islands
// are packed into the 0..1 square.
#include "mesh/Mesh.h"
#include <cstdint>
#include <string>
#include <unordered_set>

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

// Smart UV Project: no seams needed. Neighbouring faces whose normals stay within angleLimit
// degrees of the group's first face are flattened together; every group becomes its own island,
// then the islands are packed like Unwrap. The mesh's marked seams are left untouched.
UnwrapResult SmartUVProject(Mesh& mesh, float angleLimitDeg = 66.0f, const UnwrapOptions& opts = UnwrapOptions{});

// Marks or clears seams on `edges`; with liveUnwrap the mesh is unwrapped again straight away
// (Live Unwrap), otherwise the result is not ok and the UVs are left alone.
UnwrapResult MarkSeams(Mesh& mesh, const std::unordered_set<uint64_t>& edges, bool set, bool liveUnwrap);
