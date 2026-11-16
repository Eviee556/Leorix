#include "mesh/UVEdit.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <unordered_map>

namespace {
inline bool SameUV(const Float2& a, const Float2& b) {
    return std::fabs(a.u - b.u) < 1e-4f && std::fabs(a.v - b.v) < 1e-4f;
}
inline uint32_t CornerVertex(const Mesh& m, size_t corner) {
    const Tri& t = m.tris[corner / 3];
    return corner % 3 == 0 ? t.a : corner % 3 == 1 ? t.b : t.c;
}
inline Float3 Sub3(const Float3& a, const Float3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Float3 Cross3(const Float3& a, const Float3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float Dot3(const Float3& a, const Float3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
}

int UVIslands(const Mesh& mesh, std::vector<int>& islandOfTri) {
    const size_t n = mesh.tris.size();
    islandOfTri.assign(n, -1);
    if (!mesh.HasLoopUVs()) return 0;
    std::vector<int> parent(n);
    std::iota(parent.begin(), parent.end(), 0);
    auto find = [&](int a) { while (parent[a] != a) { parent[a] = parent[parent[a]]; a = parent[a]; } return a; };

    // first triangle seen on each edge, with the UVs at its two ends (ordered by vertex index)
    struct Use { int tri; Float2 lo, hi; };
    std::unordered_map<uint64_t, std::vector<Use>> edges;
    edges.reserve(n * 3);
    for (size_t ti = 0; ti < n; ++ti) {
        const Tri& t = mesh.tris[ti];
        const uint32_t v[3] = {t.a, t.b, t.c};
        for (int e = 0; e < 3; ++e) {
            uint32_t i = v[e], j = v[(e + 1) % 3];
            Float2 ui = mesh.loopUVs[ti * 3 + e], uj = mesh.loopUVs[ti * 3 + (e + 1) % 3];
            Use u{(int)ti, i < j ? ui : uj, i < j ? uj : ui};
            auto& list = edges[Mesh::EdgeKey(i, j)];
            for (const Use& o : list)
                if (SameUV(o.lo, u.lo) && SameUV(o.hi, u.hi)) { int ra = find(o.tri), rb = find(u.tri); if (ra != rb) parent[ra] = rb; }
            list.push_back(u);
        }
    }
    std::unordered_map<int, int> ids;
    for (size_t ti = 0; ti < n; ++ti) {
        auto it = ids.emplace(find((int)ti), (int)ids.size()).first;
        islandOfTri[ti] = it->second;
    }
    return (int)ids.size();
}

int PickUVTriangle(const Mesh& mesh, Float2 p) {
    if (!mesh.HasLoopUVs()) return -1;
    for (size_t ti = 0; ti < mesh.tris.size(); ++ti) {
        const Float2 &A = mesh.loopUVs[ti * 3], &B = mesh.loopUVs[ti * 3 + 1], &C = mesh.loopUVs[ti * 3 + 2];
        float d = (B.v - C.v) * (A.u - C.u) + (C.u - B.u) * (A.v - C.v);
        if (std::fabs(d) < 1e-12f) continue;
        float w0 = ((B.v - C.v) * (p.u - C.u) + (C.u - B.u) * (p.v - C.v)) / d;
        float w1 = ((C.v - A.v) * (p.u - C.u) + (A.u - C.u) * (p.v - C.v)) / d;
        if (w0 >= 0 && w1 >= 0 && w0 + w1 <= 1) return (int)ti;
    }
    return -1;
}

int PickUVCorner(const Mesh& mesh, Float2 p, float maxDist) {
    if (!mesh.HasLoopUVs()) return -1;
    int best = -1;
    float bestD = maxDist * maxDist;
    for (size_t c = 0; c < mesh.loopUVs.size(); ++c) {
        float du = mesh.loopUVs[c].u - p.u, dv = mesh.loopUVs[c].v - p.v, d = du * du + dv * dv;
        if (d <= bestD) { bestD = d; best = (int)c; }
    }
    return best;
}

void SelectUVIsland(const Mesh& mesh, const std::vector<int>& islandOfTri, int tri,
                    std::vector<uint8_t>& sel, bool value) {
    if (tri < 0 || tri >= (int)islandOfTri.size() || islandOfTri.size() != mesh.tris.size()) return;
    sel.resize(mesh.tris.size() * 3, 0);
    const int id = islandOfTri[tri];
    for (size_t ti = 0; ti < islandOfTri.size(); ++ti)
        if (islandOfTri[ti] == id) sel[ti * 3] = sel[ti * 3 + 1] = sel[ti * 3 + 2] = value;
}

void SelectUVVertex(const Mesh& mesh, int corner, std::vector<uint8_t>& sel, bool value) {
    if (!mesh.HasLoopUVs() || corner < 0 || corner >= (int)mesh.loopUVs.size()) return;
    sel.resize(mesh.loopUVs.size(), 0);
    const uint32_t v = CornerVertex(mesh, corner);
    const Float2 uv = mesh.loopUVs[corner];
    for (size_t c = 0; c < mesh.loopUVs.size(); ++c)
        if (CornerVertex(mesh, c) == v && SameUV(mesh.loopUVs[c], uv)) sel[c] = value;
}

bool UVSelectionCenter(const Mesh& mesh, const std::vector<uint8_t>& sel, Float2& center) {
    float lo[2] = {1e30f, 1e30f}, hi[2] = {-1e30f, -1e30f};
    bool any = false;
    for (size_t c = 0; c < sel.size() && c < mesh.loopUVs.size(); ++c) {
        if (!sel[c]) continue;
        any = true;
        lo[0] = std::min(lo[0], mesh.loopUVs[c].u); hi[0] = std::max(hi[0], mesh.loopUVs[c].u);
        lo[1] = std::min(lo[1], mesh.loopUVs[c].v); hi[1] = std::max(hi[1], mesh.loopUVs[c].v);
    }
    if (any) center = {(lo[0] + hi[0]) * 0.5f, (lo[1] + hi[1]) * 0.5f};
    return any;
}

void TransformUVSelection(Mesh& mesh, const std::vector<Float2>& base, const std::vector<uint8_t>& sel,
                          Float2 offset, float angle, Float2 scale, Float2 pivot) {
    if (base.size() != mesh.loopUVs.size()) return;
    const float cs = std::cos(angle), sn = std::sin(angle);
    const bool keepVertexUVs = mesh.uvs.size() == mesh.positions.size();
    for (size_t c = 0; c < sel.size() && c < base.size(); ++c) {
        if (!sel[c]) continue;
        float x = (base[c].u - pivot.u) * scale.u, y = (base[c].v - pivot.v) * scale.v;
        Float2 r{pivot.u + x * cs - y * sn + offset.u, pivot.v + x * sn + y * cs + offset.v};
        mesh.loopUVs[c] = r;
        if (keepVertexUVs) mesh.uvs[CornerVertex(mesh, c)] = r;   // per-vertex copy, as Unwrap keeps it
    }
}

bool SurfaceUV(const Mesh& mesh, uint32_t tri, const Float3& p, Float2& uv) {
    if (!mesh.HasLoopUVs() || tri >= mesh.tris.size()) return false;
    const Tri& t = mesh.tris[tri];
    if (t.a >= mesh.positions.size() || t.b >= mesh.positions.size() || t.c >= mesh.positions.size()) return false;
    const Float3 &A = mesh.positions[t.a], &B = mesh.positions[t.b], &C = mesh.positions[t.c];
    // barycentrics from sub-triangle areas, measured along the face normal
    Float3 n = Cross3(Sub3(B, A), Sub3(C, A));
    float nn = Dot3(n, n);
    if (nn < 1e-20f) return false;
    float wa = Dot3(n, Cross3(Sub3(B, p), Sub3(C, p))) / nn;
    float wb = Dot3(n, Cross3(Sub3(C, p), Sub3(A, p))) / nn;
    float wc = 1.0f - wa - wb;
    const Float2 &ua = mesh.loopUVs[tri * 3], &ub = mesh.loopUVs[tri * 3 + 1], &uc = mesh.loopUVs[tri * 3 + 2];
    uv = {ua.u * wa + ub.u * wb + uc.u * wc, ua.v * wa + ub.v * wb + uc.v * wc};
    return true;
}

float TexelsPerUnit(const Mesh& mesh, uint32_t tri, int w, int h) {
    if (!mesh.HasLoopUVs() || tri >= mesh.tris.size()) return 0.0f;
    const Tri& t = mesh.tris[tri];
    if (t.a >= mesh.positions.size() || t.b >= mesh.positions.size() || t.c >= mesh.positions.size()) return 0.0f;
    Float3 n = Cross3(Sub3(mesh.positions[t.b], mesh.positions[t.a]), Sub3(mesh.positions[t.c], mesh.positions[t.a]));
    float area3 = std::sqrt(Dot3(n, n));
    const Float2 &a = mesh.loopUVs[tri * 3], &b = mesh.loopUVs[tri * 3 + 1], &c = mesh.loopUVs[tri * 3 + 2];
    float areaPx = std::fabs((b.u - a.u) * (c.v - a.v) - (c.u - a.u) * (b.v - a.v)) * (float)w * (float)h;
    if (area3 < 1e-12f || areaPx < 1e-12f) return 0.0f;
    return std::sqrt(areaPx / area3);
}
