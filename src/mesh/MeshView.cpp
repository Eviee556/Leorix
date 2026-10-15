#include "mesh/MeshView.h"
#include "app/Icons.h"
#include <algorithm>
#include <cmath>

static const float kPi = 3.14159265358979f;
static const float kNear = 0.02f;

void Camera3D::Update(float aspect) {
    Vec3 dir{std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw)};
    eye = target + dir * distance;
    forward = Normalize(target - eye);
    right = Normalize(Cross(forward, Vec3{0, 1, 0}));
    up = Cross(right, forward);
    (void)aspect;
}
void Camera3D::Frame(const AABB& b) {
    if (b.hi.x < b.lo.x) return;
    target = b.center();
    float r = std::max(0.2f, b.radius());
    distance = r / std::tan(fovY * 0.5f * kPi / 180.0f) * 1.4f;
}

void MeshView::SetMesh(Mesh&& m) {
    mesh = std::move(m);
    hasMesh = !mesh.Empty();
    ClearSelection();
}

// --------------------------------------------------------------- projection
void MeshView::Project(const Camera3D& cam, ImVec2 origin, ImVec2 size) const {
    size_t n = mesh.pos.size();
    proj.resize(n); depth.resize(n); visible.assign(n, 0);
    float tanHalf = std::tan(cam.fovY * 0.5f * kPi / 180.0f);
    float aspect = size.y > 0 ? size.x / size.y : 1.0f;
    for (size_t i = 0; i < n; ++i) {
        Vec3 d = mesh.pos[i] - cam.eye;
        float z = Dot(d, cam.forward);
        depth[i] = z;
        if (z < kNear) continue;
        float x = Dot(d, cam.right) / (z * tanHalf * aspect);
        float y = Dot(d, cam.up) / (z * tanHalf);
        proj[i] = ImVec2(origin.x + (x * 0.5f + 0.5f) * size.x, origin.y + (0.5f - y * 0.5f) * size.y);
        visible[i] = 1;
    }
}

