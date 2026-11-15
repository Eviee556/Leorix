#include "mesh/Mesh.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

void ComputeLoopTangents(const Mesh& mesh, std::vector<Float3>& T, std::vector<Float3>& B) {
    T.assign(mesh.tris.size() * 3, Float3{1, 0, 0});
    B.assign(mesh.tris.size() * 3, Float3{0, 1, 0});
    if (!mesh.HasLoopUVs()) return;
    auto sub = [](Float3 a, Float3 b) { return Float3{a.x - b.x, a.y - b.y, a.z - b.z}; };
    auto dot = [](Float3 a, Float3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
    auto cross = [](Float3 a, Float3 b) { return Float3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; };
    auto norm = [&](Float3 a) { float l = std::sqrt(dot(a, a)); return l > 1e-12f ? Float3{a.x / l, a.y / l, a.z / l} : Float3{}; };
    for (size_t ti = 0; ti < mesh.tris.size(); ++ti) {
        const Tri& t = mesh.tris[ti];
        const Float2* uv = &mesh.loopUVs[ti * 3];
        Float3 e1 = sub(mesh.positions[t.b], mesh.positions[t.a]), e2 = sub(mesh.positions[t.c], mesh.positions[t.a]);
        float du1 = uv[1].u - uv[0].u, dv1 = uv[1].v - uv[0].v, du2 = uv[2].u - uv[0].u, dv2 = uv[2].v - uv[0].v;
        float r = du1 * dv2 - du2 * dv1;
        Float3 dPdu = std::fabs(r) > 1e-12f ? Float3{(e1.x * dv2 - e2.x * dv1) / r, (e1.y * dv2 - e2.y * dv1) / r,
                                                     (e1.z * dv2 - e2.z * dv1) / r} : e1;   // degenerate UVs: any edge
        float sign = r < 0 ? -1.0f : 1.0f;
        Float3 fn = norm(cross(e1, e2));
        const uint32_t v[3] = {t.a, t.b, t.c};
        for (int k = 0; k < 3; ++k) {
            Float3 n = mesh.normals.size() == mesh.positions.size() ? norm(mesh.normals[v[k]]) : fn;
            Float3 tg = norm(sub(dPdu, Float3{n.x * dot(n, dPdu), n.y * dot(n, dPdu), n.z * dot(n, dPdu)}));
            if (dot(tg, tg) < 0.5f) continue;   // U runs along the normal: keep the default frame
            Float3 bt = cross(n, tg);
            T[ti * 3 + k] = tg;
            B[ti * 3 + k] = Float3{bt.x * sign, bt.y * sign, bt.z * sign};
        }
    }
}

void Mesh::Clear() { positions.clear(); normals.clear(); uvs.clear(); loopUVs.clear(); tris.clear(); polyCount = 0; }

void Mesh::ComputeBounds() {
    if (positions.empty()) { bmin = bmax = {}; return; }
    bmin = bmax = positions[0];
    for (const auto& p : positions) {
        bmin.x = std::min(bmin.x, p.x); bmin.y = std::min(bmin.y, p.y); bmin.z = std::min(bmin.z, p.z);
        bmax.x = std::max(bmax.x, p.x); bmax.y = std::max(bmax.y, p.y); bmax.z = std::max(bmax.z, p.z);
    }
}

void Mesh::ComputeNormals() {
    normals.assign(positions.size(), Float3{0, 0, 0});
    for (const auto& t : tris) {
        const Float3& A = positions[t.a]; const Float3& B = positions[t.b]; const Float3& C = positions[t.c];
        Float3 u{B.x - A.x, B.y - A.y, B.z - A.z}, v{C.x - A.x, C.y - A.y, C.z - A.z};
        Float3 n{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
        for (uint32_t idx : {t.a, t.b, t.c}) { normals[idx].x += n.x; normals[idx].y += n.y; normals[idx].z += n.z; }
    }
    for (auto& n : normals) {
        float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        if (len > 1e-12f) { n.x /= len; n.y /= len; n.z /= len; }
        else n = {0, 1, 0};
    }
}

void Mesh::RecenterAndNormalize(float targetSize) {
    ComputeBounds();
    Float3 c{(bmin.x + bmax.x) * 0.5f, bmin.y, (bmin.z + bmax.z) * 0.5f}; // rest on the grid (y = min)
    float sx = bmax.x - bmin.x, sy = bmax.y - bmin.y, sz = bmax.z - bmin.z;
    float maxDim = std::max({sx, sy, sz, 1e-6f});
    float scale = targetSize / maxDim;
    for (auto& p : positions) { p.x = (p.x - c.x) * scale; p.y = (p.y - c.y) * scale; p.z = (p.z - c.z) * scale; }
    ComputeBounds();
    ComputeNormals();
}

void Mesh::EnsureUVs() {
    if (HasLoopUVs()) return;
    // Promote a per-vertex UV set to per-corner if that is all we have.
    if (uvs.size() == positions.size()) {
        bool allZero = true;
        for (const auto& uv : uvs) if (uv.u != 0.0f || uv.v != 0.0f) { allZero = false; break; }
        if (!allZero) {
            loopUVs.resize(tris.size() * 3);
            for (size_t i = 0; i < tris.size(); ++i) {
                loopUVs[i * 3 + 0] = uvs[tris[i].a];
                loopUVs[i * 3 + 1] = uvs[tris[i].b];
                loopUVs[i * 3 + 2] = uvs[tris[i].c];
            }
            return;
        }
    }
    bool missing = uvs.size() != positions.size();
    if (!missing) {
        bool allZero = true;
        for (const auto& uv : uvs) if (uv.u != 0.0f || uv.v != 0.0f) { allZero = false; break; }
        missing = allZero;
    }
    if (!missing) return;
    ComputeBounds();
    Float3 c{(bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f, (bmin.z + bmax.z) * 0.5f};
    uvs.resize(positions.size());
    const float kPi = 3.14159265358979323846f;
    for (size_t i = 0; i < positions.size(); ++i) {
        Float3 d{positions[i].x - c.x, positions[i].y - c.y, positions[i].z - c.z};
        float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        if (len < 1e-9f) { uvs[i] = {0.5f, 0.5f}; continue; }
        uvs[i] = {0.5f + std::atan2(d.z, d.x) / (2 * kPi), 0.5f - std::asin(std::clamp(d.y / len, -1.0f, 1.0f)) / kPi};
    }
    loopUVs.resize(tris.size() * 3);
    for (size_t i = 0; i < tris.size(); ++i) {
        loopUVs[i * 3 + 0] = uvs[tris[i].a];
        loopUVs[i * 3 + 1] = uvs[tris[i].b];
        loopUVs[i * 3 + 2] = uvs[tris[i].c];
        // wrap fix: a triangle spanning the seam would otherwise smear the whole texture across it
        float& u0 = loopUVs[i * 3 + 0].u; float& u1 = loopUVs[i * 3 + 1].u; float& u2 = loopUVs[i * 3 + 2].u;
        float mx = std::max({u0, u1, u2});
        if (mx - std::min({u0, u1, u2}) > 0.5f) {
            if (mx - u0 > 0.5f) u0 += 1.0f;
            if (mx - u1 > 0.5f) u1 += 1.0f;
            if (mx - u2 > 0.5f) u2 += 1.0f;
        }
    }
}

void Mesh::FlipNormals() {
    for (auto& t : tris) std::swap(t.b, t.c);
    if (HasLoopUVs())
        for (size_t i = 0; i < tris.size(); ++i) std::swap(loopUVs[i * 3 + 1], loopUVs[i * 3 + 2]);
    ComputeNormals();
}

int Mesh::HarmonizeNormals() {
    if (tris.empty()) return 0;
    std::vector<uint8_t> didFlip(tris.size(), 0);

    // Which triangles share each edge, and in which direction the edge was traversed. Two
    // neighbours agree when they walk their shared edge in opposite directions.
    std::unordered_map<uint64_t, std::vector<std::pair<uint32_t, bool>>> edges;
    edges.reserve(tris.size() * 3);
    auto record = [&](uint32_t a, uint32_t b, uint32_t ti) {
        edges[EdgeKey(a, b)].push_back({ti, a < b});
    };
    for (uint32_t i = 0; i < tris.size(); ++i) {
        record(tris[i].a, tris[i].b, i);
        record(tris[i].b, tris[i].c, i);
        record(tris[i].c, tris[i].a, i);
    }

    int flipped = 0;
    std::vector<int> shell(tris.size(), -1);
    std::vector<uint32_t> stack;
    int shellCount = 0;

    for (uint32_t seed = 0; seed < tris.size(); ++seed) {
        if (shell[seed] >= 0) continue;
        int id = shellCount++;
        shell[seed] = id;
        stack.clear();
        stack.push_back(seed);
        std::vector<uint32_t> members;

        while (!stack.empty()) {
            uint32_t ti = stack.back(); stack.pop_back();
            members.push_back(ti);
            const Tri& t = tris[ti];
            const uint32_t pairs[3][2] = {{t.a, t.b}, {t.b, t.c}, {t.c, t.a}};
            for (auto& pr : pairs) {
                uint64_t key = EdgeKey(pr[0], pr[1]);
                bool myDir = pr[0] < pr[1];
                for (auto& use : edges[key]) {
                    if (use.first == ti || shell[use.first] >= 0) continue;
                    // same direction across a shared edge means the neighbour is wound the other way
                    if (use.second == myDir) {
                        std::swap(tris[use.first].b, tris[use.first].c);
                        didFlip[use.first] ^= 1;
                        ++flipped;
                    }
                    shell[use.first] = id;
                    stack.push_back(use.first);
                }
            }
        }

        // Signed volume of the shell decides whether it faces out; negative means inside out.
        double volume = 0.0;
        for (uint32_t ti : members) {
            const Float3& A = positions[tris[ti].a];
            const Float3& B = positions[tris[ti].b];
            const Float3& C = positions[tris[ti].c];
            volume += (double)A.x * (B.y * C.z - B.z * C.y)
                    - (double)A.y * (B.x * C.z - B.z * C.x)
                    + (double)A.z * (B.x * C.y - B.y * C.x);
        }
        if (volume < 0.0) {
            for (uint32_t ti : members) { std::swap(tris[ti].b, tris[ti].c); didFlip[ti] ^= 1; ++flipped; }
        }
    }

    // Only the triangles that actually turned around need their corner UVs reordered; swapping
    // them all would corrupt the faces that were already correct.
    if (flipped && HasLoopUVs())
        for (size_t i = 0; i < tris.size(); ++i)
            if (didFlip[i]) std::swap(loopUVs[i * 3 + 1], loopUVs[i * 3 + 2]);
    ComputeNormals();
    return flipped;
}

