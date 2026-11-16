#include "mesh/EditOps.h"
#include <algorithm>
#include <unordered_map>
#include <vector>

namespace {
uint64_t Dir(uint32_t a, uint32_t b) { return (uint64_t(a) << 32) | b; }

void RemapEdgeSet(std::unordered_set<uint64_t>& set, const std::vector<uint32_t>& map) {
    std::unordered_set<uint64_t> out;
    for (uint64_t k : set) {
        uint32_t a = uint32_t(k >> 32), b = uint32_t(k & 0xFFFFFFFFu);
        if (a >= map.size() || b >= map.size()) continue;
        uint32_t A = map[a], B = map[b];
        if (A != UINT32_MAX && B != UINT32_MAX && A != B) out.insert(Mesh::EdgeKey(A, B));
    }
    set.swap(out);
}

// Drops the `candidates` no triangle uses any more, then renumbers vertices and polygons densely,
// carrying per-vertex UVs, edge flags and the selection through the renumbering.
void Compact(Mesh& m, const std::unordered_set<uint32_t>& candidates, EditSelection& sel) {
    std::vector<uint8_t> used(m.positions.size(), 0);
    for (const Tri& t : m.tris) used[t.a] = used[t.b] = used[t.c] = 1;
    const bool perVertUV = m.uvs.size() == m.positions.size();
    std::vector<uint32_t> vmap(m.positions.size(), UINT32_MAX);
    uint32_t n = 0;
    for (uint32_t i = 0; i < m.positions.size(); ++i) {
        if (!used[i] && candidates.count(i)) continue;
        vmap[i] = n;
        m.positions[n] = m.positions[i];
        if (perVertUV) m.uvs[n] = m.uvs[i];
        ++n;
    }
    m.positions.resize(n);
    if (perVertUV) m.uvs.resize(n);

    std::vector<uint8_t> polyUsed(m.polyCount, 0);
    for (const Tri& t : m.tris) polyUsed[t.poly] = 1;
    std::vector<uint32_t> pmap(m.polyCount, UINT32_MAX);
    uint32_t pn = 0;
    for (uint32_t p = 0; p < m.polyCount; ++p) if (polyUsed[p]) pmap[p] = pn++;
    for (Tri& t : m.tris) { t.a = vmap[t.a]; t.b = vmap[t.b]; t.c = vmap[t.c]; t.poly = pmap[t.poly]; }
    m.polyCount = pn;

    RemapEdgeSet(m.seamEdges, vmap);
    RemapEdgeSet(m.sharpEdges, vmap);
    RemapEdgeSet(sel.edges, vmap);
    std::unordered_set<uint32_t> verts, faces;
    for (uint32_t v : sel.verts) if (v < vmap.size() && vmap[v] != UINT32_MAX) verts.insert(vmap[v]);
    for (uint32_t f : sel.faces) if (f < pmap.size() && pmap[f] != UINT32_MAX) faces.insert(pmap[f]);
    sel.verts.swap(verts);
    sel.faces.swap(faces);
    m.ComputeNormals();
    m.ComputeBounds();
}

// Every vertex the selection touches, whichever select mode picked it.
std::unordered_set<uint32_t> TouchedVerts(const Mesh& m, const EditSelection& sel, bool includeFaces) {
    std::unordered_set<uint32_t> s;
    for (uint32_t v : sel.verts) if (v < m.positions.size()) s.insert(v);
    for (uint64_t e : sel.edges) {   // a stale selection may name vertices that no longer exist
        uint32_t a = uint32_t(e >> 32), b = uint32_t(e & 0xFFFFFFFFu);
        if (a < m.positions.size() && b < m.positions.size()) { s.insert(a); s.insert(b); }
    }
    if (includeFaces)
        for (const Tri& t : m.tris) if (sel.faces.count(t.poly)) { s.insert(t.a); s.insert(t.b); s.insert(t.c); }
    return s;
}
} // namespace

