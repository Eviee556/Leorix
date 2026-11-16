#include "mesh/Unwrap.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <vector>

namespace {

struct Vec2 { double x = 0, y = 0; };

inline Float3 Sub(const Float3& a, const Float3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline double Dot(const Float3& a, const Float3& b) { return (double)a.x * b.x + (double)a.y * b.y + (double)a.z * b.z; }
inline Float3 Cross(const Float3& a, const Float3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline double Len(const Float3& a) { return std::sqrt(Dot(a, a)); }

// One island: triangles plus the UV vertices they use. A vertex on a seam appears once per
// side of the cut, which is what lets a cut tube flatten into a rectangle.
struct Island {
    std::vector<uint32_t> tris;                 // indices into mesh.tris
    std::vector<uint32_t> verts;                // mesh vertex id per local index
    std::unordered_map<int, int> local;         // uv-vertex id -> local index
    std::vector<Vec2> uv;
};

// Flatten one triangle into its own plane, preserving edge lengths.
void TriangleLocal(const Float3& p0, const Float3& p1, const Float3& p2,
                   Vec2& a, Vec2& b, Vec2& c) {
    Float3 e1 = Sub(p1, p0), e2 = Sub(p2, p0);
    double l1 = Len(e1);
    a = {0, 0};
    b = {l1, 0};
    if (l1 < 1e-12) { c = {0, 0}; return; }
    double px = Dot(e2, e1) / l1;
    Float3 n = Cross(e1, e2);
    double area2 = Len(n);
    double py = area2 / l1;                     // perpendicular distance
    c = {px, py};
}

// Conjugate gradient on the normal equations of the LSCM system.
// Rows are stored sparsely; only a handful of entries per row.
struct SparseRow { int idx[6]; double val[6]; int n = 0; double rhs = 0;
    void Add(int i, double v) { if (i < 0) return; for (int k = 0; k < n; ++k) if (idx[k] == i) { val[k] += v; return; } if (n < 6) { idx[n] = i; val[n] = v; ++n; } } };

bool SolveLeastSquares(const std::vector<SparseRow>& rows, int unknowns, std::vector<double>& x, int iterations) {
    if (unknowns <= 0) return false;
    x.assign(unknowns, 0.0);
    std::vector<double> r(unknowns, 0.0), p(unknowns, 0.0), Ap(unknowns, 0.0), tmp(rows.size(), 0.0);

    auto applyA = [&](const std::vector<double>& v, std::vector<double>& out) {
        std::fill(tmp.begin(), tmp.end(), 0.0);
        for (size_t i = 0; i < rows.size(); ++i)
            for (int k = 0; k < rows[i].n; ++k) tmp[i] += rows[i].val[k] * v[rows[i].idx[k]];
        std::fill(out.begin(), out.end(), 0.0);
        for (size_t i = 0; i < rows.size(); ++i)
            for (int k = 0; k < rows[i].n; ++k) out[rows[i].idx[k]] += rows[i].val[k] * tmp[i];
    };

    // r = A^T b  (x starts at zero)
    for (size_t i = 0; i < rows.size(); ++i)
        for (int k = 0; k < rows[i].n; ++k) r[rows[i].idx[k]] += rows[i].val[k] * rows[i].rhs;
    p = r;
    double rs = 0; for (double v : r) rs += v * v;
    if (rs < 1e-24) return true;

    for (int it = 0; it < iterations; ++it) {
        applyA(p, Ap);
        double pAp = 0; for (int i = 0; i < unknowns; ++i) pAp += p[i] * Ap[i];
        if (std::fabs(pAp) < 1e-30) break;
        double alpha = rs / pAp;
        for (int i = 0; i < unknowns; ++i) { x[i] += alpha * p[i]; r[i] -= alpha * Ap[i]; }
        double rs2 = 0; for (double v : r) rs2 += v * v;
        if (rs2 < 1e-26) break;
        double beta = rs2 / rs;
        for (int i = 0; i < unknowns; ++i) p[i] = r[i] + beta * p[i];
        rs = rs2;
    }
    return true;
}

// LSCM for one island. Two vertices are pinned to fix translation, rotation and scale.
bool FlattenIsland(const Mesh& mesh, Island& is, const std::vector<int>& cornerUV,
                   const std::unordered_set<uint64_t>& diagonals) {
    const int n = (int)is.verts.size();
    if (n < 3) return false;

    // Pin the two ends of one mesh edge. Pinning the two farthest vertices instead distorts
    // curved surfaces: their straight-line 3D distance is a chord, while the flattened distance
    // follows the surface, and the hard constraint then fights the conformal solution.
    int pinA = -1, pinB = -1;
    double best = -1;
    for (uint32_t ti : is.tris) {
        const Tri& t = mesh.tris[ti];
        const int c[3] = {cornerUV[ti * 3], cornerUV[ti * 3 + 1], cornerUV[ti * 3 + 2]};
        const uint32_t v[3] = {t.a, t.b, t.c};
        for (int j = 0; j < 3; ++j) {
            int j2 = (j + 1) % 3;
            // Skip triangulation diagonals: pinning a quad's diagonal lands every island at 45
            // degrees, which just wastes space in the pack.
            if (diagonals.count(Mesh::EdgeKey(v[j], v[j2]))) continue;
            double d = Len(Sub(mesh.positions[v[j]], mesh.positions[v[j2]]));
            if (d > best) { best = d; pinA = is.local.at(c[j]); pinB = is.local.at(c[j2]); }
        }
    }
    if (pinA < 0 || pinB < 0 || pinA == pinB || best <= 1e-9) return false;

    // unknown numbering: every vertex except the two pinned ones, u and v interleaved
    std::vector<int> mapU(n, -1), mapV(n, -1);
    int unknowns = 0;
    for (int i = 0; i < n; ++i) {
        if (i == pinA || i == pinB) continue;
        mapU[i] = unknowns++; mapV[i] = unknowns++;
    }
    const double pinAU = 0.0, pinAV = 0.0, pinBU = best, pinBV = 0.0;

    std::vector<SparseRow> rows;
    rows.reserve(is.tris.size() * 2);
    for (uint32_t ti : is.tris) {
        const Tri& t = mesh.tris[ti];
        const int vid[3] = {cornerUV[ti * 3], cornerUV[ti * 3 + 1], cornerUV[ti * 3 + 2]};
        Vec2 q0, q1, q2;
        TriangleLocal(mesh.positions[t.a], mesh.positions[t.b], mesh.positions[t.c], q0, q1, q2);
        double dT = (q1.x - q0.x) * (q2.y - q0.y) - (q2.x - q0.x) * (q1.y - q0.y);
        if (std::fabs(dT) < 1e-16) continue;
        double s = 1.0 / std::sqrt(std::fabs(dT));
        // W_j = (x_{j+2} - x_{j+1}) + i (y_{j+2} - y_{j+1})
        double wr[3], wi[3];
        const Vec2 q[3] = {q0, q1, q2};
        for (int j = 0; j < 3; ++j) {
            const Vec2& p1 = q[(j + 1) % 3];
            const Vec2& p2 = q[(j + 2) % 3];
            wr[j] = (p2.x - p1.x) * s;
            wi[j] = (p2.y - p1.y) * s;
        }
        // real part: sum(wr*u - wi*v) = 0 ; imaginary: sum(wi*u + wr*v) = 0
        SparseRow re, im;
        for (int j = 0; j < 3; ++j) {
            int li = is.local.at(vid[j]);
            double pu = (li == pinA) ? pinAU : (li == pinB ? pinBU : 0.0);
            double pv = (li == pinA) ? pinAV : (li == pinB ? pinBV : 0.0);
            if (li == pinA || li == pinB) {
                re.rhs -= wr[j] * pu - wi[j] * pv;
                im.rhs -= wi[j] * pu + wr[j] * pv;
            } else {
                re.Add(mapU[li], wr[j]); re.Add(mapV[li], -wi[j]);
                im.Add(mapU[li], wi[j]); im.Add(mapV[li], wr[j]);
            }
        }
        rows.push_back(re);
        rows.push_back(im);
    }

    std::vector<double> x;
    if (unknowns > 0 && !SolveLeastSquares(rows, unknowns, x, std::max(2000, unknowns * 40))) return false;

    is.uv.assign(n, Vec2{});
    for (int i = 0; i < n; ++i) {
        if (i == pinA) is.uv[i] = {pinAU, pinAV};
        else if (i == pinB) is.uv[i] = {pinBU, pinBV};
        else is.uv[i] = {x[mapU[i]], x[mapV[i]]};
    }
    // reject a degenerate solve
    for (const Vec2& p : is.uv)
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) return false;
    return true;
}

// Fallback for islands LSCM can't handle: project onto the island's best-fit plane.
void ProjectIsland(const Mesh& mesh, Island& is) {
    Float3 c{0, 0, 0}, n{0, 0, 0};
    for (uint32_t v : is.verts) { c.x += mesh.positions[v].x; c.y += mesh.positions[v].y; c.z += mesh.positions[v].z; }
    float inv = 1.0f / std::max<size_t>(1, is.verts.size());
    c = {c.x * inv, c.y * inv, c.z * inv};
    for (uint32_t ti : is.tris) {
        const Tri& t = mesh.tris[ti];
        Float3 fn = Cross(Sub(mesh.positions[t.b], mesh.positions[t.a]), Sub(mesh.positions[t.c], mesh.positions[t.a]));
        n.x += fn.x; n.y += fn.y; n.z += fn.z;
    }
    double nl = Len(n);
    if (nl < 1e-12) n = {0, 1, 0}; else n = {(float)(n.x / nl), (float)(n.y / nl), (float)(n.z / nl)};
    Float3 up = std::fabs(n.y) > 0.9f ? Float3{1, 0, 0} : Float3{0, 1, 0};
    Float3 tx = Cross(up, n); double tl = Len(tx);
    tx = tl > 1e-9 ? Float3{(float)(tx.x / tl), (float)(tx.y / tl), (float)(tx.z / tl)} : Float3{1, 0, 0};
    Float3 ty = Cross(n, tx);
    is.uv.resize(is.verts.size());
    for (size_t i = 0; i < is.verts.size(); ++i) {
        Float3 d = Sub(mesh.positions[is.verts[i]], c);
        is.uv[i] = {Dot(d, tx), Dot(d, ty)};
    }
}

} // namespace

UnwrapResult UnwrapMesh(Mesh& mesh, const UnwrapOptions& opts) {
    UnwrapResult res;
    if (mesh.tris.empty()) { res.message = "Mesh has no faces"; return res; }

    // ---- group triangles into islands, never crossing a seam
    // Triangles of one polygon always stay together; polygons join across non-seam edges.
    std::unordered_map<uint64_t, std::vector<uint32_t>> edgeToTris;
    for (uint32_t i = 0; i < mesh.tris.size(); ++i) {
        const Tri& t = mesh.tris[i];
        edgeToTris[Mesh::EdgeKey(t.a, t.b)].push_back(i);
        edgeToTris[Mesh::EdgeKey(t.b, t.c)].push_back(i);
        edgeToTris[Mesh::EdgeKey(t.c, t.a)].push_back(i);
    }

    // ---- split vertices at seams: two corners at the same vertex are the same UV vertex only
    // when you can walk between their faces around that vertex without crossing a seam.
    std::unordered_set<uint64_t> diagonals;
    {
        std::unordered_map<uint64_t, uint32_t> firstPoly;
        for (const Tri& t : mesh.tris)
            for (uint64_t k : {Mesh::EdgeKey(t.a, t.b), Mesh::EdgeKey(t.b, t.c), Mesh::EdgeKey(t.c, t.a)}) {
                auto it = firstPoly.find(k);
                if (it == firstPoly.end()) firstPoly.emplace(k, t.poly);
                else if (it->second == t.poly) diagonals.insert(k);
            }
    }

    std::vector<int> parent(mesh.tris.size() * 3);
    for (size_t i = 0; i < parent.size(); ++i) parent[i] = (int)i;
    std::function<int(int)> find = [&](int a) { while (parent[a] != a) { parent[a] = parent[parent[a]]; a = parent[a]; } return a; };
    auto unite = [&](int a, int b) { int ra = find(a), rb = find(b); if (ra != rb) parent[ra] = rb; };

    for (const auto& kv : edgeToTris) {
        if (mesh.seamEdges.count(kv.first)) continue;      // the cut: do not merge across it
        const auto& tris = kv.second;
        uint32_t ea = (uint32_t)(kv.first >> 32), eb = (uint32_t)(kv.first & 0xFFFFFFFF);
        for (size_t i = 0; i + 1 < tris.size(); ++i) {
            for (size_t j = i + 1; j < tris.size(); ++j) {
                const Tri& t1 = mesh.tris[tris[i]];
                const Tri& t2 = mesh.tris[tris[j]];
                const uint32_t v1[3] = {t1.a, t1.b, t1.c};
                const uint32_t v2[3] = {t2.a, t2.b, t2.c};
                for (int c1 = 0; c1 < 3; ++c1)
                    for (int c2 = 0; c2 < 3; ++c2)
                        if (v1[c1] == v2[c2] && (v1[c1] == ea || v1[c1] == eb))
                            unite((int)tris[i] * 3 + c1, (int)tris[j] * 3 + c2);
            }
        }
    }
    std::vector<int> cornerUV(mesh.tris.size() * 3);
    for (size_t i = 0; i < cornerUV.size(); ++i) cornerUV[i] = find((int)i);

    std::vector<int> islandOf(mesh.tris.size(), -1);
    std::vector<Island> islands;
    std::vector<uint32_t> stack;
    for (uint32_t start = 0; start < mesh.tris.size(); ++start) {
        if (islandOf[start] >= 0) continue;
        int id = (int)islands.size();
        islands.push_back({});
        stack.clear();
        stack.push_back(start);
        islandOf[start] = id;
        while (!stack.empty()) {
            uint32_t ti = stack.back(); stack.pop_back();
            islands[id].tris.push_back(ti);
            const Tri& t = mesh.tris[ti];
            const uint32_t pairs[3][2] = {{t.a, t.b}, {t.b, t.c}, {t.c, t.a}};
            for (auto& pr : pairs) {
                uint64_t key = Mesh::EdgeKey(pr[0], pr[1]);
                if (mesh.seamEdges.count(key)) continue;           // the cut
                for (uint32_t other : edgeToTris[key]) {
                    if (islandOf[other] >= 0) continue;
                    islandOf[other] = id;
                    stack.push_back(other);
                }
            }
        }
    }

    // ---- flatten each island
    for (Island& is : islands) {
        for (uint32_t ti : is.tris) {
            const Tri& t = mesh.tris[ti];
            const uint32_t v[3] = {t.a, t.b, t.c};
            for (int j = 0; j < 3; ++j)
                if (is.local.emplace(cornerUV[ti * 3 + j], (int)is.verts.size()).second) is.verts.push_back(v[j]);
        }
        if (!FlattenIsland(mesh, is, cornerUV, diagonals)) ProjectIsland(mesh, is);
    }

    // ---- normalise, then shelf-pack into 0..1
    struct Box { int island; double w, h; };
    std::vector<Box> boxes;
    double totalScale = 0;
    for (size_t i = 0; i < islands.size(); ++i) {
        Island& is = islands[i];
        double minx = 1e30, miny = 1e30, maxx = -1e30, maxy = -1e30;
        for (const Vec2& p : is.uv) { minx = std::min(minx, p.x); miny = std::min(miny, p.y); maxx = std::max(maxx, p.x); maxy = std::max(maxy, p.y); }
        for (Vec2& p : is.uv) { p.x -= minx; p.y -= miny; }
        double w = std::max(1e-9, maxx - minx), h = std::max(1e-9, maxy - miny);
        if (!opts.averageScale) {                 // normalise each island to its own box
            for (Vec2& p : is.uv) { p.x /= w; p.y /= h; }
            w = h = 1.0;
        }
        boxes.push_back({(int)i, w, h});
        totalScale = std::max(totalScale, std::max(w, h));
    }

    if (opts.packIslands && !boxes.empty()) {
        std::sort(boxes.begin(), boxes.end(), [](const Box& a, const Box& b) { return a.h > b.h; });
        // grow a square-ish area, then shelf pack
        double area = 0;
        for (const Box& b : boxes) area += (b.w + opts.margin) * (b.h + opts.margin);
        double side = std::sqrt(area) * 1.08;
        double x = 0, y = 0, shelfH = 0, used = 0;
        for (const Box& b : boxes) {
            if (x + b.w > side && x > 0) { x = 0; y += shelfH + opts.margin; shelfH = 0; }
            Island& is = islands[b.island];
            for (Vec2& p : is.uv) { p.x += x; p.y += y; }
            x += b.w + opts.margin;
            shelfH = std::max(shelfH, b.h);
            used = std::max(used, y + shelfH);
        }
        double extent = std::max(side, used);
        if (extent > 1e-9)
            for (Island& is : islands)
                for (Vec2& p : is.uv) { p.x /= extent; p.y /= extent; }
    }

    // ---- write per-corner UVs
    mesh.loopUVs.assign(mesh.tris.size() * 3, Float2{});
    for (size_t i = 0; i < islands.size(); ++i) {
        Island& is = islands[i];
        for (uint32_t ti : is.tris) {
            const Tri& t = mesh.tris[ti];
            for (int j = 0; j < 3; ++j) {
                const Vec2& p = is.uv[is.local.at(cornerUV[ti * 3 + j])];
                mesh.loopUVs[ti * 3 + j] = {(float)std::clamp(p.x, 0.0, 1.0), (float)std::clamp(p.y, 0.0, 1.0)};
            }
        }
    }
    // keep the per-vertex set roughly in sync for anything that still reads it
    mesh.uvs.assign(mesh.positions.size(), Float2{});
    for (size_t ti = 0; ti < mesh.tris.size(); ++ti) {
        const Tri& t = mesh.tris[ti];
        mesh.uvs[t.a] = mesh.loopUVs[ti * 3];
        mesh.uvs[t.b] = mesh.loopUVs[ti * 3 + 1];
        mesh.uvs[t.c] = mesh.loopUVs[ti * 3 + 2];
    }

    res.ok = true;
    res.islands = islands.size();
    res.message = "Unwrapped " + std::to_string(islands.size()) + " island" + (islands.size() == 1 ? "" : "s");
    return res;
}

UnwrapResult SmartUVProject(Mesh& mesh, float angleLimitDeg, const UnwrapOptions& opts) {
    if (mesh.tris.empty()) { UnwrapResult r; r.message = "Mesh has no faces"; return r; }
    // polygon normals (area weighted, so a fan of slivers can't skew them)
    uint32_t polys = 0;
    for (const Tri& t : mesh.tris) polys = std::max(polys, t.poly + 1);
    std::vector<Float3> pn(polys, Float3{0, 0, 0});
    std::unordered_map<uint64_t, std::vector<uint32_t>> edgePolys;
    for (const Tri& t : mesh.tris) {
        Float3 n = Cross(Sub(mesh.positions[t.b], mesh.positions[t.a]), Sub(mesh.positions[t.c], mesh.positions[t.a]));
        pn[t.poly].x += n.x; pn[t.poly].y += n.y; pn[t.poly].z += n.z;
        for (uint64_t k : {Mesh::EdgeKey(t.a, t.b), Mesh::EdgeKey(t.b, t.c), Mesh::EdgeKey(t.c, t.a)}) {
            auto& l = edgePolys[k];
            if (std::find(l.begin(), l.end(), t.poly) == l.end()) l.push_back(t.poly);
        }
    }
    for (Float3& n : pn) { double l = Len(n); if (l > 1e-12) n = {(float)(n.x / l), (float)(n.y / l), (float)(n.z / l)}; }

    // flood-fill groups across shared edges while the normal stays near the seed's
    std::unordered_map<uint32_t, std::vector<uint64_t>> polyEdges;
    for (const auto& kv : edgePolys) for (uint32_t p : kv.second) polyEdges[p].push_back(kv.first);
    const double cosLimit = std::cos(std::clamp(angleLimitDeg, 1.0f, 89.0f) * 3.14159265358979 / 180.0);
    std::vector<int> group(polys, -1);
    int groups = 0;
    std::vector<uint32_t> stack;
    for (uint32_t seed = 0; seed < polys; ++seed) {
        if (group[seed] >= 0 || !polyEdges.count(seed)) continue;
        const Float3 sn = pn[seed];
        group[seed] = groups;
        stack.assign(1, seed);
        while (!stack.empty()) {
            uint32_t p = stack.back(); stack.pop_back();
            for (uint64_t k : polyEdges[p])
                for (uint32_t q : edgePolys[k])
                    if (group[q] < 0 && Dot(pn[q], sn) >= cosLimit) { group[q] = groups; stack.push_back(q); }
        }
        ++groups;
    }

    // cut along group borders by unwrapping with those as the only seams, then put the user's back
    std::unordered_set<uint64_t> userSeams = std::move(mesh.seamEdges);
    mesh.seamEdges.clear();
    for (const auto& kv : edgePolys)
        for (size_t i = 1; i < kv.second.size(); ++i)
            if (group[kv.second[i]] != group[kv.second[0]]) { mesh.seamEdges.insert(kv.first); break; }
    UnwrapResult r = UnwrapMesh(mesh, opts);
    mesh.seamEdges = std::move(userSeams);
    if (r.ok) r.message = "Smart UV Project: " + std::to_string(r.islands) + " island" + (r.islands == 1 ? "" : "s");
    return r;
}

UnwrapResult MarkSeams(Mesh& mesh, const std::unordered_set<uint64_t>& edges, bool set, bool liveUnwrap) {
    for (uint64_t e : edges) { if (set) mesh.seamEdges.insert(e); else mesh.seamEdges.erase(e); }
    if (!liveUnwrap) return {};
    return UnwrapMesh(mesh);
}