// --------------------------------------------------------------- draw
void MeshView::Draw(ImDrawList* dl, const Camera3D& cam, ImVec2 origin, ImVec2 size,
                    Mode3D mode, int editSelectMask, bool xray, const ViewportPrefs& prefs) {
    if (!hasMesh) return;
    Project(cam, origin, size);
    const size_t triCount = mesh.TriangleCount();

    // Depth-sort triangles back-to-front (painter's), capped so huge meshes don't stall.
    const size_t kFillCap = 120000; // filled triangles per frame
    decimated = triCount > kFillCap;
    size_t step = decimated ? (triCount / kFillCap) + 1 : 1;

    struct T { uint32_t i; float z; };
    static std::vector<T> order; order.clear(); order.reserve(triCount / step + 1);
    for (size_t t = 0; t < triCount; t += step) {
        uint32_t a = mesh.tris[t * 3], b = mesh.tris[t * 3 + 1], c = mesh.tris[t * 3 + 2];
        if (!visible[a] || !visible[b] || !visible[c]) continue;
        order.push_back({static_cast<uint32_t>(t), (depth[a] + depth[b] + depth[c]) / 3.0f});
    }
    std::sort(order.begin(), order.end(), [](const T& x, const T& y) { return x.z > y.z; });

    Vec3 lightDir = Normalize(Vec3{0.4f, 0.8f, 0.5f});
    float fillAlpha = xray ? 0.35f : 1.0f;
    if (!wireframe || mode == Mode3D::Sculpt) {
        for (const auto& o : order) {
            uint32_t a = mesh.tris[o.i * 3], b = mesh.tris[o.i * 3 + 1], c = mesh.tris[o.i * 3 + 2];
            Vec3 nrm = Normalize(Cross(mesh.pos[b] - mesh.pos[a], mesh.pos[c] - mesh.pos[a]));
            float lambert = std::max(0.12f, Dot(nrm, lightDir));
            // backface: dim rather than cull so thin meshes still read
            if (Dot(nrm, cam.forward) > 0) lambert *= 0.55f;
            float base = 0.62f;
            uint8_t s = static_cast<uint8_t>(std::clamp(base * (0.35f + 0.65f * lambert), 0.0f, 1.0f) * 255);
            uint8_t r = static_cast<uint8_t>(s * 1.0f), g = static_cast<uint8_t>(s * 0.98f), bl = static_cast<uint8_t>(s * 0.92f);
            dl->AddTriangleFilled(proj[a], proj[b], proj[c], IM_COL32(r, g, bl, (int)(fillAlpha * 255)));
        }
    }

    // Wireframe (face edges only, not triangulation diagonals): draw the unique edge list.
    bool showWire = wireframe || mode == Mode3D::Edit || mode == Mode3D::Retopology;
    if (showWire) {
        ImU32 wc = mode == Mode3D::Edit ? IM_COL32(20, 22, 28, 220) : IM_COL32(20, 22, 28, 150);
        size_t edgeStep = mesh.edges.size() > 400000 ? mesh.edges.size() / 400000 + 1 : 1;
        for (size_t e = 0; e < mesh.edges.size(); e += edgeStep) {
            uint32_t a = mesh.edges[e].a, b = mesh.edges[e].b;
            if (visible[a] && visible[b]) dl->AddLine(proj[a], proj[b], wc, 1.0f);
        }
    }
    drawnTris = order.size();

    // --------------------------------------------------------- edit-mode overlays
    if (mode == Mode3D::Edit) {
        // Faces first (yellow fill), then edges (orange), then vertices (purple dots) on top.
        if (editSelectMask & static_cast<int>(SelectMode::Face)) {
            for (uint32_t fi : selFaces) {
                if (fi >= mesh.faces.size()) continue;
                const Face& f = mesh.faces[fi];
                for (uint32_t i = 1; i + 1 < f.count; ++i) {
                    uint32_t a = mesh.faceVerts[f.start], b = mesh.faceVerts[f.start + i], c = mesh.faceVerts[f.start + i + 1];
                    if (visible[a] && visible[b] && visible[c])
                        dl->AddTriangleFilled(proj[a], proj[b], proj[c], IM_COL32(240, 220, 60, 90)); // yellow
                }
            }
        }
        if (editSelectMask & static_cast<int>(SelectMode::Edge)) {
            for (uint32_t ei : selEdges) {
                if (ei >= mesh.edges.size()) continue;
                uint32_t a = mesh.edges[ei].a, b = mesh.edges[ei].b;
                if (visible[a] && visible[b]) dl->AddLine(proj[a], proj[b], IM_COL32(255, 150, 40, 255), 2.6f); // orange
            }
        }
        bool vertMode = editSelectMask & static_cast<int>(SelectMode::Vertex);
        if (vertMode) {
            // Show all vertices faintly, selected ones as bright purple dots.
            size_t vstep = mesh.pos.size() > 200000 ? mesh.pos.size() / 200000 + 1 : 1;
            for (size_t i = 0; i < mesh.pos.size(); i += vstep)
                if (visible[i]) dl->AddCircleFilled(proj[i], 2.0f, IM_COL32(150, 130, 200, 160));
        }
        for (uint32_t vi : selVerts)
            if (vi < visible.size() && visible[vi])
                dl->AddCircleFilled(proj[vi], 4.5f, IM_COL32(180, 90, 240, 255)); // purple
    }
}

// --------------------------------------------------------------- picking
bool MeshView::RayTriangle(const Vec3& ro, const Vec3& rd, uint32_t a, uint32_t b, uint32_t c, float& t, Vec3& hit) const {
    const Vec3& v0 = mesh.pos[a]; const Vec3& v1 = mesh.pos[b]; const Vec3& v2 = mesh.pos[c];
    Vec3 e1 = v1 - v0, e2 = v2 - v0, p = Cross(rd, e2);
    float det = Dot(e1, p);
    if (std::fabs(det) < 1e-8f) return false;
    float inv = 1.0f / det;
    Vec3 s = ro - v0;
    float u = Dot(s, p) * inv; if (u < 0 || u > 1) return false;
    Vec3 q = Cross(s, e1);
    float v = Dot(rd, q) * inv; if (v < 0 || u + v > 1) return false;
    float tt = Dot(e2, q) * inv; if (tt <= 1e-5f) return false;
    t = tt; hit = ro + rd * tt; return true;
}

