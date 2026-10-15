#pragma once
#include "mesh/Mesh.h"

// Clean-topology primitives built to match Blender's default meshes exactly:
// same construction, same default sizes (2 m cube, radius-1 sphere, etc), same face order,
// quads wherever Blender uses quads (tracked as polygons), NO subdivision surface applied.
enum class Primitive { Plane, Cube, Circle, UVSphere, IcoSphere, Cylinder, Cone, Torus, Grid, Monkey };

struct PrimitiveParams {
    // Shared
    int   verticesRing = 32;    // Circle/Cylinder/Cone vertices, UV sphere segments
    int   rings = 16;           // UV sphere rings
    int   subdivisions = 2;     // Ico sphere
    int   gridX = 10, gridY = 10;
    float radius = 1.0f;        // Blender's default sphere/circle radius
    float depth = 2.0f;         // Cylinder/Cone height (Blender default 2 m)
    float radius2 = 0.25f;      // Torus minor radius (Blender minor 0.25, major 1.0)
    bool  capFill = true;       // Cylinder/Cone/Circle n-gon caps
    float size = 2.0f;          // Cube/Plane/Grid side (Blender default 2 m)
};

Mesh BuildPrimitive(Primitive type, const PrimitiveParams& p);
// Blender's Add Mesh defaults for one primitive type.
PrimitiveParams DefaultParams(Primitive type);
const char* PrimitiveName(Primitive type);
