#pragma once
// Editing UVs directly: island detection, picking in UV space, selection and move/rotate/scale
// of face-corner UVs, plus the surface-to-UV lookup that painting on the 3D model uses.
// A UV selection is one flag per face corner (same indexing as Mesh::loopUVs).
#include "mesh/Mesh.h"
#include <cstdint>
#include <vector>

// UV island id per triangle. Two triangles are in one island when they share a mesh edge whose
// two ends have the same UVs on both sides (so a seam splits them). Returns the island count.
int UVIslands(const Mesh& mesh, std::vector<int>& islandOfTri);

// Front-most is meaningless in UV space, so the first triangle containing `uv` wins. -1 = none.
int PickUVTriangle(const Mesh& mesh, Float2 uv);
// Nearest face corner within maxDist (UV units) of `uv`. -1 = none.
int PickUVCorner(const Mesh& mesh, Float2 uv, float maxDist);

// Adds (or removes) every corner of the island that contains triangle `tri`.
void SelectUVIsland(const Mesh& mesh, const std::vector<int>& islandOfTri, int tri,
                    std::vector<uint8_t>& sel, bool value = true);
// Adds (or removes) every corner sharing corner `corner`'s vertex and UV position.
void SelectUVVertex(const Mesh& mesh, int corner, std::vector<uint8_t>& sel, bool value = true);

// Centre of the selected corners' UV bounding box; false when nothing is selected.
bool UVSelectionCenter(const Mesh& mesh, const std::vector<uint8_t>& sel, Float2& center);

// Writes base[i] transformed (scale, then rotate by `angle` radians about `pivot`, then move by
// `offset`) into every selected corner. Working from `base` keeps a drag free of drift.
void TransformUVSelection(Mesh& mesh, const std::vector<Float2>& base, const std::vector<uint8_t>& sel,
                          Float2 offset, float angle, Float2 scale, Float2 pivot);

// UV at object-space point `p` on triangle `tri`, from the triangle's corner UVs. False when the
// mesh has no face-corner UVs, the index is out of range or the triangle is degenerate.
// `pos` overrides mesh.positions (e.g. skinned positions, so the hit is measured on the posed surface).
bool SurfaceUV(const Mesh& mesh, uint32_t tri, const Float3& p, Float2& uv, const std::vector<Float3>* pos = nullptr);
// Texture pixels per object-space unit on triangle `tri` for a w x h map (0 if degenerate), so a
// brush radius measured on the surface converts to a radius in map pixels.
float TexelsPerUnit(const Mesh& mesh, uint32_t tri, int w, int h);