PickResult MeshView::Pick(const Camera3D& cam, ImVec2 origin, ImVec2 size, ImVec2 mouse, int editSelectMask) const {
    PickResult r;
    if (!hasMesh) return r;
    float tanHalf = std::tan(cam.fovY * 0.5f * kPi / 180.0f);
    float aspect = size.y > 0 ? size.x / size.y : 1.0f;
    float ndcx = ((mouse.x - origin.x) / size.x * 2.0f - 1.0f) * tanHalf * aspect;
    float ndcy = (1.0f - (mouse.y - origin.y) / size.y * 2.0f) * tanHalf;
    Vec3 rd = Normalize(cam.forward + cam.right * ndcx + cam.up * ndcy);
    Vec3 ro = cam.eye;

    float best = 1e30f;
    for (size_t t = 0; t < mesh.TriangleCount(); ++t) {
        float tt; Vec3 hit;
        if (RayTriangle(ro, rd, mesh.tris[t * 3], mesh.tris[t * 3 + 1], mesh.tris[t * 3 + 2], tt, hit) && tt < best) {
            best = tt; r.hit = true; r.tri = static_cast<int>(t); r.point = hit; r.t = tt;
            r.face = static_cast<int>(mesh.triFace[t]);
        }
    }
    if (!r.hit) return r;

    // Refine to nearest vertex / edge of the picked face, in screen space.
    Project(cam, origin, size);
    const Face& f = mesh.faces[r.face];
    if (editSelectMask & static_cast<int>(SelectMode::Vertex)) {
        float bd = 1e30f;
        for (uint32_t i = 0; i < f.count; ++i) {
            uint32_t vi = mesh.faceVerts[f.start + i];
            if (!visible[vi]) continue;
            float d = (proj[vi].x - mouse.x) * (proj[vi].x - mouse.x) + (proj[vi].y - mouse.y) * (proj[vi].y - mouse.y);
            if (d < bd) { bd = d; r.vertex = static_cast<int>(vi); }
        }
    }
    if (editSelectMask & static_cast<int>(SelectMode::Edge)) {
        float bd = 1e30f;
        for (uint32_t i = 0; i < f.count; ++i) {
            uint32_t a = mesh.faceVerts[f.start + i], b = mesh.faceVerts[f.start + (i + 1) % f.count];
            if (!visible[a] || !visible[b]) continue;
            ImVec2 pa = proj[a], pb = proj[b];
            float vx = pb.x - pa.x, vy = pb.y - pa.y, len2 = vx * vx + vy * vy;
            float tt = len2 > 0 ? std::clamp(((mouse.x - pa.x) * vx + (mouse.y - pa.y) * vy) / len2, 0.0f, 1.0f) : 0.0f;
            float dx = pa.x + vx * tt - mouse.x, dy = pa.y + vy * tt - mouse.y;
            float d = dx * dx + dy * dy;
            if (d < bd) {
                bd = d;
                uint32_t ea = std::min(a, b), eb = std::max(a, b);
                for (size_t e = 0; e < mesh.edges.size(); ++e)
                    if (mesh.edges[e].a == ea && mesh.edges[e].b == eb) { r.edge = static_cast<int>(e); break; }
            }
        }
    }
    return r;
}

// --------------------------------------------------------------- sculpt
int MeshView::SculptStroke(const Vec3& center, float radius, float strength, const Vec3& normal,
                           const Vec3& grabDelta, int brushKind, bool invert) {
    if (!hasMesh) return 0;
    float r2 = radius * radius;
    int touched = 0;
    float dir = invert ? -1.0f : 1.0f;
    for (size_t i = 0; i < mesh.pos.size(); ++i) {
        Vec3 d = mesh.pos[i] - center;
        float dist2 = Dot(d, d);
        if (dist2 > r2) continue;
        float falloff = 1.0f - std::sqrt(dist2) / radius;
        falloff = falloff * falloff * (3 - 2 * falloff); // smoothstep
        float amt = strength * falloff;
        Vec3& p = mesh.pos[i];
        switch (brushKind) {
            case 0: p = p + normal * (amt * radius * 0.25f * dir); break;                 // Draw
            case 1: p = p + Normalize(mesh.normal[i]) * (amt * radius * 0.25f * dir); break; // Inflate
            case 2: p = p + grabDelta * falloff; break;                                    // Grab
            case 3: {                                                                       // Flatten toward brush plane
                float sd = Dot(p - center, normal);
                p = p - normal * (sd * amt * 0.5f); break;
            }
            default: p = p + normal * (amt * radius * 0.25f * dir); break;
        }
        ++touched;
    }
    if (touched) { mesh.RecomputeNormals(); mesh.RecomputeBounds(); }
    return touched;
}
