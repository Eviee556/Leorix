#pragma once
#include "mesh/Mesh.h"
#include <string>

// Import and export OBJ, FBX, glTF/GLB and USD (ASCII USDA). Returns false with an error string
// on failure. Import triangulates and tags polygons; export writes quads back where it can.
bool ImportMesh(const std::string& path, Mesh& out, std::string& error);
bool ExportMesh(const std::string& path, const Mesh& mesh, std::string& error);

struct Armature; struct Action;
// ExportMesh plus rigging: glTF/GLB get the armature as a skin (bind-pose mesh, JOINTS_0/WEIGHTS_0,
// joints posed as now) and the keyframes baked per frame into an animation. Other formats ignore
// the extras. Either pointer may be null.
bool ExportMeshAnimated(const std::string& path, const Mesh& mesh, const Armature* armature, const Action* action,
                        int fps, std::string& error);

// Which formats this build supports, for the file dialog filter list.
const char* ImportFilterSpec();  // e.g. "obj;fbx;gltf;glb;usd;usda;usdc"
const char* ExportFilterSpec();
