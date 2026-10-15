#pragma once
#include "mesh/Mesh.h"
#include <string>
#include <functional>
#include <vector>

// Leorix Script: a tiny, safe, line-based language for generating meshes from parameters.
// No general-purpose evaluation (no eval/exec), so a script can only build geometry.
//
//   # comment
//   cube size=2
//   sphere radius=1 segments=32 rings=16
//   cylinder radius=0.5 depth=2 verts=24 caps=1
//   move 0 3 0          # translate what was built since the last 'begin'
//   rotate y 45         # degrees, axis x|y|z
//   scale 2 1 2
//   # manual construction:
//   vertex 0 0 0        # returns an index (auto-incrementing)
//   face 0 1 2 3        # face from vertex indices
//   # control:
//   repeat 6            # repeat the next block 'count' times; $i is the loop index (0..count-1)
//     cube size=0.5
//     move {$i*1.5} 0 0
//   end
struct ScriptResult {
    bool ok = false;
    std::string error;
    int line = 0;
    Mesh mesh;
    int meshesBuilt = 0;
};

ScriptResult RunLeorixScript(const std::string& source);
const char* LeorixScriptExample();
const char* LeorixScriptReference();
