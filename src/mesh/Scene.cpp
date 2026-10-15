#include "mesh/Scene.h"
#include <unordered_map>
#include <cmath>

Float3 Transform::Apply(const Float3& v) const {
    Float3 p{v.x * scale.x, v.y * scale.y, v.z * scale.z};
    float cx = std::cos(rotation.x), sx = std::sin(rotation.x);
    float cy = std::cos(rotation.y), sy = std::sin(rotation.y);
    float cz = std::cos(rotation.z), sz = std::sin(rotation.z);
    Float3 a{p.x, p.y * cx - p.z * sx, p.y * sx + p.z * cx};          // X
    Float3 b{a.x * cy + a.z * sy, a.y, -a.x * sy + a.z * cy};          // Y
    Float3 c{b.x * cz - b.y * sz, b.x * sz + b.y * cz, b.z};           // Z
    return {c.x + position.x, c.y + position.y, c.z + position.z};
}

Float3 Transform::InverseApply(const Float3& v) const {
    Float3 p{v.x - position.x, v.y - position.y, v.z - position.z};
    // inverse rotation: negate the angles and apply Z, Y, X (reverse of Apply's X, Y, Z)
    float cx = std::cos(-rotation.x), sx = std::sin(-rotation.x);
    float cy = std::cos(-rotation.y), sy = std::sin(-rotation.y);
    float cz = std::cos(-rotation.z), sz = std::sin(-rotation.z);
    Float3 a{p.x * cz - p.y * sz, p.x * sz + p.y * cz, p.z};            // Z
    Float3 b{a.x * cy + a.z * sy, a.y, -a.x * sy + a.z * cy};           // Y
    Float3 c{b.x, b.y * cx - b.z * sx, b.y * sx + b.z * cx};            // X
    return {c.x / (scale.x != 0 ? scale.x : 1),
            c.y / (scale.y != 0 ? scale.y : 1),
            c.z / (scale.z != 0 ? scale.z : 1)};
}

void MeshObject::BuildTopology() {
    if (!topoDirty) return;
    std::unordered_set<uint64_t> edgeSet;
    edgeSet.reserve(mesh.tris.size() * 3);
    vertToTris.clear();
    vertToTris.reserve(mesh.positions.size());
    diagonals.clear();

    // An edge used twice by triangles of the SAME polygon is that polygon's triangulation
    // diagonal; an edge shared by two different polygons is a real edge.
    std::unordered_map<uint64_t, uint32_t> firstPoly;
    firstPoly.reserve(mesh.tris.size() * 3);

    for (uint32_t ti = 0; ti < mesh.tris.size(); ++ti) {
        const Tri& t = mesh.tris[ti];
        const uint64_t keys[3] = {Mesh::EdgeKey(t.a, t.b), Mesh::EdgeKey(t.b, t.c), Mesh::EdgeKey(t.c, t.a)};
        for (uint64_t k : keys) {
            edgeSet.insert(k);
            auto it = firstPoly.find(k);
            if (it == firstPoly.end()) firstPoly.emplace(k, t.poly);
            else if (it->second == t.poly) diagonals.insert(k);
        }
        vertToTris[t.a].push_back(ti);
        vertToTris[t.b].push_back(ti);
        vertToTris[t.c].push_back(ti);
    }
    uniqueEdges.assign(edgeSet.begin(), edgeSet.end());
    polyEdges.clear();
    polyEdges.reserve(uniqueEdges.size());
    for (uint64_t k : uniqueEdges)
        if (!diagonals.count(k)) polyEdges.push_back(k);
    topoDirty = false;
}
