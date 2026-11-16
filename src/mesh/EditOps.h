#pragma once
// Edit Mode operators that change topology: Extrude (E), Delete (X), Merge at Center (M).
// They work on the triangle list directly, so polygons keep their triangulation, and loopUVs is
// rebuilt in the same loop as tris (3 corners per triangle, same order) so the two never desync.
#include "mesh/Mesh.h"
#include <string>
#include <unordered_set>

struct EditSelection {
    std::unordered_set<uint32_t> verts;
    std::unordered_set<uint64_t> edges;   // Mesh::EdgeKey
    std::unordered_set<uint32_t> faces;   // polygon ids
};

struct EditOpResult {
    bool          ok = false;
    std::string   message;
    EditSelection sel;   // the selection to show afterwards, in the new numbering
};

// Extrudes the selected faces (or faces whose vertices are all selected) as one region. The new
// geometry sits on top of the old until it is moved, as in Blender, where a grab follows.
EditOpResult ExtrudeFaces(Mesh& m, const EditSelection& sel);
// Deletes selected faces, every face touching a selected vertex or edge, and the vertices left unused.
EditOpResult DeleteSelected(Mesh& m, const EditSelection& sel);
// Collapses every selected vertex into one at their average position.
EditOpResult MergeAtCenter(Mesh& m, const EditSelection& sel);
// Copies the selected faces (or faces whose vertices are all selected) as new, unconnected
// geometry on top of the old and selects the copy, ready to be moved (Shift+D).
EditOpResult DuplicateFaces(Mesh& m, const EditSelection& sel);
// Moves the selected faces out of `m` into `out`, a new mesh (P). Vertices left unused are removed.
EditOpResult SeparateFaces(Mesh& m, const EditSelection& sel, Mesh& out);
