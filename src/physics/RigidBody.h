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

// Per-object cloth settings and its baked vertex cache. Colliders are the objects with a rigid body.
struct Cloth {
    bool  enabled = false;
    float mass = 0.3f;        // kg for the whole cloth
    float stretch = 1.0f;     // 0 = rubbery, 1 = does not stretch
    float bend = 0.1f;        // 0 = folds freely, 1 = stiff like card
    float damping = 0.5f;     // share of speed lost per second
    float friction = 0.5f;
    float thickness = 0.02f;  // m kept between the cloth and colliders
    std::vector<uint32_t> pins;   // vertices held where they are

    // Baked frames cacheStart .. cacheStart + cache.size() - 1, object-space positions per vertex.
    int cacheStart = 1;
    std::vector<std::vector<Float3>> cache;
    std::vector<Float3> rest;     // mesh positions before the bake; Free Bake puts them back
    int shown = -1;               // cache entry last written to the mesh
    bool Baked() const { return !cache.empty(); }
};
