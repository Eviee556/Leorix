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
constexpr int kMaxBakeFrames = 10000;   // longer bakes would freeze the UI thread
// Puts every baked object at its cached transform for `frame` (clamped to the baked range).
void ApplyRigidBodyCache(Scene& scene, float frame);
// Drops every cache and puts the objects back where they started.
void FreeRigidBodyBake(Scene& scene);

// Simulates every enabled cloth (XPBD edge and bending constraints, pins, gravity, damping),
// colliding with every object that has a rigid body and following its rigid bake when there is
// one, so bake rigid bodies first. Returns how many cloths were baked. ApplyRigidBodyCache and
// FreeRigidBodyBake also play back and free cloth caches.
int BakeCloth(Scene& scene, const PhysicsSettings& s);
struct MeshObject;
// Drops one object's cloth bake: keepShape leaves the mesh as it is now (Apply as Shape),
// otherwise the rest shape comes back.
void DropClothBake(MeshObject& o, bool keepShape);
// Pins the vertices in the top 2% of the object's world height. Returns how many.
int PinClothTop(MeshObject& o);
