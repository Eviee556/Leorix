#pragma once
#include "mesh/Mesh.h"
#include <string>
#include <vector>

// Import combines every object in a file into one editable Mesh (Milestone 2 keeps it simple:
// one mesh at a time). Formats: OBJ, glTF/GLB, FBX (via ufbx), USD/USDA (ASCII subset).
namespace MeshIO {
bool Import(const std::string& path, Mesh& out, std::string& error);

bool ExportOBJ(const std::string& path, const Mesh& m, std::string& error);
bool ExportGLTF(const std::string& path, const Mesh& m, std::string& error);   // .gltf (embedded) or .glb
bool ExportUSDA(const std::string& path, const Mesh& m, std::string& error);

// Extension helpers for the UI file filters.
const char* ImportFilters();   // "obj;gltf;glb;fbx;usd;usda;usdc"
bool CanExport(const std::string& ext); // obj, gltf, glb, usd, usda
}
