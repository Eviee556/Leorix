#pragma once
// Rigid body simulation: a fixed-step impulse solver baked into each object's RigidBody cache.
#include "physics/RigidBody.h"

struct Scene;

struct PhysicsSettings {
    int    frameStart = 1, frameEnd = 120;
    int    fps = 24;
    Float3 gravity{0, -9.81f, 0};   // Y is up
};

// Simulates every enabled rigid body from its rest transform (the current transform, or the rest
// saved by an earlier bake) and fills the caches. Returns how many active (moving) bodies were baked.
int BakeRigidBodies(Scene& scene, const PhysicsSettings& s);
// Puts every baked object at its cached transform for `frame` (clamped to the baked range).
void ApplyRigidBodyCache(Scene& scene, float frame);
// Drops every cache and puts the objects back where they started.
void FreeRigidBodyBake(Scene& scene);
