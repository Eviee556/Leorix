#include "mesh/UVView.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <vector>

namespace {
// Quantise a position so vertices that coincide (including across separate islands) match,
// the same trick the Electron version used before comparing UVs.
inline uint64_t QuantPos(const Float3& p) {
    const float Q = 10000.0f;
    int64_t x = (int64_t)std::llround(p.x * Q);
    int64_t y = (int64_t)std::llround(p.y * Q);
    int64_t z = (int64_t)std::llround(p.z * Q);
    uint64_t h = 1469598103934665603ull;
    for (int64_t v : {x, y, z}) { h ^= (uint64_t)v; h *= 1099511628211ull; }
    return h;
}
inline bool SameUV(const Float2& a, const Float2& b) {
    return std::fabs(a.u - b.u) < 1e-4f && std::fabs(a.v - b.v) < 1e-4f;
}
struct EdgeUse { Float2 u1, u2; };
}

void UVLayout::Build(const Mesh& mesh) {
    Clear();
    if (!mesh.HasLoopUVs() || mesh.tris.empty()) return;

    // Collect every UV edge, keyed by the quantised 3D edge so the two sides of a seam meet here.
    std::unordered_map<uint64_t, std::vector<EdgeUse>> edges;
    edges.reserve(mesh.tris.size() * 3);

    // Polygon boundary edges only, so quad diagonals are not drawn as UV edges.
    std::unordered_map<uint64_t, uint32_t> firstPoly;
    std::unordered_set<uint64_t> diagonals;
    for (const Tri& t : mesh.tris) {
        const uint64_t keys[3] = {Mesh::EdgeKey(t.a, t.b), Mesh::EdgeKey(t.b, t.c), Mesh::EdgeKey(t.c, t.a)};
        for (uint64_t k : keys) {
            auto it = firstPoly.find(k);
            if (it == firstPoly.end()) firstPoly.emplace(k, t.poly);
            else if (it->second == t.poly) diagonals.insert(k);
        }
    }

    for (size_t ti = 0; ti < mesh.tris.size(); ++ti) {
        const Tri& t = mesh.tris[ti];
        const uint32_t vi[3] = {t.a, t.b, t.c};
        const Float2* luv = &mesh.loopUVs[ti * 3];
        for (int e = 0; e < 3; ++e) {
            uint32_t i = vi[e], j = vi[(e + 1) % 3];
            if (diagonals.count(Mesh::EdgeKey(i, j))) continue;   // triangulation diagonal
            Float2 ua = luv[e], ub = luv[(e + 1) % 3];
            wire.push_back({ua, ub});

            uint64_t ki = QuantPos(mesh.positions[i]), kj = QuantPos(mesh.positions[j]);
            bool flip = kj < ki;
            uint64_t key = flip ? (kj ^ (ki * 1099511628211ull)) : (ki ^ (kj * 1099511628211ull));
            edges[key].push_back(flip ? EdgeUse{ub, ua} : EdgeUse{ua, ub});
        }
    }

    // A seam is an edge used once (open border) or whose uses disagree in UV space.
    for (const auto& kv : edges) {
        const auto& uses = kv.second;
        bool seam = uses.size() == 1;
        if (!seam)
            for (size_t i = 1; i < uses.size() && !seam; ++i)
                if (!SameUV(uses[0].u1, uses[i].u1) || !SameUV(uses[0].u2, uses[i].u2)) seam = true;
        if (seam) seams.push_back({uses[0].u1, uses[0].u2});
    }

    // Island count: union-find over UV positions joined by non-seam edges.
    std::unordered_map<uint64_t, uint64_t> parent;
    auto quantUV = [](const Float2& p) {
        int64_t x = (int64_t)std::llround(p.u * 10000.0f), y = (int64_t)std::llround(p.v * 10000.0f);
        return (uint64_t)((x << 32) ^ (y & 0xFFFFFFFF));
    };
    std::function<uint64_t(uint64_t)> find = [&](uint64_t a) {
        auto it = parent.find(a);
        if (it == parent.end()) { parent[a] = a; return a; }
        if (it->second == a) return a;
        uint64_t root = find(it->second);
        parent[a] = root;
        return root;
    };
    for (const UVEdge& e : wire) {
        uint64_t a = quantUV(e.a), b = quantUV(e.b);
        uint64_t ra = find(a), rb = find(b);
        if (ra != rb) parent[ra] = rb;
    }
    std::unordered_set<uint64_t> roots;
    for (const auto& kv : parent) roots.insert(find(kv.first));
    islands = roots.size();
    valid = true;
}
