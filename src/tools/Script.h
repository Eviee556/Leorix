#pragma once
// Leorix Scripting: a small Lua environment for building meshes from parameters.
// Lua is chosen because it's tiny to embed, safe to sandbox, and easy for artists to pick up.
#include "mesh/Mesh.h"
#include <string>
#include <vector>

struct ScriptResult {
    bool                     ok = false;
    std::string              output;   // print() output and errors
    std::vector<Mesh>        meshes;   // meshes the script created
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
// Limits: ~100M Lua instructions and 512 MB of Lua memory per run; exceeding either fails the run.
ScriptResult RunMeshScript(const std::string& code);

// Merges every mesh one run created into a single mesh, keeping each mesh's own UVs
// (a mesh without UVs gets the usual fallback projection on its own).
Mesh MergeScriptMeshes(std::vector<Mesh> meshes);

const char* DefaultScriptExample();
