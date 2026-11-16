#pragma once
// Per-object rigid body settings and its baked simulation cache. Kept free of the solver so
// Scene.h can hold it without pulling the physics code in.
#include "mesh/Mesh.h"
#include <vector>

enum class RigidShape { Box, Sphere, ConvexHull };
const char* RigidShapeName(RigidShape s);

struct RigidTransform { Float3 position, rotation; };   // the object's position and euler XYZ

struct RigidBody {
    bool       enabled = false;
    bool       passive = false;    // collides, never moves (floors, walls)
    float      mass = 1.0f;        // kg
    float      friction = 0.5f;
    float      bounciness = 0.0f;  // 0 = no bounce, 1 = keeps all its speed
    RigidShape shape = RigidShape::ConvexHull;

    // Baked frames cacheStart .. cacheStart + cache.size() - 1. Playback reads these, so scrubbing
    // the timeline always shows the same result.
    int cacheStart = 1;
    std::vector<RigidTransform> cache;
    RigidTransform rest;           // the transform before the bake; Free Bake puts it back
    bool Baked() const { return !cache.empty(); }
};
