#include "mesh/Modifier.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

const char* ModifierName(ModifierType t) {
    switch (t) {
        case ModifierType::Array: return "Array";
        case ModifierType::Mirror: return "Mirror";
        case ModifierType::Solidify: return "Solidify";
        case ModifierType::Subdivide: return "Subdivision Surface";
        case ModifierType::Smooth: return "Smooth";
        case ModifierType::Weld: return "Weld";
        case ModifierType::Shrinkwrap: return "Shrinkwrap";
        case ModifierType::Screw: return "Screw / Spin";
        case ModifierType::Remesh: return "Remesh (Voxel)";
        case ModifierType::Displace: return "Displace";
        case ModifierType::Triangulate: return "Triangulate";
        case ModifierType::Decimate: return "Decimate";
        case ModifierType::CurveDeform: return "Curve Deform";
        default: return "";
    }
}

const char* ModifierHint(ModifierType t) {
    switch (t) {
        case ModifierType::Array: return "Repeats the mesh, offset relative to its bounding box";
        case ModifierType::Mirror: return "Mirrors across the object's axes, welding the seam";
        case ModifierType::Solidify: return "Gives a surface thickness: an offset shell plus a rim";
        case ModifierType::Subdivide: return "Catmull-Clark: every face becomes one quad per corner";
        case ModifierType::Smooth: return "Relaxes vertices toward the average of their neighbours";
        case ModifierType::Weld: return "Merges vertices closer together than the distance";
        case ModifierType::Shrinkwrap: return "Snaps vertices onto the surface of another object";
        case ModifierType::Screw: return "Revolves the profile around an axis (spin / screw)";
        case ModifierType::Remesh: return "Rebuilds the surface as uniform voxels, then relaxes it";
        case ModifierType::Displace: return "Pushes vertices along their normals, optionally by noise";
        case ModifierType::Triangulate: return "Turns every polygon into triangles";
        case ModifierType::Decimate: return "Reduces detail by welding on a coarser grid";
        case ModifierType::CurveDeform: return "Bends the mesh along a curve object";
        default: return "";
    }
}

