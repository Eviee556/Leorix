#pragma once
// UV layout for the 2D canvas: the mesh's UV wireframe plus its seams.
// Seam rule taken from Leorix: a 3D edge is a seam when its two sides land in different places
// in UV space, or when only one face uses it (an open border).
#include "mesh/Mesh.h"
#include <vector>

struct UVEdge { Float2 a, b; };

struct UVLayout {
    std::vector<UVEdge> wire;    // every UV edge of every polygon
    std::vector<UVEdge> seams;   // seams and open borders
    size_t islands = 0;          // connected groups in UV space
    bool   valid = false;

    void Build(const Mesh& mesh);
    void Clear() { wire.clear(); seams.clear(); islands = 0; valid = false; }
};
