#pragma once
// Leorix Scripting: a small Lua environment for building meshes from parameters.
// Lua is chosen because it's tiny to embed, safe to sandbox, and easy for artists to pick up.
#include "mesh/Mesh.h"
#include <string>
#include <vector>

// Look settings a scene script can ask for. The Scripting panel builds geometry only; a headless
// render (leorix --headless-render scene.lua out.png) turns each mesh into its own object with
// these materials, camera and environment.
struct ScriptMaterial {
    float color[3] = {0.78f, 0.76f, 0.72f};
    float roughness = 0.5f, metallic = 0.0f;
    float emission[3] = {0, 0, 0};
    float strength = 1.0f;
    float transmission = 0.0f, ior = 1.45f;
    bool  smooth = false;
};
struct ScriptView {
    bool  hasCamera = false;
    Float3 eye{6, 4, 6}, target{0, 1, 0};
    float lens = 35.0f;                 // mm, full-frame sensor
    float fStop = 0.0f, focus = 0.0f;   // fStop > 0 turns depth of field on
    int   hdriPreset = -1;              // HdriPreset index, -1 = none (sky colour + sun)
    float hdriStrength = 1.0f, hdriRotation = 0.0f;   // rotation in degrees
    float sunElevation = 35.0f, sunAzimuth = 30.0f, sunStrength = 3.0f;
    float sunColor[3] = {1.0f, 0.96f, 0.9f};
    float skyColor[3] = {0.16f, 0.18f, 0.22f};
    float horizonColor[3] = {0.07f, 0.08f, 0.10f};   // the gradient sky fades to this below the horizon
    float skyStrength = 1.0f;
    float exposure = 1.0f;
};

struct ScriptResult {
    bool                        ok = false;
    std::string                 output;      // print() output and errors
    std::vector<Mesh>           meshes;      // meshes the script created
    std::vector<ScriptMaterial> materials;   // one per mesh: the material() active when it was made
    ScriptView                  view;        // camera{} and world{} settings
};

// Runs one script. The API exposed to scripts:
//   -- primitives (clean topology, Blender defaults)
//   cube(size)                       sphere(radius, segments, rings)     ico_sphere(radius, subdivisions)
//   cylinder(radius, depth, verts)   cone(radius, depth, verts)          torus(major, minor, majseg, minseg)
//   plane(size)                      circle(radius, verts)               grid(x, y, size)
//   -- manual mesh building
//   m = mesh()                       v = m:vert(x, y, z)                 m:face(a, b, c[, d, ...])
//   m:build()                        -- adds it to the scene
//   -- helpers: print(...), PI, math.*
//   -- timeline: frame (current frame, starts at 1), fps, time (seconds since frame 1)
//   -- look (used by --headless-render; the panel builds geometry only)
//   material{color={r,g,b}, roughness=, metallic=, emission={r,g,b}, strength=, glass=ior, smooth=true}
//   curve({{x,y,z}, ...}, {radius=, cyclic=, nurbs=, sides=, taper={...}})   -- a swept tube
//   camera{eye={x,y,z}, target={x,y,z}, lens=mm, fstop=, focus=}
//   world{preset="GoldenHour", strength=, rotation=deg, sun_elevation=, sun_azimuth=, sun_strength=,
//         sun_color={r,g,b}, sky_color={r,g,b}, horizon_color={r,g,b}, sky_strength=, exposure=}
// Limits: ~100M Lua instructions and 512 MB of Lua memory per run; exceeding either fails the run.
ScriptResult RunMeshScript(const std::string& code, float frame = 1.0f, int fps = 24);

// Merges every mesh one run created into a single mesh, keeping each mesh's own UVs
// (a mesh without UVs gets the usual fallback projection on its own).
Mesh MergeScriptMeshes(std::vector<Mesh> meshes);

const char* DefaultScriptExample();