EditOpResult ExtrudeFaces(Mesh& m, const EditSelection& sel) {
    EditOpResult r;
    // A face is in the region if it is selected, or (vertex/edge select) all its vertices are.
    std::unordered_set<uint32_t> vs = TouchedVerts(m, sel, false);
    std::vector<uint8_t> allIn(m.polyCount, vs.empty() ? 0 : 1), region(m.polyCount, 0);
    for (const Tri& t : m.tris)
        if (!vs.count(t.a) || !vs.count(t.b) || !vs.count(t.c)) allIn[t.poly] = 0;
    bool any = false;
    for (uint32_t p = 0; p < m.polyCount; ++p) { region[p] = allIn[p] || sel.faces.count(p); any |= region[p] != 0; }
    if (!any) { r.message = "Select faces to extrude"; return r; }

    const bool hasUV = m.HasLoopUVs();
    const bool perVertUV = m.uvs.size() == m.positions.size();
    std::unordered_set<uint64_t> dirs;   // directed edges of the region's triangles
    for (const Tri& t : m.tris)
        if (region[t.poly]) { dirs.insert(Dir(t.a, t.b)); dirs.insert(Dir(t.b, t.c)); dirs.insert(Dir(t.c, t.a)); }

    std::unordered_map<uint32_t, uint32_t> dup;
    auto D = [&](uint32_t v) {
        auto it = dup.find(v);
        if (it != dup.end()) return it->second;
        uint32_t n = (uint32_t)m.positions.size();
        m.positions.push_back(m.positions[v]);
        if (perVertUV) m.uvs.push_back(m.uvs[v]);
        dup[v] = n;
        return n;
    };

    EditSelection out;
    std::unordered_set<uint32_t> candidates;
    const size_t triCount = m.tris.size();
    uint32_t nextPoly = m.polyCount;
    for (size_t i = 0; i < triCount; ++i) {
        const Tri t = m.tris[i];   // copy: the pushes below may reallocate
        if (!region[t.poly]) continue;
        const uint32_t v[3] = {t.a, t.b, t.c};
        Float2 uv[3];
        for (int j = 0; j < 3; ++j) uv[j] = hasUV ? m.loopUVs[i * 3 + j] : Float2{};
        for (int j = 0; j < 3; ++j) {
            uint32_t a = v[j], b = v[(j + 1) % 3];
            if (dirs.count(Dir(b, a))) continue;   // shared by two region faces: not a boundary
            // side quad a, b, b', a' - walks the shared edges opposite to both neighbours
            uint32_t A = D(a), B = D(b), poly = nextPoly++;
            m.tris.push_back({a, b, B, poly});
            m.tris.push_back({a, B, A, poly});
            if (hasUV) {
                Float2 ua = uv[j], ub = uv[(j + 1) % 3];
                for (Float2 c : {ua, ub, ub, ua, ub, ua}) m.loopUVs.push_back(c);
            }
            out.edges.insert(Mesh::EdgeKey(A, B));
        }
    }
    for (size_t i = 0; i < triCount; ++i) {
        Tri& t = m.tris[i];
        if (!region[t.poly]) continue;
        candidates.insert(t.a); candidates.insert(t.b); candidates.insert(t.c);
        t.a = D(t.a); t.b = D(t.b); t.c = D(t.c);
        out.faces.insert(t.poly);
    }
    m.polyCount = nextPoly;
    for (auto& kv : dup) out.verts.insert(kv.second);

    Compact(m, candidates, out);   // interior vertices of the region are now unused
    r.ok = true;
    r.message = "Extruded " + std::to_string(out.faces.size()) + " face" + (out.faces.size() == 1 ? "" : "s");
    r.sel = std::move(out);
    return r;
}

EditOpResult DeleteSelected(Mesh& m, const EditSelection& sel) {
    EditOpResult r;
    std::vector<uint8_t> kill(m.polyCount, 0);
    for (const Tri& t : m.tris)
        if (sel.faces.count(t.poly) || sel.verts.count(t.a) || sel.verts.count(t.b) || sel.verts.count(t.c) ||
            sel.edges.count(Mesh::EdgeKey(t.a, t.b)) || sel.edges.count(Mesh::EdgeKey(t.b, t.c)) ||
            sel.edges.count(Mesh::EdgeKey(t.c, t.a)))
            kill[t.poly] = 1;
    std::unordered_set<uint32_t> candidates = TouchedVerts(m, sel, false);

    const bool hasUV = m.HasLoopUVs();
    std::vector<Tri> tris;
    std::vector<Float2> uvs;
    for (size_t i = 0; i < m.tris.size(); ++i) {
        const Tri& t = m.tris[i];
        if (kill[t.poly]) { candidates.insert(t.a); candidates.insert(t.b); candidates.insert(t.c); continue; }
        tris.push_back(t);
        if (hasUV) for (int j = 0; j < 3; ++j) uvs.push_back(m.loopUVs[i * 3 + j]);
    }
    size_t faces = std::count(kill.begin(), kill.end(), 1);
    if (!faces && candidates.empty()) { r.message = "Nothing selected"; return r; }
    m.tris.swap(tris);
    if (hasUV) m.loopUVs.swap(uvs);

    size_t before = m.positions.size();
    Compact(m, candidates, r.sel);
    r.sel = {};
    r.ok = true;
    r.message = "Deleted " + std::to_string(faces) + " face" + (faces == 1 ? "" : "s") + ", " +
                std::to_string(before - m.positions.size()) + " vertices";
    return r;
}

EditOpResult MergeAtCenter(Mesh& m, const EditSelection& sel) {
    EditOpResult r;
    std::unordered_set<uint32_t> s = TouchedVerts(m, sel, true);
    if (s.size() < 2) { r.message = "Select at least two vertices to merge"; return r; }

    Float3 c{};
    for (uint32_t v : s) { c.x += m.positions[v].x; c.y += m.positions[v].y; c.z += m.positions[v].z; }
    float inv = 1.0f / (float)s.size();
    uint32_t keep = *std::min_element(s.begin(), s.end());
    m.positions[keep] = {c.x * inv, c.y * inv, c.z * inv};

    std::vector<uint32_t> map(m.positions.size());
    for (uint32_t i = 0; i < map.size(); ++i) map[i] = s.count(i) ? keep : i;
    // flags on edges into the merged vertices follow them; edges that collapse go away
    RemapEdgeSet(m.seamEdges, map);
    RemapEdgeSet(m.sharpEdges, map);

    const bool hasUV = m.HasLoopUVs();
    std::vector<Tri> tris;
    std::vector<Float2> uvs;
    for (size_t i = 0; i < m.tris.size(); ++i) {
        Tri t = m.tris[i];
        t.a = map[t.a]; t.b = map[t.b]; t.c = map[t.c];
        if (t.a == t.b || t.b == t.c || t.c == t.a) continue;   // collapsed triangle
        tris.push_back(t);
        if (hasUV) for (int j = 0; j < 3; ++j) uvs.push_back(m.loopUVs[i * 3 + j]);
    }
    m.tris.swap(tris);
    if (hasUV) m.loopUVs.swap(uvs);

    r.sel.verts.insert(keep);
    std::unordered_set<uint32_t> merged = s;
    merged.erase(keep);
    Compact(m, merged, r.sel);
    r.ok = true;
    r.message = "Merged " + std::to_string(s.size()) + " vertices";
    return r;
}
