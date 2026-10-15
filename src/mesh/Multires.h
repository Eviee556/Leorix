#pragma once
// Multiresolution: the base mesh is subdivided to a level, and sculpted detail is stored as a
// displacement per vertex of that level rather than baked into the mesh. Changing the base or
// the level keeps the detail, which is the whole point of multires.
#include "mesh/Mesh.h"
#include <vector>

struct Multires {
    int  levels = 0;                    // how many subdivisions the sculpt level has
    int  viewLevel = 0;                 // what the viewport shows (<= levels)
    std::vector<Float3> displacement;   // one per vertex of the sculpt level
    Mesh cage;                          // the subdivided mesh before displacement
    bool valid = false;

    void Rebuild(const Mesh& base, int newLevels);   // (re)subdivide and keep what detail fits
    Mesh Evaluate() const;                           // cage + displacement
    void Clear() { levels = viewLevel = 0; displacement.clear(); cage.Clear(); valid = false; }
    size_t VertexCount() const { return cage.positions.size(); }
};