namespace {

inline Float3 Sub(const Float3& a, const Float3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Float3 Add(const Float3& a, const Float3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Float3 Mul(const Float3& a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float  Dot(const Float3& a, const Float3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Float3 Cross(const Float3& a, const Float3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float Len(const Float3& a) { return std::sqrt(Dot(a, a)); }
inline Float3 Norm(const Float3& a) { float l = Len(a); return l > 1e-12f ? Mul(a, 1.0f / l) : Float3{0, 1, 0}; }

// Rebuilds the polygon rings of a mesh (a quad shows up as two triangles sharing a poly id).
std::vector<std::vector<uint32_t>> PolygonRings(const Mesh& m) {
    std::unordered_map<uint32_t, std::vector<const Tri*>> byPoly;
    for (const Tri& t : m.tris) byPoly[t.poly].push_back(&t);
    std::vector<std::vector<uint32_t>> rings;
    rings.reserve(byPoly.size());
    std::vector<uint32_t> order;
    order.reserve(byPoly.size());
    for (auto& kv : byPoly) order.push_back(kv.first);
    std::sort(order.begin(), order.end());
    for (uint32_t pid : order) {
        const auto& tris = byPoly[pid];
        std::vector<uint32_t> ring = {tris[0]->a, tris[0]->b, tris[0]->c};
        for (size_t i = 1; i < tris.size(); ++i) ring.push_back(tris[i]->c);   // fan reconstruction
        rings.push_back(std::move(ring));
    }
    return rings;
}

void AddPolygon(Mesh& out, const std::vector<uint32_t>& ring, uint32_t poly) {
    for (size_t i = 1; i + 1 < ring.size(); ++i) out.tris.push_back({ring[0], ring[i], ring[i + 1], poly});
}

Mesh Finish(Mesh m) {
    m.ComputeBounds();
    m.ComputeNormals();
    return m;
}

// ---------------------------------------------------------------- Array
Mesh DoArray(const Mesh& in, const Modifier& mod) {
    Mesh out;
    out.name = in.name;
    int count = std::max(1, mod.arrayCount);
    Mesh tmp = in;
    tmp.ComputeBounds();
    Float3 size{tmp.bmax.x - tmp.bmin.x, tmp.bmax.y - tmp.bmin.y, tmp.bmax.z - tmp.bmin.z};
    Float3 step{size.x * mod.arrayOffset[0], size.y * mod.arrayOffset[1], size.z * mod.arrayOffset[2]};

    for (int c = 0; c < count; ++c) {
        uint32_t vOff = (uint32_t)out.positions.size();
        uint32_t pOff = out.polyCount;
        for (const Float3& p : in.positions)
            out.positions.push_back({p.x + step.x * c, p.y + step.y * c, p.z + step.z * c});
        for (const Tri& t : in.tris) out.tris.push_back({t.a + vOff, t.b + vOff, t.c + vOff, t.poly + pOff});
        if (in.HasLoopUVs()) for (const Float2& uv : in.loopUVs) out.loopUVs.push_back(uv);
        out.polyCount += in.polyCount;
    }
    return Finish(std::move(out));
}

// ---------------------------------------------------------------- Mirror
Mesh MirrorAxis(const Mesh& in, int axis, bool merge, float threshold) {
    Mesh out = in;
    uint32_t vOff = (uint32_t)out.positions.size();
    uint32_t pOff = out.polyCount;

    // Vertices sitting on the mirror plane are shared rather than duplicated, which is what
    // Blender's Merge option does and what stops a seam down the middle.
    std::vector<uint32_t> remap(in.positions.size());
    for (size_t i = 0; i < in.positions.size(); ++i) {
        Float3 p = in.positions[i];
        float coord = axis == 0 ? p.x : axis == 1 ? p.y : p.z;
        if (merge && std::fabs(coord) <= threshold) { remap[i] = (uint32_t)i; continue; }
        if (axis == 0) p.x = -p.x; else if (axis == 1) p.y = -p.y; else p.z = -p.z;
        remap[i] = (uint32_t)out.positions.size();
        out.positions.push_back(p);
        if (in.HasLoopUVs()) { /* loop UVs are appended per triangle below */ }
    }
    for (size_t ti = 0; ti < in.tris.size(); ++ti) {
        const Tri& t = in.tris[ti];
        out.tris.push_back({remap[t.a], remap[t.c], remap[t.b], t.poly + pOff});   // flipped winding
        if (in.HasLoopUVs()) {
            out.loopUVs.push_back(in.loopUVs[ti * 3 + 0]);
            out.loopUVs.push_back(in.loopUVs[ti * 3 + 2]);
            out.loopUVs.push_back(in.loopUVs[ti * 3 + 1]);
        }
    }
    out.polyCount += in.polyCount;
    (void)vOff;
    return out;
}

Mesh DoMirror(const Mesh& in, const Modifier& mod) {
    Mesh out = in;
    if (mod.mirrorX) out = MirrorAxis(out, 0, mod.mirrorMerge, mod.mirrorThreshold);
    if (mod.mirrorY) out = MirrorAxis(out, 1, mod.mirrorMerge, mod.mirrorThreshold);
    if (mod.mirrorZ) out = MirrorAxis(out, 2, mod.mirrorMerge, mod.mirrorThreshold);
    return Finish(std::move(out));
}

// ---------------------------------------------------------------- Solidify
Mesh DoSolidify(const Mesh& in, const Modifier& mod) {
    Mesh work = in;
    work.ComputeNormals();
    Mesh out;
    out.name = in.name;
    out.positions = work.positions;
    uint32_t inner = (uint32_t)out.positions.size();
    for (size_t i = 0; i < work.positions.size(); ++i)
        out.positions.push_back(Sub(work.positions[i], Mul(work.normals[i], mod.solidifyThickness)));

    uint32_t poly = 0;
    for (const Tri& t : work.tris) out.tris.push_back({t.a, t.b, t.c, t.poly});
    poly = work.polyCount;
    for (const Tri& t : work.tris) out.tris.push_back({t.a + inner, t.c + inner, t.b + inner, t.poly + poly});
    out.polyCount = work.polyCount * 2;

    if (mod.solidifyRim) {
        // A boundary edge belongs to exactly one polygon; bridge those with quads.
        std::unordered_map<uint64_t, int> count;
        std::unordered_map<uint64_t, uint32_t> firstPoly;
        std::unordered_set<uint64_t> diagonals;
        for (const Tri& t : work.tris)
            for (uint64_t k : {Mesh::EdgeKey(t.a, t.b), Mesh::EdgeKey(t.b, t.c), Mesh::EdgeKey(t.c, t.a)}) {
                auto it = firstPoly.find(k);
                if (it == firstPoly.end()) firstPoly.emplace(k, t.poly);
                else if (it->second == t.poly) diagonals.insert(k);
                count[k] += 1;
            }
        for (const auto& kv : count) {
            if (kv.second != 1 || diagonals.count(kv.first)) continue;
            uint32_t a = uint32_t(kv.first >> 32), b = uint32_t(kv.first & 0xFFFFFFFF);
            AddPolygon(out, {a, b, b + inner, a + inner}, out.polyCount);
            ++out.polyCount;
        }
    }
    return Finish(std::move(out));
}

// ---------------------------------------------------------------- Subdivision (quad-preserving)
Mesh DoSubdivide(const Mesh& in, const Modifier& mod) {
    Mesh cur = in;
    int levels = std::clamp(mod.subdivLevels, 1, 4);
    for (int level = 0; level < levels; ++level) {
        auto rings = PolygonRings(cur);
        Mesh out;
        out.name = cur.name;
        out.positions = cur.positions;
        // Corner UVs per ring, rebuilt the way PolygonRings fans its triangles back into polygons.
        const bool uvs = cur.HasLoopUVs();
        std::vector<std::vector<Float2>> ringUV;
        if (uvs) {
            std::unordered_map<uint32_t, std::vector<size_t>> byPoly;
            for (size_t ti = 0; ti < cur.tris.size(); ++ti) byPoly[cur.tris[ti].poly].push_back(ti);
            std::vector<uint32_t> order;
            for (auto& kv : byPoly) order.push_back(kv.first);
            std::sort(order.begin(), order.end());
            for (uint32_t pid : order) {
                const auto& ts = byPoly[pid];
                std::vector<Float2> r = {cur.loopUVs[ts[0] * 3], cur.loopUVs[ts[0] * 3 + 1], cur.loopUVs[ts[0] * 3 + 2]};
                for (size_t i = 1; i < ts.size(); ++i) r.push_back(cur.loopUVs[ts[i] * 3 + 2]);
                ringUV.push_back(std::move(r));
            }
        }
        size_t ringIndex = 0;

        // Catmull-Clark style: every polygon becomes one quad per corner, meeting at the centroid.
        std::unordered_map<uint64_t, uint32_t> edgePoint;
        std::vector<std::vector<uint32_t>> vertFacePoints(cur.positions.size());
        std::vector<std::vector<uint32_t>> vertEdgePoints(cur.positions.size());

        for (const auto& ring : rings) {
            Float3 centroid{0, 0, 0};
            for (uint32_t v : ring) centroid = Add(centroid, cur.positions[v]);
            centroid = Mul(centroid, 1.0f / ring.size());
            uint32_t cIdx = (uint32_t)out.positions.size();
            out.positions.push_back(centroid);

            std::vector<uint32_t> mids(ring.size());
            for (size_t i = 0; i < ring.size(); ++i) {
                uint32_t a = ring[i], b = ring[(i + 1) % ring.size()];
                uint64_t key = Mesh::EdgeKey(a, b);
                auto it = edgePoint.find(key);
                if (it == edgePoint.end()) {
                    Float3 mid = Mul(Add(cur.positions[a], cur.positions[b]), 0.5f);
                    uint32_t idx = (uint32_t)out.positions.size();
                    out.positions.push_back(mid);
                    edgePoint.emplace(key, idx);
                    mids[i] = idx;
                } else mids[i] = it->second;
                vertEdgePoints[a].push_back(mids[i]);
                vertEdgePoints[b].push_back(mids[i]);
            }
            for (uint32_t v : ring) vertFacePoints[v].push_back(cIdx);

            const std::vector<Float2>* ru = uvs && ringIndex < ringUV.size() && ringUV[ringIndex].size() == ring.size()
                                                 ? &ringUV[ringIndex] : nullptr;
            ++ringIndex;
            Float2 cUV{0, 0};
            if (ru) { for (const Float2& q : *ru) { cUV.u += q.u; cUV.v += q.v; } cUV.u /= ring.size(); cUV.v /= ring.size(); }
            auto midUV = [](Float2 a, Float2 b) { return Float2{(a.u + b.u) * 0.5f, (a.v + b.v) * 0.5f}; };
            for (size_t i = 0; i < ring.size(); ++i) {
                size_t ip = (i + ring.size() - 1) % ring.size(), in = (i + 1) % ring.size();
                uint32_t prev = mids[ip];
                AddPolygon(out, {ring[i], mids[i], cIdx, prev}, out.polyCount);
                ++out.polyCount;
                if (uvs) {   // AddPolygon fans the quad as (0,1,2) (0,2,3); UVs stay linear, as "keep corners"
                    Float2 q0 = ru ? (*ru)[i] : Float2{0, 0}, q1 = ru ? midUV((*ru)[i], (*ru)[in]) : q0,
                           q3 = ru ? midUV((*ru)[ip], (*ru)[i]) : q0, q2 = ru ? cUV : q0;
                    for (Float2 q : {q0, q1, q2, q0, q2, q3}) out.loopUVs.push_back(q);
                }
            }
        }

        if (mod.subdivSmooth) {
            // Move the original vertices, the Catmull-Clark way: (F + 2R + (n-3)P) / n
            std::vector<Float3> moved = out.positions;
            for (size_t v = 0; v < cur.positions.size(); ++v) {
                size_t n = vertFacePoints[v].size();
                if (n < 3) continue;
                Float3 F{0, 0, 0}, R{0, 0, 0};
                for (uint32_t f : vertFacePoints[v]) F = Add(F, out.positions[f]);
                F = Mul(F, 1.0f / n);
                for (uint32_t e : vertEdgePoints[v]) R = Add(R, out.positions[e]);
                R = Mul(R, 1.0f / std::max<size_t>(1, vertEdgePoints[v].size()));
                Float3 P = cur.positions[v];
                moved[v] = Mul(Add(Add(F, Mul(R, 2.0f)), Mul(P, (float)n - 3.0f)), 1.0f / n);
            }
            out.positions = moved;
        }
        cur = Finish(std::move(out));
    }
    return cur;
}

// ---------------------------------------------------------------- Smooth (Laplacian)
Mesh DoSmooth(const Mesh& in, const Modifier& mod) {
    Mesh out = in;
    std::vector<std::vector<uint32_t>> adj(in.positions.size());
    for (const Tri& t : in.tris) {
        adj[t.a].push_back(t.b); adj[t.a].push_back(t.c);
        adj[t.b].push_back(t.a); adj[t.b].push_back(t.c);
        adj[t.c].push_back(t.a); adj[t.c].push_back(t.b);
    }
    for (int it = 0; it < std::max(1, mod.smoothIterations); ++it) {
        std::vector<Float3> next = out.positions;
        for (size_t v = 0; v < out.positions.size(); ++v) {
            if (adj[v].empty()) continue;
            Float3 sum{0, 0, 0};
            for (uint32_t n : adj[v]) sum = Add(sum, out.positions[n]);
            Float3 avg = Mul(sum, 1.0f / adj[v].size());
            next[v] = Add(out.positions[v], Mul(Sub(avg, out.positions[v]), std::clamp(mod.smoothFactor, 0.0f, 1.0f)));
        }
        out.positions.swap(next);
    }
    return Finish(std::move(out));
}

// ---------------------------------------------------------------- Weld
Mesh DoWeld(const Mesh& in, const Modifier& mod) {
    const float d = std::max(1e-6f, mod.weldDistance);
    std::unordered_map<uint64_t, uint32_t> cell;
    std::vector<uint32_t> remap(in.positions.size());
    Mesh out;
    out.name = in.name;
    auto key = [&](const Float3& p) {
        int64_t x = (int64_t)std::llround(p.x / d), y = (int64_t)std::llround(p.y / d), z = (int64_t)std::llround(p.z / d);
        uint64_t h = 1469598103934665603ull;
        for (int64_t v : {x, y, z}) { h ^= (uint64_t)v; h *= 1099511628211ull; }
        return h;
    };
    for (size_t i = 0; i < in.positions.size(); ++i) {
        uint64_t k = key(in.positions[i]);
        auto it = cell.find(k);
        if (it == cell.end()) {
            uint32_t idx = (uint32_t)out.positions.size();
            out.positions.push_back(in.positions[i]);
            cell.emplace(k, idx);
            remap[i] = idx;
        } else remap[i] = it->second;
    }
    for (size_t ti = 0; ti < in.tris.size(); ++ti) {
        const Tri& t = in.tris[ti];
        uint32_t a = remap[t.a], b = remap[t.b], c = remap[t.c];
        if (a == b || b == c || a == c) continue;            // collapsed away
        out.tris.push_back({a, b, c, t.poly});
        if (in.HasLoopUVs())
            for (int j = 0; j < 3; ++j) out.loopUVs.push_back(in.loopUVs[ti * 3 + j]);
    }
    out.polyCount = in.polyCount;
    return Finish(std::move(out));
}

// ---------------------------------------------------------------- Shrinkwrap
Mesh DoShrinkwrap(const Mesh& in, const Modifier& mod, const std::vector<const Mesh*>& targets) {
    if (mod.shrinkTarget < 0 || mod.shrinkTarget >= (int)targets.size() || !targets[mod.shrinkTarget]) return in;
    const Mesh& tgt = *targets[mod.shrinkTarget];
    if (tgt.tris.empty()) return in;

    Mesh out = in;
    out.ComputeNormals();
    for (size_t i = 0; i < out.positions.size(); ++i) {
        const Float3& p = out.positions[i];
        float best = 1e30f;
        Float3 bestPoint = p;
        for (const Tri& t : tgt.tris) {
            // closest point on triangle
            const Float3 &A = tgt.positions[t.a], &B = tgt.positions[t.b], &C = tgt.positions[t.c];
            Float3 ab = Sub(B, A), ac = Sub(C, A), ap = Sub(p, A);
            float d1 = Dot(ab, ap), d2 = Dot(ac, ap);
            Float3 q;
            if (d1 <= 0 && d2 <= 0) q = A;
            else {
                Float3 bp = Sub(p, B);
                float d3 = Dot(ab, bp), d4 = Dot(ac, bp);
                if (d3 >= 0 && d4 <= d3) q = B;
                else {
                    Float3 cp = Sub(p, C);
                    float d5 = Dot(ab, cp), d6 = Dot(ac, cp);
                    if (d6 >= 0 && d5 <= d6) q = C;
                    else {
                        float vc = d1 * d4 - d3 * d2;
                        if (vc <= 0 && d1 >= 0 && d3 <= 0) q = Add(A, Mul(ab, d1 / (d1 - d3)));
                        else {
                            float vb = d5 * d2 - d1 * d6;
                            if (vb <= 0 && d2 >= 0 && d6 <= 0) q = Add(A, Mul(ac, d2 / (d2 - d6)));
                            else {
                                float va = d3 * d6 - d5 * d4;
                                if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0)
                                    q = Add(B, Mul(Sub(C, B), (d4 - d3) / ((d4 - d3) + (d5 - d6))));
                                else {
                                    float denom = 1.0f / (va + vb + vc);
                                    q = Add(A, Add(Mul(ab, vb * denom), Mul(ac, vc * denom)));
                                }
                            }
                        }
                    }
                }
            }
            float dist = Len(Sub(q, p));
            if (dist < best) { best = dist; bestPoint = q; }
        }
        out.positions[i] = Add(bestPoint, Mul(out.normals[i], mod.shrinkOffset));
    }
    return Finish(std::move(out));
}

// ---------------------------------------------------------------- Screw / Spin
Mesh DoScrew(const Mesh& in, const Modifier& mod) {
    if (in.positions.empty()) return in;
    Mesh out;
    out.name = in.name;
    const int steps = std::clamp(mod.screwSteps, 2, 512);
    const float total = mod.screwAngle * 3.14159265f / 180.0f;
    const bool full = std::fabs(mod.screwAngle - 360.0f) < 0.01f && std::fabs(mod.screwOffset) < 1e-6f;
    const int rings = full ? steps : steps + 1;

    for (int s = 0; s < rings; ++s) {
        float t = (float)s / steps;
        float a = total * t;
        float ca = std::cos(a), sa = std::sin(a);
        float lift = mod.screwOffset * t;
        for (const Float3& p : in.positions) {
            Float3 q = p;
            if (mod.screwAxis == 1)      q = {p.x * ca + p.z * sa, p.y + lift, -p.x * sa + p.z * ca};
            else if (mod.screwAxis == 0) q = {p.x + lift, p.y * ca - p.z * sa, p.y * sa + p.z * ca};
            else                         q = {p.x * ca - p.y * sa, p.x * sa + p.y * ca, p.z + lift};
            out.positions.push_back(q);
        }
    }

    // Bridge consecutive rings. The profile's edges become the quads of the revolved surface.
    const uint32_t n = (uint32_t)in.positions.size();
    std::unordered_map<uint64_t, int> edgeCount;
    for (const Tri& t : in.tris)
        for (uint64_t k : {Mesh::EdgeKey(t.a, t.b), Mesh::EdgeKey(t.b, t.c), Mesh::EdgeKey(t.c, t.a)}) edgeCount[k] += 1;

    std::vector<std::pair<uint32_t, uint32_t>> profile;
    if (in.tris.empty()) {                       // a point cloud / open line: chain the vertices
        for (uint32_t i = 0; i + 1 < n; ++i) profile.push_back({i, i + 1});
    } else {
        for (const auto& kv : edgeCount)
            if (kv.second == 1) profile.push_back({uint32_t(kv.first >> 32), uint32_t(kv.first & 0xFFFFFFFF)});
    }

    for (int s = 0; s + 1 <= (full ? steps - 1 : steps - 1); ++s) {
        uint32_t base0 = n * s, base1 = n * ((s + 1) % rings);
        for (auto& e : profile) {
            AddPolygon(out, {base0 + e.first, base0 + e.second, base1 + e.second, base1 + e.first}, out.polyCount);
            ++out.polyCount;
        }
    }
    if (full && rings > 1) {                      // close the loop back onto the first ring
        uint32_t base0 = n * (rings - 1), base1 = 0;
        for (auto& e : profile) {
            AddPolygon(out, {base0 + e.first, base0 + e.second, base1 + e.second, base1 + e.first}, out.polyCount);
            ++out.polyCount;
        }
    }
    return Finish(std::move(out));
}

// ---------------------------------------------------------------- Remesh (voxel + marching cubes)
Mesh DoRemesh(const Mesh& in, const Modifier& mod) {
    if (in.tris.empty()) return in;
    Mesh src = in;
    src.ComputeBounds();
    float voxel = std::max(0.005f, mod.remeshVoxel);
    Float3 mn{src.bmin.x - voxel * 2, src.bmin.y - voxel * 2, src.bmin.z - voxel * 2};
    Float3 mx{src.bmax.x + voxel * 2, src.bmax.y + voxel * 2, src.bmax.z + voxel * 2};
    int nx = std::clamp((int)std::ceil((mx.x - mn.x) / voxel), 4, 192);
    int ny = std::clamp((int)std::ceil((mx.y - mn.y) / voxel), 4, 192);
    int nz = std::clamp((int)std::ceil((mx.z - mn.z) / voxel), 4, 192);

    // Inside/outside by ray parity: one ray per (y,z) row along +X, crossings sorted, spans filled.
    std::vector<uint8_t> solid((size_t)nx * ny * nz, 0);
    auto at = [&](int x, int y, int z) -> uint8_t& { return solid[((size_t)z * ny + y) * nx + x]; };
    std::vector<float> hits;
    for (int z = 0; z < nz; ++z) {
        float wz = mn.z + (z + 0.5f) * (mx.z - mn.z) / nz;
        for (int y = 0; y < ny; ++y) {
            float wy = mn.y + (y + 0.5f) * (mx.y - mn.y) / ny;
            hits.clear();
            for (const Tri& t : src.tris) {
                const Float3 &A = src.positions[t.a], &B = src.positions[t.b], &C = src.positions[t.c];
                // ray from (mn.x - 1, wy, wz) along +X, Möller-Trumbore with a fixed direction
                Float3 e1 = Sub(B, A), e2 = Sub(C, A);
                Float3 dir{1, 0, 0};
                Float3 pv = Cross(dir, e2);
                float det = Dot(e1, pv);
                if (std::fabs(det) < 1e-12f) continue;
                float invDet = 1.0f / det;
                Float3 tv = Sub(Float3{mn.x - 1.0f, wy, wz}, A);
                float u = Dot(tv, pv) * invDet;
                if (u < 0 || u > 1) continue;
                Float3 qv = Cross(tv, e1);
                float v = Dot(dir, qv) * invDet;
                if (v < 0 || u + v > 1) continue;
                float dist = Dot(e2, qv) * invDet;
                if (dist > 0) hits.push_back(mn.x - 1.0f + dist);
            }
            if (hits.size() < 2) continue;
            std::sort(hits.begin(), hits.end());
            for (size_t i = 0; i + 1 < hits.size(); i += 2) {
                for (int x = 0; x < nx; ++x) {
                    float wx = mn.x + (x + 0.5f) * (mx.x - mn.x) / nx;
                    if (wx >= hits[i] && wx <= hits[i + 1]) at(x, y, z) = 1;
                }
            }
        }
    }

    // Surface extraction: emit a quad wherever a solid voxel meets an empty neighbour, then
    // relax. Blocky before smoothing, which is exactly how a voxel remesh behaves.
    Mesh out;
    out.name = in.name;
    std::unordered_map<uint64_t, uint32_t> corners;
    auto corner = [&](int x, int y, int z) {
        uint64_t k = ((uint64_t)(uint32_t)x << 42) ^ ((uint64_t)(uint32_t)y << 21) ^ (uint32_t)z;
        auto it = corners.find(k);
        if (it != corners.end()) return it->second;
        uint32_t idx = (uint32_t)out.positions.size();
        out.positions.push_back({mn.x + x * (mx.x - mn.x) / nx,
                                 mn.y + y * (mx.y - mn.y) / ny,
                                 mn.z + z * (mx.z - mn.z) / nz});
        corners.emplace(k, idx);
        return idx;
    };
    auto isSolid = [&](int x, int y, int z) {
        return x >= 0 && y >= 0 && z >= 0 && x < nx && y < ny && z < nz && at(x, y, z) != 0;
    };
    for (int z = 0; z < nz; ++z)
    for (int y = 0; y < ny; ++y)
    for (int x = 0; x < nx; ++x) {
        if (!isSolid(x, y, z)) continue;
        if (!isSolid(x - 1, y, z)) { AddPolygon(out, {corner(x, y, z), corner(x, y, z + 1), corner(x, y + 1, z + 1), corner(x, y + 1, z)}, out.polyCount++); }
        if (!isSolid(x + 1, y, z)) { AddPolygon(out, {corner(x + 1, y, z), corner(x + 1, y + 1, z), corner(x + 1, y + 1, z + 1), corner(x + 1, y, z + 1)}, out.polyCount++); }
        if (!isSolid(x, y - 1, z)) { AddPolygon(out, {corner(x, y, z), corner(x + 1, y, z), corner(x + 1, y, z + 1), corner(x, y, z + 1)}, out.polyCount++); }
        if (!isSolid(x, y + 1, z)) { AddPolygon(out, {corner(x, y + 1, z), corner(x, y + 1, z + 1), corner(x + 1, y + 1, z + 1), corner(x + 1, y + 1, z)}, out.polyCount++); }
        if (!isSolid(x, y, z - 1)) { AddPolygon(out, {corner(x, y, z), corner(x, y + 1, z), corner(x + 1, y + 1, z), corner(x + 1, y, z)}, out.polyCount++); }
        if (!isSolid(x, y, z + 1)) { AddPolygon(out, {corner(x, y, z + 1), corner(x + 1, y, z + 1), corner(x + 1, y + 1, z + 1), corner(x, y + 1, z + 1)}, out.polyCount++); }
    }

    if (mod.remeshSmooth > 0) {
        Modifier sm; sm.smoothFactor = 0.5f; sm.smoothIterations = mod.remeshSmooth;
        out = DoSmooth(out, sm);
    }
    return Finish(std::move(out));
}

// ---------------------------------------------------------------- Displace
Mesh DoDisplace(const Mesh& in, const Modifier& mod) {
    Mesh out = in;
    out.ComputeNormals();
    for (size_t i = 0; i < out.positions.size(); ++i) {
        float amount = mod.displaceStrength * (1.0f - mod.displaceMidLevel);
        if (mod.displaceUseNoise) {
            const Float3& p = out.positions[i];
            float n = std::sin(p.x * mod.displaceNoiseScale * 7.13f) *
                      std::cos(p.y * mod.displaceNoiseScale * 5.71f) *
                      std::sin(p.z * mod.displaceNoiseScale * 3.97f);
            amount = mod.displaceStrength * (n - (mod.displaceMidLevel - 0.5f));
        }
        out.positions[i] = Add(out.positions[i], Mul(out.normals[i], amount));
    }
    return Finish(std::move(out));
}

// ---------------------------------------------------------------- Triangulate
Mesh DoTriangulate(const Mesh& in, const Modifier&) {
    Mesh out = in;
    for (uint32_t i = 0; i < (uint32_t)out.tris.size(); ++i) out.tris[i].poly = i;
    out.polyCount = (uint32_t)out.tris.size();
    return out;
}

// ---------------------------------------------------------------- Decimate
// Vertex clustering: weld on a grid coarse enough to hit the requested ratio. Not quadric error
// metrics, so it is blunter than Blender's Collapse mode, but it is predictable and fast.
Mesh DoDecimate(const Mesh& in, const Modifier& mod) {
    Mesh src = in;
    src.ComputeBounds();
    float diag = Len(Sub(src.bmax, src.bmin));
    float ratio = std::clamp(mod.decimateRatio, 0.01f, 1.0f);
    if (ratio >= 0.999f) return in;
    Modifier weld;
    weld.type = ModifierType::Weld;
    // coarser grid for a lower ratio; cube root because clustering works in three dimensions
    weld.weldDistance = std::max(1e-5f, diag * 0.01f / std::cbrt(ratio));
    return DoWeld(in, weld);
}

// ---------------------------------------------------------------- Curve Deform
// The mesh's extent along one axis is mapped onto the curve's length. Each vertex is rebuilt in
// the frame at that point on the curve, so the shape follows the bend.
Mesh DoCurveDeform(const Mesh& in, const Modifier& mod, const std::vector<const CurvePath*>& curves) {
    if (mod.curveTarget < 0 || mod.curveTarget >= (int)curves.size() || !curves[mod.curveTarget]) return in;
    std::vector<Float3> path = curves[mod.curveTarget]->Evaluate();
    if (path.size() < 2) return in;

    // arc length so the mesh spreads evenly rather than bunching where samples are dense
    std::vector<float> arc(path.size(), 0.0f);
    for (size_t i = 1; i < path.size(); ++i) arc[i] = arc[i - 1] + Len(Sub(path[i], path[i - 1]));
    float total = arc.back();
    if (total < 1e-6f) return in;

    Mesh out = in;
    out.ComputeBounds();
    int axis = std::clamp(mod.curveAxis, 0, 2);
    float lo = axis == 0 ? out.bmin.x : axis == 1 ? out.bmin.y : out.bmin.z;
    float hi = axis == 0 ? out.bmax.x : axis == 1 ? out.bmax.y : out.bmax.z;
    float span = std::max(1e-6f, hi - lo);

    for (Float3& p : out.positions) {
        float along = axis == 0 ? p.x : axis == 1 ? p.y : p.z;
        float t = std::clamp((along - lo) / span, 0.0f, 1.0f) * mod.curveStretch;
        float target = std::clamp(t, 0.0f, 1.0f) * total;

        size_t seg = 0;
        while (seg + 2 < path.size() && arc[seg + 1] < target) ++seg;
        float segLen = std::max(1e-6f, arc[seg + 1] - arc[seg]);
        float f = std::clamp((target - arc[seg]) / segLen, 0.0f, 1.0f);
        Float3 pos = Add(path[seg], Mul(Sub(path[seg + 1], path[seg]), f));
        Float3 tangent = Norm(Sub(path[seg + 1], path[seg]));

        // a stable frame: up is world Y unless the tangent is nearly vertical
        Float3 ref = std::fabs(tangent.y) > 0.95f ? Float3{1, 0, 0} : Float3{0, 1, 0};
        Float3 right = Norm(Cross(ref, tangent));
        Float3 up = Cross(tangent, right);

        // the two axes that are not running along the curve stay as offsets in that frame
        float a = axis == 0 ? p.y : axis == 1 ? p.z : p.x;
        float b = axis == 0 ? p.z : axis == 1 ? p.x : p.y;
        p = Add(pos, Add(Mul(up, a), Mul(right, b)));
    }
    return Finish(std::move(out));
}

} // namespace

std::vector<Float3> CurvePath::Evaluate() const {
    std::vector<Float3> out;
    const size_t n = points.size();
    if (n < 2) return points;
    int res = std::clamp(resolution, 1, 64);
    size_t spans = cyclic ? n : n - 1;
    for (size_t i = 0; i < spans; ++i) {
        const Float3& p0 = points[(i + n - 1) % n];
        const Float3& p1 = points[i];
        const Float3& p2 = points[(i + 1) % n];
        const Float3& p3 = points[(i + 2) % n];
        for (int s = 0; s < res; ++s) {
            float t = (float)s / res, t2 = t * t, t3 = t2 * t;
            // Catmull-Rom: passes through every control point
            Float3 p{0, 0, 0};
            p.x = 0.5f * ((2 * p1.x) + (-p0.x + p2.x) * t + (2 * p0.x - 5 * p1.x + 4 * p2.x - p3.x) * t2 + (-p0.x + 3 * p1.x - 3 * p2.x + p3.x) * t3);
            p.y = 0.5f * ((2 * p1.y) + (-p0.y + p2.y) * t + (2 * p0.y - 5 * p1.y + 4 * p2.y - p3.y) * t2 + (-p0.y + 3 * p1.y - 3 * p2.y + p3.y) * t3);
            p.z = 0.5f * ((2 * p1.z) + (-p0.z + p2.z) * t + (2 * p0.z - 5 * p1.z + 4 * p2.z - p3.z) * t2 + (-p0.z + 3 * p1.z - 3 * p2.z + p3.z) * t3);
            out.push_back(p);
        }
    }
    if (!cyclic) out.push_back(points.back());
    else out.push_back(out.front());
    return out;
}

Mesh ApplyModifier(const Mesh& in, const Modifier& m, const std::vector<const Mesh*>& targets,
                   const std::vector<const CurvePath*>& curves) {
    if (!m.enabled || in.positions.empty()) return in;
    switch (m.type) {
        case ModifierType::Array:      return DoArray(in, m);
        case ModifierType::Mirror:     return DoMirror(in, m);
        case ModifierType::Solidify:   return DoSolidify(in, m);
        case ModifierType::Subdivide:  return DoSubdivide(in, m);
        case ModifierType::Smooth:     return DoSmooth(in, m);
        case ModifierType::Weld:       return DoWeld(in, m);
        case ModifierType::Shrinkwrap: return DoShrinkwrap(in, m, targets);
        case ModifierType::Screw:      return DoScrew(in, m);
        case ModifierType::Remesh:     return DoRemesh(in, m);
        case ModifierType::Displace:   return DoDisplace(in, m);
        case ModifierType::Triangulate:return DoTriangulate(in, m);
        case ModifierType::Decimate:   return DoDecimate(in, m);
        case ModifierType::CurveDeform:return DoCurveDeform(in, m, curves);
        default: return in;
    }
}

Mesh EvaluateModifiers(const Mesh& base, const std::vector<Modifier>& stack,
                       const std::vector<const Mesh*>& targets,
                       const std::vector<const CurvePath*>& curves) {
    Mesh cur = base;
    for (const Modifier& m : stack) cur = ApplyModifier(cur, m, targets, curves);
    return cur;
}
