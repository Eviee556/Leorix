#pragma once
// The mesh the whole 3D side operates on. Designed for ZBrush-scale counts:
// - deduplicated indexed vertices
// - a half-edge-free adjacency built lazily only when a tool needs it
// - selection stored as index sets so millions of verts stay cheap
#include <cstdint>
#include <string>
#include <array>
#include <unordered_set>
#include <vector>

struct Float3 { float x = 0, y = 0, z = 0; };
struct Float2 { float u = 0, v = 0; };

// One triangle references three vertices. Quads/n-gons from importers are triangulated on load,
// but the original polygon id is kept so edit-mode can show quad wireframes (like Leorix's QuadWireView).
struct Tri { uint32_t a, b, c; uint32_t poly; };

struct Mesh {
    std::string           name;
    std::vector<Float3>   positions;
    std::vector<Float3>   normals;     // per vertex, recomputed after edits
    std::vector<Float2>   uvs;         // per vertex; may be empty
    // Per face-corner UVs, 3 per triangle. This is how Blender stores them: a UV seam does not
    // split the vertex, so a textured cube stays 8 vertices / 6 faces.
    std::vector<Float2>   loopUVs;
    std::vector<Tri>      tris;
    uint32_t              polyCount = 0;

    // Edge attributes, keyed by EdgeKey, exactly like Blender's edge flags.
    std::unordered_set<uint64_t> seamEdges;    // UV islands are cut here
    std::unordered_set<uint64_t> sharpEdges;   // shading is split here

    bool IsSeam(uint32_t a, uint32_t b) const { return seamEdges.count(EdgeKey(a, b)) != 0; }
    bool IsSharp(uint32_t a, uint32_t b) const { return sharpEdges.count(EdgeKey(a, b)) != 0; }

    // bounds
    Float3 bmin, bmax;

    size_t VertexCount() const { return positions.size(); }
    size_t TriCount() const { return tris.size(); }

    void ComputeNormals();
    void ComputeBounds();
    void RecenterAndNormalize(float targetSize = 2.0f); // frames any imported model sensibly
    void Clear();
    bool HasLoopUVs() const { return loopUVs.size() == tris.size() * 3; }
    // Some primitives and imports arrive without UVs. Material Preview needs them, so fall back
    // to a spherical projection rather than sampling a single texel.
    void EnsureUVs();
    // Flip every face's winding (Blender's Mesh > Normals > Flip).
    void FlipNormals();
    // Makes winding consistent across each connected shell by walking the topology, then orients
    // every shell outward using its signed volume. Runs automatically when a mesh enters the
    // scene, so imports with mixed winding arrive already correct.
    int  HarmonizeNormals();

    // Edge helpers for edit mode. An edge key packs the two vertex indices (min,max).
    static uint64_t EdgeKey(uint32_t a, uint32_t b) {
        return a < b ? (uint64_t(a) << 32) | b : (uint64_t(b) << 32) | a;
    }
};

// Tangent frame per face corner (3 per triangle, same indexing as loopUVs), for tangent-space
// normal maps: T follows +U, B follows +V, both orthogonal to the corner's vertex normal
// (the face normal when the mesh has none). Mirrored UVs flip B. Needs HasLoopUVs().
void ComputeLoopTangents(const Mesh& mesh, std::vector<Float3>& tangents, std::vector<Float3>& bitangents);

// A picking result in the viewport: which element and where in the world.
struct PickResult {
    bool     hit = false;
    uint32_t tri = 0;
    uint32_t vertex = 0;          // nearest vertex of the hit triangle
    uint64_t edge = 0;            // nearest edge key
    Float3   point;              // world-space hit position
    Float3   vertexPos, edgeA, edgeB; // world-space, for drawing the highlight exactly on the element
    float    depth = 0;
};
