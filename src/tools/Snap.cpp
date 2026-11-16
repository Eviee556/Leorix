#include "tools/Snap.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

const char* SnapToName(SnapTo s) {
    static const char* names[] = {"Increment", "Nearest Vertex", "Grid (1 m)", "Edge", "Edge Center", "Face"};
    int i = (int)s;
    return i >= 0 && i < (int)SnapTo::Count ? names[i] : "?";
}

float SnapAngle(float radians, bool fine) {
    const float step = (fine ? 5.0f : 15.0f) * 3.14159265358979f / 180.0f;
    return std::round(radians / step) * step;
}

float SnapScale(float factor, float step) {
    return std::max(step, std::round(factor / step) * step);
}

static Float3 Sub(const Float3& a, const Float3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static float  Dot3(const Float3& a, const Float3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static Float3 Mad(const Float3& a, const Float3& d, float t) { return {a.x + d.x * t, a.y + d.y * t, a.z + d.z * t}; }
static float  Dist2(const Float3& a, const Float3& b) { Float3 d = Sub(a, b); return Dot3(d, d); }

Float3 ClosestPointOnSegment(const Float3& p, const Float3& a, const Float3& b) {
    Float3 ab = Sub(b, a);
    float len2 = Dot3(ab, ab);
    float t = len2 > 1e-12f ? std::clamp(Dot3(Sub(p, a), ab) / len2, 0.0f, 1.0f) : 0.0f;
    return Mad(a, ab, t);
}

// Ericson, Real-Time Collision Detection 5.1.5: Voronoi regions of the triangle.
Float3 ClosestPointOnTriangle(const Float3& p, const Float3& a, const Float3& b, const Float3& c) {
    Float3 ab = Sub(b, a), ac = Sub(c, a), ap = Sub(p, a);
    float d1 = Dot3(ab, ap), d2 = Dot3(ac, ap);
    if (d1 <= 0 && d2 <= 0) return a;
    Float3 bp = Sub(p, b);
    float d3 = Dot3(ab, bp), d4 = Dot3(ac, bp);
    if (d3 >= 0 && d4 <= d3) return b;
    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) return Mad(a, ab, d1 / (d1 - d3));
    Float3 cp = Sub(p, c);
    float d5 = Dot3(ab, cp), d6 = Dot3(ac, cp);
    if (d6 >= 0 && d5 <= d6) return c;
    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) return Mad(a, ac, d2 / (d2 - d6));
    float va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return Mad(b, Sub(c, b), (d4 - d3) / ((d4 - d3) + (d5 - d6)));
    float denom = 1.0f / (va + vb + vc);
    float v = vb * denom, w = vc * denom;
    return {a.x + ab.x * v + ac.x * w, a.y + ab.y * v + ac.y * w, a.z + ab.z * v + ac.z * w};
}

bool SnapToGeometry(const Scene& scene, int skipObject, SnapTo mode, const Float3& want, Float3& out) {
    if (mode != SnapTo::Vertex && mode != SnapTo::Edge && mode != SnapTo::EdgeCenter && mode != SnapTo::Face) return false;
    float best = 1e30f;
    auto consider = [&](const Float3& w) { float d = Dist2(w, want); if (d < best) { best = d; out = w; } };
    // shortcut: brute force over every element each drag frame, add a BVH if big scenes lag.
    for (int i = 0; i < (int)scene.objects.size(); ++i) {
        if (i == skipObject) continue;
        const MeshObject& o = *scene.objects[i];
        if (!o.visible || o.isCurve) continue;
        const Mesh& m = o.displayMesh.positions.empty() ? o.mesh : o.displayMesh;
        std::vector<Float3> w(m.positions.size());
        for (size_t v = 0; v < w.size(); ++v) w[v] = o.transform.Apply(m.positions[v]);
        if (mode == SnapTo::Vertex) { for (const Float3& p : w) consider(p); continue; }
        if (mode == SnapTo::Face) {
            for (const Tri& t : m.tris)
                if (t.a < w.size() && t.b < w.size() && t.c < w.size())
                    consider(ClosestPointOnTriangle(want, w[t.a], w[t.b], w[t.c]));
            continue;
        }
        // Real polygon edges only: a triangulation diagonal appears twice inside one polygon.
        struct Use { uint32_t poly; int count; };
        std::unordered_map<uint64_t, Use> uses;
        for (const Tri& t : m.tris) {
            const uint32_t e[3][2] = {{t.a, t.b}, {t.b, t.c}, {t.c, t.a}};
            for (auto& ab : e) {
                auto [it, fresh] = uses.try_emplace(Mesh::EdgeKey(ab[0], ab[1]), Use{t.poly, 0});
                if (it->second.poly == t.poly) ++it->second.count;
                else it->second.count = 1;   // shared with another polygon: a real edge
            }
        }
        for (const auto& [key, use] : uses) {
            if (use.count > 1) continue;
            uint32_t a = uint32_t(key >> 32), b = uint32_t(key & 0xFFFFFFFF);
            if (a >= w.size() || b >= w.size()) continue;
            if (mode == SnapTo::Edge) consider(ClosestPointOnSegment(want, w[a], w[b]));
            else consider({(w[a].x + w[b].x) * 0.5f, (w[a].y + w[b].y) * 0.5f, (w[a].z + w[b].z) * 0.5f});
        }
    }
    return best < 1e29f;
}
