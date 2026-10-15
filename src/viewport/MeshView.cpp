#include "viewport/MeshView.h"
#include "imgui_internal.h"   // ImDrawListSharedData for the batched primitive fast path
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <unordered_map>
#include <cstdio>
#include <cstdlib>
#include <vector>

ViewportDepth& SharedViewportDepth() { static ViewportDepth d; return d; }

void ViewportDepth::Begin(ImVec2 vpOrigin, ImVec2 vpSize) {
    origin = vpOrigin;
    int nw = std::max(1, (int)(vpSize.x * scale));
    int nh = std::max(1, (int)(vpSize.y * scale));
    if (nw != w || nh != h) { w = nw; h = nh; depth.assign((size_t)w * h, FLT_MAX); }
    else Clear();
}
void ViewportDepth::Clear() { std::fill(depth.begin(), depth.end(), FLT_MAX); }

float ViewportDepth::At(ImVec2 s) const {
    if (depth.empty()) return FLT_MAX;
    int x = (int)((s.x - origin.x) * scale), y = (int)((s.y - origin.y) * scale);
    if (x < 0 || y < 0 || x >= w || y >= h) return FLT_MAX;
    return depth[(size_t)y * w + x];
}
bool ViewportDepth::Occluded(ImVec2 s, float z, float bias) const {
    float d = At(s);
    return d != FLT_MAX && d < z - bias;
}

void ViewportDepth::WriteTriangle(ImVec2 a, ImVec2 b, ImVec2 c, float za, float zb, float zc) {
    if (depth.empty()) return;
    // to buffer space
    ImVec2 A((a.x - origin.x) * scale, (a.y - origin.y) * scale);
    ImVec2 B((b.x - origin.x) * scale, (b.y - origin.y) * scale);
    ImVec2 C((c.x - origin.x) * scale, (c.y - origin.y) * scale);
    int minX = std::max(0, (int)std::floor(std::min({A.x, B.x, C.x})));
    int maxX = std::min(w - 1, (int)std::ceil(std::max({A.x, B.x, C.x})));
    int minY = std::max(0, (int)std::floor(std::min({A.y, B.y, C.y})));
    int maxY = std::min(h - 1, (int)std::ceil(std::max({A.y, B.y, C.y})));
    if (minX > maxX || minY > maxY) return;
    float det = (B.y - C.y) * (A.x - C.x) + (C.x - B.x) * (A.y - C.y);
    if (std::fabs(det) < 1e-9f) return;
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            float px = x + 0.5f, py = y + 0.5f;
            float w0 = ((B.y - C.y) * (px - C.x) + (C.x - B.x) * (py - C.y)) / det;
            float w1 = ((C.y - A.y) * (px - C.x) + (A.x - C.x) * (py - C.y)) / det;
            float w2 = 1.0f - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            float z = w0 * za + w1 * zb + w2 * zc;
            float& dst = depth[(size_t)y * w + x];
            if (z < dst) dst = z;
        }
    }
}

void RasterizeDepth(const Viewport3D& vp, MeshObject& obj) {
    if (!obj.visible || obj.mesh.positions.empty()) return;
    ViewportDepth& buf = SharedViewportDepth();
    const Mesh& mesh = obj.hasDisplay ? obj.displayMesh : obj.mesh;
    const bool useSkin = obj.armature.enabled && obj.armature.Bound() && obj.skinned.size() == mesh.positions.size();
    static std::vector<ImVec2> pts; static std::vector<float> zs; static std::vector<char> ok;
    const size_t vn = mesh.positions.size();
    pts.resize(vn); zs.resize(vn); ok.resize(vn);
    for (size_t i = 0; i < vn; ++i) {
        ImVec2 s; float z;
        ok[i] = vp.ProjectPoint(obj.transform.Apply(useSkin ? obj.skinned[i] : mesh.positions[i]), z, s) ? 1 : 0;
        pts[i] = s; zs[i] = z;
    }
    for (const Tri& t : mesh.tris) {
        if (!ok[t.a] || !ok[t.b] || !ok[t.c]) continue;
        buf.WriteTriangle(pts[t.a], pts[t.b], pts[t.c], zs[t.a], zs[t.b], zs[t.c]);
    }
}

// A vertex projected to screen for this frame.
struct ProjV { ImVec2 s; float z; bool ok; float shade; float sss; };

static float Dot3(const Float3& a, const Float3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

PickResult DrawAndPickMesh(ImDrawList* dl, const Viewport3D& vp, MeshObject& obj,
                           int selectMode, bool editMode, bool retopoMode,
                           const MeshDrawPrefs& prefs, ImVec2 mouse, bool mouseInside,
                           ShadingMode shading, const ViewLighting& light) {
    // Modifiers are a preview: the stack result is what you see and click, but Edit mode works
    // on the base mesh, the way Blender edits the cage rather than the evaluated result.
    const Mesh& mesh = (obj.hasDisplay && !editMode) ? obj.displayMesh : obj.mesh;
    PickResult pick;
    if (mesh.positions.empty()) return pick;
    obj.BuildTopology();

    const size_t vn = mesh.positions.size();
    std::vector<ProjV> proj(vn);

    // Solid keeps a camera-follow studio light (Blender's "Studio" lighting); Material and
    // Rendered use the sun and world from the Environment settings.
    Float3 L = light.sunDir;
    if (shading == ShadingMode::Solid && light.studio) {
        Vec3 f = vp.CameraForward(), r = vp.CameraRight(), u = vp.CameraUp();
        L = {-f.x + r.x * 0.35f + u.x * 0.5f, -f.y + r.y * 0.35f + u.y * 0.5f, -f.z + r.z * 0.35f + u.z * 0.5f};
    }
    float ll = std::sqrt(Dot3(L, L)); if (ll > 1e-6f) L = {L.x / ll, L.y / ll, L.z / ll};

    const bool lit = shading == ShadingMode::Material || shading == ShadingMode::Rendered;
    static std::vector<Float3> worldPos;
    worldPos.resize(vn);
    // A bound armature deforms the mesh before anything else sees it.
    const bool skinned = obj.armature.enabled && obj.armature.Bound() && obj.skinned.size() == vn;
    for (size_t i = 0; i < vn; ++i) {
        proj[i].sss = 0.0f;
        worldPos[i] = obj.transform.Apply(skinned ? obj.skinned[i] : mesh.positions[i]);
        ImVec2 s; float z;
        proj[i].ok = vp.ProjectPoint(worldPos[i], z, s);
        proj[i].s = s; proj[i].z = z;
        const Float3& n = mesh.normals.empty() ? L : mesh.normals[i];
        float d = Dot3(n, L);
        if (!lit) {
            proj[i].shade = 0.25f + 0.75f * std::clamp(d * 0.5f + 0.5f, 0.0f, 1.0f);
        } else {
            float diffuse = std::max(0.0f, d) * light.sunStrength;
            float ambient = (0.5f + 0.5f * n.y) * light.worldStrength;   // sky above, ground below
            float spec = 0.0f;
            if (shading == ShadingMode::Rendered) {
                // Blinn-Phong highlight; roughness drives how tight it is.
                Vec3 e = vp.CameraEye();
                Float3 V{e.x - worldPos[i].x, e.y - worldPos[i].y, e.z - worldPos[i].z};
                float vl = std::sqrt(Dot3(V, V)); if (vl > 1e-6f) { V.x /= vl; V.y /= vl; V.z /= vl; }
                Float3 H{L.x + V.x, L.y + V.y, L.z + V.z};
                float hl = std::sqrt(Dot3(H, H)); if (hl > 1e-6f) { H.x /= hl; H.y /= hl; H.z /= hl; }
                float rough = std::clamp(obj.material.roughness, 0.03f, 1.0f);
                float shininess = 2.0f / (rough * rough * rough * rough + 1e-4f);
                float ndoth = std::max(0.0f, Dot3(n, H));
                spec = std::pow(ndoth, std::min(shininess, 2048.0f)) * (1.0f - rough) * light.sunStrength;
            }
            float v = (diffuse + ambient + spec) * light.exposure;
            proj[i].shade = 1.0f - std::exp(-std::max(0.0f, v));

            // Subsurface: wrapped diffuse (light bleeds past the terminator) plus transmission
            // where the light is behind the surface relative to the camera.
            if (obj.material.sssWeight > 0.001f) {
                float wrap = std::clamp((d + 0.6f) / 1.6f, 0.0f, 1.0f);
                float back = std::max(0.0f, -d);
                float thin = std::pow(back, 1.5f);
                proj[i].sss = std::clamp((wrap * 0.35f + thin * 0.8f) * obj.material.sssWeight * light.sunStrength, 0.0f, 1.5f);
            }
        }
    }

    // ---- solid pass.
    // Dense meshes (ZBrush-scale) make a full std::sort of every triangle the frame bottleneck,
    // so: cull first (back-face + offscreen), then bucket-sort only what survives by quantized
    // depth. Culling typically removes well over half the triangles, and the bucket sort is
    // linear instead of n log n.
    const size_t tn = mesh.tris.size();
    static std::vector<uint32_t> visible;
    visible.clear();
    visible.reserve(tn);
    float zMin = FLT_MAX, zMax = -FLT_MAX;
    const ImVec2 clipMin = vp.ViewportOrigin();
    const ImVec2 clipMax(clipMin.x + vp.ViewportSize().x, clipMin.y + vp.ViewportSize().y);

    for (size_t i = 0; i < tn; ++i) {
        const Tri& t = mesh.tris[i];
        const ProjV &A = proj[t.a], &B = proj[t.b], &C = proj[t.c];
        if (!A.ok || !B.ok || !C.ok) continue;
        // back-face cull via screen-space winding
        if ((B.s.x - A.s.x) * (C.s.y - A.s.y) - (C.s.x - A.s.x) * (B.s.y - A.s.y) >= 0) continue;
        // offscreen cull on the triangle's screen bounds
        float minX = std::min({A.s.x, B.s.x, C.s.x}), maxX = std::max({A.s.x, B.s.x, C.s.x});
        float minY = std::min({A.s.y, B.s.y, C.s.y}), maxY = std::max({A.s.y, B.s.y, C.s.y});
        if (maxX < clipMin.x || minX > clipMax.x || maxY < clipMin.y || minY > clipMax.y) continue;
        visible.push_back(static_cast<uint32_t>(i));
        float z = (A.z + B.z + C.z) / 3.0f;
        zMin = std::min(zMin, z); zMax = std::max(zMax, z);
    }

    // bucket sort far-to-near
    static std::vector<uint32_t> order, counts, cursor;
    order.resize(visible.size());
    if (!visible.empty()) {
        const int kBuckets = 2048;
        counts.assign(kBuckets + 1, 0);
        float span = std::max(zMax - zMin, 1e-6f);
        auto bucketOf = [&](uint32_t ti) {
            const Tri& t = mesh.tris[ti];
            float z = (proj[t.a].z + proj[t.b].z + proj[t.c].z) / 3.0f;
            int b = static_cast<int>((zMax - z) / span * (kBuckets - 1)); // far first
            return std::clamp(b, 0, kBuckets - 1);
        };
        for (uint32_t ti : visible) counts[bucketOf(ti) + 1] += 1;
        for (int i = 0; i < kBuckets; ++i) counts[i + 1] += counts[i];
        cursor = counts;
        for (uint32_t ti : visible) order[cursor[bucketOf(ti)]++] = ti;
    }

    // Ambient occlusion from the depth buffer: darken vertices sitting in a crevice or close to
    // other geometry. Cheap, per vertex, and it gives Rendered the contact shading Material lacks.
    static std::vector<float> aoFactor;
    aoFactor.assign(vn, 1.0f);
    if (shading == ShadingMode::Rendered) {
        const ViewportDepth& zb = SharedViewportDepth();
        if (!zb.depth.empty()) {
            const float radius = 14.0f;
            static const float offs[8][2] = {{1,0},{-1,0},{0,1},{0,-1},{0.7f,0.7f},{-0.7f,0.7f},{0.7f,-0.7f},{-0.7f,-0.7f}};
            for (size_t i = 0; i < vn; ++i) {
                if (!proj[i].ok) continue;
                int occluded = 0;
                for (auto& o : offs) {
                    ImVec2 sp(proj[i].s.x + o[0] * radius, proj[i].s.y + o[1] * radius);
                    float d = zb.At(sp);
                    if (d != FLT_MAX && d < proj[i].z - 0.02f) ++occluded;
                }
                aoFactor[i] = 1.0f - 0.55f * (occluded / 8.0f);
                proj[i].shade *= aoFactor[i];
            }
        }
    }

    // Average AO per polygon, not per triangle: a quad's two triangles would otherwise get
    // different values and the diagonal would show up as a seam again.
    static std::vector<float> polyAO; static std::vector<float> polyAOCount;
    if (shading == ShadingMode::Rendered) {
        size_t pc = (size_t)mesh.polyCount + 1;
        polyAO.assign(pc, 0.0f); polyAOCount.assign(pc, 0.0f);
        for (const Tri& t : mesh.tris) {
            if (t.poly >= pc) continue;
            polyAO[t.poly] += aoFactor[t.a] + aoFactor[t.b] + aoFactor[t.c];
            polyAOCount[t.poly] += 3.0f;
        }
        for (size_t i = 0; i < pc; ++i) polyAO[i] = polyAOCount[i] > 0 ? polyAO[i] / polyAOCount[i] : 1.0f;
    }

    // Flat shading: one value per triangle from its geometric normal.
    auto shadeFromNormal = [&](const Float3& n) {
        float d = Dot3(n, L);
        if (!lit) return 0.25f + 0.75f * std::clamp(d * 0.5f + 0.5f, 0.0f, 1.0f);
        float diffuse = std::max(0.0f, d) * light.sunStrength;
        float ambient = (0.5f + 0.5f * n.y) * light.worldStrength;
        return 1.0f - std::exp(-std::max(0.0f, (diffuse + ambient) * light.exposure));
    };
    auto faceShade = [&](const Tri& t) {
        const Float3 &A = worldPos[t.a], &B = worldPos[t.b], &C = worldPos[t.c];
        Float3 u{B.x - A.x, B.y - A.y, B.z - A.z}, v{C.x - A.x, C.y - A.y, C.z - A.z};
        Float3 n{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
        float len = std::sqrt(Dot3(n, n));
        if (len > 1e-12f) { n.x /= len; n.y /= len; n.z /= len; }
        float base = shadeFromNormal(n);
        if (shading == ShadingMode::Rendered) {
            // specular from the face normal, and the averaged AO of its corners
            Vec3 e = vp.CameraEye();
            Float3 mid{(A.x + B.x + C.x) / 3, (A.y + B.y + C.y) / 3, (A.z + B.z + C.z) / 3};
            Float3 V{e.x - mid.x, e.y - mid.y, e.z - mid.z};
            float vl = std::sqrt(Dot3(V, V)); if (vl > 1e-6f) { V.x /= vl; V.y /= vl; V.z /= vl; }
            Float3 H{L.x + V.x, L.y + V.y, L.z + V.z};
            float hl = std::sqrt(Dot3(H, H)); if (hl > 1e-6f) { H.x /= hl; H.y /= hl; H.z /= hl; }
            float rough = std::clamp(obj.material.roughness, 0.03f, 1.0f);
            float shininess = std::min(2.0f / (rough * rough * rough * rough + 1e-4f), 2048.0f);
            float spec = std::pow(std::max(0.0f, Dot3(n, H)), shininess) * (1.0f - rough) * light.sunStrength;
            base = 1.0f - std::exp(-std::max(0.0f, (base + spec * light.exposure)));
            base *= (t.poly < polyAO.size()) ? polyAO[t.poly] : 1.0f;
        }
        return base;
    };

    ImU32 baseR = (prefs.faceColor >> IM_COL32_R_SHIFT) & 0xFF;
    ImU32 baseG = (prefs.faceColor >> IM_COL32_G_SHIFT) & 0xFF;
    ImU32 baseB = (prefs.faceColor >> IM_COL32_B_SHIFT) & 0xFF;

    // Anti-aliased fills and lines cost several extra triangles each, which dominates at these
    // counts. Flat-shaded geometry doesn't need them, so batch raw primitives instead:
    // one PrimReserve for the whole mesh, three vertices per triangle, no AA fringe.
    const ImDrawListFlags savedFlags = dl->Flags;
    dl->Flags &= ~(ImDrawListFlags_AntiAliasedFill | ImDrawListFlags_AntiAliasedLines |
                   ImDrawListFlags_AntiAliasedLinesUseTex);
    const ImVec2 uv = dl->_Data->TexUvWhitePixel;

    // Material / Rendered use the object's base colour, and its Base Color texture when one is
    // loaded (UVs come straight from the mesh; ImDrawList interpolates them affinely per triangle).
    const Material& mat = obj.material;
    bool textured = lit && mat.useTextures && mat.Has(MapSlot::BaseColor) && mesh.HasLoopUVs();
    if (std::getenv("LEORIX_DEBUG_TEX")) {
        static int once = 0;
        if (once++ < 3) std::printf("[tex] lit=%d useTex=%d hasBase=%d uvs=%zu verts=%zu -> textured=%d\n",
                                    lit ? 1 : 0, mat.useTextures ? 1 : 0, mat.Has(MapSlot::BaseColor) ? 1 : 0,
                                    mesh.uvs.size(), vn, textured ? 1 : 0);
    }
    if (lit) {
        baseR = (ImU32)std::clamp(mat.baseColor[0] * 255.0f, 0.0f, 255.0f);
        baseG = (ImU32)std::clamp(mat.baseColor[1] * 255.0f, 0.0f, 255.0f);
        baseB = (ImU32)std::clamp(mat.baseColor[2] * 255.0f, 0.0f, 255.0f);
        if (textured) { baseR = baseG = baseB = 255; }   // the texture supplies the colour
    }

    const bool wireMode = shading == ShadingMode::Wireframe;
    if (wireMode && !order.empty()) {
        // A near-black shell: it occludes the far side of the mesh (so the trace reads as being
        // on the surface) without competing with the glow.
        dl->PrimReserve((int)order.size() * 3, (int)order.size() * 3);
        for (uint32_t oi : order) {
            const Tri& t = mesh.tris[oi];
            const ProjV &A = proj[t.a], &B = proj[t.b], &C = proj[t.c];
            // neutral dark shell, lit just enough to read the form behind the trace
            float sh = 0.55f + 0.45f * ((A.shade + B.shade + C.shade) / 3.0f);
            ImU32 col = IM_COL32((int)(14 * sh), (int)(17 * sh), (int)(20 * sh), 255);
            unsigned int base = dl->_VtxCurrentIdx;
            dl->PrimWriteVtx(A.s, uv, col);
            dl->PrimWriteVtx(B.s, uv, col);
            dl->PrimWriteVtx(C.s, uv, col);
            dl->PrimWriteIdx((ImDrawIdx)base); dl->PrimWriteIdx((ImDrawIdx)(base + 1)); dl->PrimWriteIdx((ImDrawIdx)(base + 2));
        }
    }
    if (prefs.shaded && !wireMode && !order.empty()) {
        if (textured) dl->PushTexture(mat.maps[(int)MapSlot::BaseColor].Id());
        dl->PrimReserve(static_cast<int>(order.size() * 3), static_cast<int>(order.size() * 3));
        for (uint32_t oi : order) {
            const Tri& t = mesh.tris[oi];
            const ProjV &A = proj[t.a], &B = proj[t.b], &C = proj[t.c];
            unsigned int base = dl->_VtxCurrentIdx;
            const float flat = obj.shadeSmooth ? 0.0f : faceShade(t);
            if (textured) {
                const Float2* luv = &mesh.loopUVs[oi * 3];
                auto W = [&](const ProjV& P, const Float2& t2) {
                    float sh = obj.shadeSmooth ? P.shade : flat;
                    ImU32 c = IM_COL32((int)(baseR * sh), (int)(baseG * sh), (int)(baseB * sh), 255);
                    dl->PrimWriteVtx(P.s, ImVec2(t2.u, t2.v), c);
                };
                W(A, luv[0]); W(B, luv[1]); W(C, luv[2]);
            } else {
                // Smooth shading needs a colour per corner so it is interpolated across the face
                // (Gouraud). Averaging the three corners into one colour, as this used to do, just
                // gave every triangle a different flat tone - still faceted.
                const float mx = std::max({mat.sssRadius[0], mat.sssRadius[1], mat.sssRadius[2], 1e-4f});
                const bool sssOn = lit && mat.sssWeight > 0.001f;
                auto shadeColor = [&](float sh, float sss) {
                    if (!sssOn) return IM_COL32((int)(baseR * sh), (int)(baseG * sh), (int)(baseB * sh), 255);
                    return IM_COL32(
                        (int)std::clamp(baseR * sh + 255.0f * sss * (mat.sssRadius[0] / mx), 0.0f, 255.0f),
                        (int)std::clamp(baseG * sh + 255.0f * sss * (mat.sssRadius[1] / mx), 0.0f, 255.0f),
                        (int)std::clamp(baseB * sh + 255.0f * sss * (mat.sssRadius[2] / mx), 0.0f, 255.0f), 255);
                };
                if (obj.shadeSmooth) {
                    dl->PrimWriteVtx(A.s, uv, shadeColor(A.shade, A.sss));
                    dl->PrimWriteVtx(B.s, uv, shadeColor(B.shade, B.sss));
                    dl->PrimWriteVtx(C.s, uv, shadeColor(C.shade, C.sss));
                } else {
                    ImU32 col = shadeColor(flat, (A.sss + B.sss + C.sss) / 3.0f);
                    dl->PrimWriteVtx(A.s, uv, col);
                    dl->PrimWriteVtx(B.s, uv, col);
                    dl->PrimWriteVtx(C.s, uv, col);
                }
            }
            dl->PrimWriteIdx((ImDrawIdx)base); dl->PrimWriteIdx((ImDrawIdx)(base + 1)); dl->PrimWriteIdx((ImDrawIdx)(base + 2));
        }
        if (textured) dl->PopTexture();
    }

    // ---- face orientation overlay
    if (prefs.faceOrientation && shading != ShadingMode::Wireframe) {
        // `order` only holds front-facing triangles, so build a full sorted list here: a flipped
        // face is exactly the one that is back-facing while sitting on the near side.
        static std::vector<uint32_t> allOrder;
        static std::vector<float> allDepth;
        allOrder.clear(); allDepth.assign(tn, 0.0f);
        for (size_t i = 0; i < tn; ++i) {
            const Tri& t = mesh.tris[i];
            if (!proj[t.a].ok || !proj[t.b].ok || !proj[t.c].ok) continue;
            allOrder.push_back((uint32_t)i);
            allDepth[i] = (proj[t.a].z + proj[t.b].z + proj[t.c].z) / 3.0f;
        }
        std::sort(allOrder.begin(), allOrder.end(),
                  [&](uint32_t x, uint32_t y) { return allDepth[x] > allDepth[y]; });

        const ViewportDepth& zbuf = SharedViewportDepth();
        dl->PrimReserve((int)allOrder.size() * 3, (int)allOrder.size() * 3);
        int drawn = 0;
        for (uint32_t oi : allOrder) {
            const Tri& t = mesh.tris[oi];
            const ProjV &A = proj[t.a], &B = proj[t.b], &C = proj[t.c];
            // Without this the far side of a closed mesh tints straight through the near side.
            ImVec2 mid((A.s.x + B.s.x + C.s.x) / 3.0f, (A.s.y + B.s.y + C.s.y) / 3.0f);
            float midZ = (A.z + B.z + C.z) / 3.0f;
            if (zbuf.Occluded(mid, midZ, 0.04f)) continue;
            float area = (B.s.x - A.s.x) * (C.s.y - A.s.y) - (C.s.x - A.s.x) * (B.s.y - A.s.y);
            // Screen winding depends on the projection's y flip, so judge facing by the geometric
            // normal instead: a face pointing back at the camera is front-facing.
            const Float3 &WA = worldPos[t.a], &WB = worldPos[t.b], &WC = worldPos[t.c];
            Float3 e1{WB.x - WA.x, WB.y - WA.y, WB.z - WA.z};
            Float3 e2{WC.x - WA.x, WC.y - WA.y, WC.z - WA.z};
            Float3 fn{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
            Vec3 eyeP = vp.CameraEye();
            Float3 toFace{(WA.x + WB.x + WC.x) / 3 - eyeP.x, (WA.y + WB.y + WC.y) / 3 - eyeP.y,
                          (WA.z + WB.z + WC.z) / 3 - eyeP.z};
            bool front = Dot3(fn, toFace) < 0.0f;
            ImU32 col = front ? prefs.faceFront : prefs.faceBack;
            (void)area;
            unsigned int base = dl->_VtxCurrentIdx;
            dl->PrimWriteVtx(A.s, uv, col);
            dl->PrimWriteVtx(B.s, uv, col);
            dl->PrimWriteVtx(C.s, uv, col);
            dl->PrimWriteIdx((ImDrawIdx)base); dl->PrimWriteIdx((ImDrawIdx)(base + 1)); dl->PrimWriteIdx((ImDrawIdx)(base + 2));
            ++drawn;
        }
        dl->PrimUnreserve(((int)allOrder.size() - drawn) * 3, ((int)allOrder.size() - drawn) * 3);
    }

    // ---- picking: find the front-most triangle under the cursor.
    if (mouseInside) {
        float bestZ = FLT_MAX;
        for (size_t i = 0; i < tn; ++i) {
            const Tri& t = mesh.tris[i];
            const ProjV &A = proj[t.a], &B = proj[t.b], &C = proj[t.c];
            if (!A.ok || !B.ok || !C.ok) continue;
            // barycentric test
            float d = (B.s.y - C.s.y) * (A.s.x - C.s.x) + (C.s.x - B.s.x) * (A.s.y - C.s.y);
            if (std::fabs(d) < 1e-6f) continue;
            float w0 = ((B.s.y - C.s.y) * (mouse.x - C.s.x) + (C.s.x - B.s.x) * (mouse.y - C.s.y)) / d;
            float w1 = ((C.s.y - A.s.y) * (mouse.x - C.s.x) + (A.s.x - C.s.x) * (mouse.y - C.s.y)) / d;
            float w2 = 1 - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            float z = w0 * A.z + w1 * B.z + w2 * C.z;
            if (z < bestZ) {
                bestZ = z; pick.hit = true; pick.tri = static_cast<uint32_t>(i); pick.depth = z;
                const Float3 PA = obj.transform.Apply(mesh.positions[t.a]);
                const Float3 PB = obj.transform.Apply(mesh.positions[t.b]);
                const Float3 PC = obj.transform.Apply(mesh.positions[t.c]);
                pick.point = {PA.x * w0 + PB.x * w1 + PC.x * w2, PA.y * w0 + PB.y * w1 + PC.y * w2, PA.z * w0 + PB.z * w1 + PC.z * w2};
                // nearest vertex of this triangle in screen space
                float dA = (A.s.x - mouse.x) * (A.s.x - mouse.x) + (A.s.y - mouse.y) * (A.s.y - mouse.y);
                float dB = (B.s.x - mouse.x) * (B.s.x - mouse.x) + (B.s.y - mouse.y) * (B.s.y - mouse.y);
                float dC = (C.s.x - mouse.x) * (C.s.x - mouse.x) + (C.s.y - mouse.y) * (C.s.y - mouse.y);
                pick.vertex = dA < dB ? (dA < dC ? t.a : t.c) : (dB < dC ? t.b : t.c);
                pick.vertexPos = obj.transform.Apply(mesh.positions[pick.vertex]);
                // nearest edge of this triangle: distance from mouse to each edge segment
                auto edgeDist = [&](const ImVec2& p, const ImVec2& q) {
                    float vx = q.x - p.x, vy = q.y - p.y, wx = mouse.x - p.x, wy = mouse.y - p.y;
                    float tt = std::clamp((vx * wx + vy * wy) / (vx * vx + vy * vy + 1e-6f), 0.0f, 1.0f);
                    float cx = p.x + vx * tt - mouse.x, cy = p.y + vy * tt - mouse.y;
                    return cx * cx + cy * cy;
                };
                struct { uint32_t a, b; float d; } edges[3] = {
                    {t.a, t.b, edgeDist(A.s, B.s)}, {t.b, t.c, edgeDist(B.s, C.s)}, {t.c, t.a, edgeDist(C.s, A.s)}};
                // never snap to a triangulation diagonal: those are not real edges
                for (auto& e : edges)
                    if (obj.diagonals.count(Mesh::EdgeKey(e.a, e.b))) e.d = FLT_MAX;
                int be = 0; if (edges[1].d < edges[be].d) be = 1; if (edges[2].d < edges[be].d) be = 2;
                pick.edge = Mesh::EdgeKey(edges[be].a, edges[be].b);
                pick.edgeA = obj.transform.Apply(mesh.positions[edges[be].a]);
                pick.edgeB = obj.transform.Apply(mesh.positions[edges[be].b]);
            }
        }
    }

    // ---- wireframe. Only the visible (culled) triangles, and only while the mesh is light
    // enough for the overlay to mean anything - dense sculpts would be a solid black mass.
    // Each shared edge is drawn once (from the topology's unique edge list) rather than twice
    // per triangle, and only while the overlay is still legible on screen.
    const size_t kWireEdgeLimit = 150000, kDotLimit = 20000;
    bool wantWire = wireMode ||
                    ((editMode || retopoMode || prefs.wireframe) && shading != ShadingMode::Rendered);
    if (wantWire && obj.polyEdges.size() <= kWireEdgeLimit) {
        // Polygon edges only: a quad draws as a quad, not as two triangles.
        const ViewportDepth& zb = SharedViewportDepth();
        // The neon look is three passes over the same line: a wide soft halo, a tighter one,
        // then a thin bright core. Anti-aliasing goes back on here so the halo is smooth.
        struct Pass { float thick; int alpha; };
        const Pass neon[3] = {{6.0f, (int)(38 * prefs.wireGlow)}, {3.0f, (int)(90 * prefs.wireGlow)}, {1.3f, 255}};
        const int passes = wireMode ? (prefs.wireGlow > 0.01f ? 3 : 1) : 1;
        ImDrawListFlags wireSaved = dl->Flags;
        if (wireMode) dl->Flags |= ImDrawListFlags_AntiAliasedLines;

        for (uint64_t key : obj.polyEdges) {
            uint32_t a = uint32_t(key >> 32), b = uint32_t(key & 0xFFFFFFFF);
            const ProjV& P = proj[a]; const ProjV& Q = proj[b];
            if (!P.ok || !Q.ok) continue;
            // hide edges behind the surface, the way Blender does with X-ray off
            if (!prefs.xray) {
                ImVec2 mid((P.s.x + Q.s.x) * 0.5f, (P.s.y + Q.s.y) * 0.5f);
                if (zb.Occluded(mid, (P.z + Q.z) * 0.5f, 0.05f)) continue;
            }
            if (!wireMode) { dl->AddLine(P.s, Q.s, prefs.wireColor, 1.0f); continue; }
            for (int p = 3 - passes; p < 3; ++p) {
                ImU32 col = (prefs.wireNeon & ~IM_COL32_A_MASK) |
                            ((ImU32)std::clamp(neon[p].alpha, 0, 255) << IM_COL32_A_SHIFT);
                dl->AddLine(P.s, Q.s, col, neon[p].thick);
            }
        }
        if (wireMode) dl->Flags = wireSaved;
    }

    // ---- marked edges: seams and sharp edges show in every mode, like Blender's overlays
    if (!mesh.seamEdges.empty() || !mesh.sharpEdges.empty()) {
        const ViewportDepth& zb = SharedViewportDepth();
        auto drawMarked = [&](const std::unordered_set<uint64_t>& set, ImU32 col, float thick) {
            for (uint64_t key : set) {
                uint32_t a = uint32_t(key >> 32), b = uint32_t(key & 0xFFFFFFFF);
                if (a >= vn || b >= vn || !proj[a].ok || !proj[b].ok) continue;
                if (!prefs.xray) {
                    ImVec2 mid((proj[a].s.x + proj[b].s.x) * 0.5f, (proj[a].s.y + proj[b].s.y) * 0.5f);
                    if (zb.Occluded(mid, (proj[a].z + proj[b].z) * 0.5f, 0.05f)) continue;
                }
                dl->AddLine(proj[a].s, proj[b].s, col, thick);
            }
        };
        drawMarked(mesh.sharpEdges, prefs.sharpColor, 2.0f);
        drawMarked(mesh.seamEdges, prefs.seamColor, 2.4f);
    }

    // ---- edit-mode highlights, drawn exactly on the elements
    if (editMode) {
        // faces (yellow): selected fills + hover fill
        if (selectMode & 4) {
            for (const auto& t : mesh.tris) {
                bool sel = obj.selFaces.count(t.poly) != 0;
                bool hov = pick.hit && mesh.tris[pick.tri].poly == t.poly;
                if (!sel && !hov) continue;
                const ProjV &A = proj[t.a], &B = proj[t.b], &C = proj[t.c];
                if (A.ok && B.ok && C.ok) dl->AddTriangleFilled(A.s, B.s, C.s, sel ? prefs.faceSelected : prefs.faceHover);
            }
        }
        // edges (orange)
        if (selectMode & 2) {
            for (uint64_t key : obj.selEdges) {
                uint32_t a = uint32_t(key >> 32), b = uint32_t(key & 0xFFFFFFFF);
                if (proj[a].ok && proj[b].ok) dl->AddLine(proj[a].s, proj[b].s, prefs.edgeSelected, 2.6f);
            }
            if (pick.hit && (selectMode & 2)) {
                uint32_t a = uint32_t(pick.edge >> 32), b = uint32_t(pick.edge & 0xFFFFFFFF);
                if (a < vn && b < vn && proj[a].ok && proj[b].ok) dl->AddLine(proj[a].s, proj[b].s, prefs.edgeHover, 2.6f);
            }
        }
        // vertices (purple selected, small dots for all)
        const ViewportDepth& zbuf = SharedViewportDepth();
        auto visibleAt = [&](size_t i) { return prefs.xray || !zbuf.Occluded(proj[i].s, proj[i].z); };
        if (selectMode & 1) {
            if (vn <= kDotLimit) {
                dl->PrimReserve(static_cast<int>(vn * 6), static_cast<int>(vn * 4));
                int written = 0;
                for (size_t i = 0; i < vn; ++i) {
                    if (!proj[i].ok || !visibleAt(i)) continue;
                    const ImVec2& c = proj[i].s;
                    unsigned int base = dl->_VtxCurrentIdx;
                    dl->PrimWriteVtx(ImVec2(c.x - 2, c.y - 2), uv, prefs.vertexColor);
                    dl->PrimWriteVtx(ImVec2(c.x + 2, c.y - 2), uv, prefs.vertexColor);
                    dl->PrimWriteVtx(ImVec2(c.x + 2, c.y + 2), uv, prefs.vertexColor);
                    dl->PrimWriteVtx(ImVec2(c.x - 2, c.y + 2), uv, prefs.vertexColor);
                    dl->PrimWriteIdx((ImDrawIdx)base); dl->PrimWriteIdx((ImDrawIdx)(base + 1)); dl->PrimWriteIdx((ImDrawIdx)(base + 2));
                    dl->PrimWriteIdx((ImDrawIdx)base); dl->PrimWriteIdx((ImDrawIdx)(base + 2)); dl->PrimWriteIdx((ImDrawIdx)(base + 3));
                    ++written;
                }
                dl->PrimUnreserve(static_cast<int>(vn * 6 - written * 6), static_cast<int>(vn * 4 - written * 4));
            }
            for (uint32_t vi : obj.selVerts) if (vi < vn && proj[vi].ok && visibleAt(vi)) dl->AddCircleFilled(proj[vi].s, 4.2f, prefs.vertexSelected);
            if (pick.hit && proj[pick.vertex].ok) dl->AddCircle(proj[pick.vertex].s, 5.0f, prefs.vertexSelected, 0, 2.0f);
        }
    }
    // Object mode: selected objects get an orange outline, like Blender's selection highlight.
    if (!editMode && !retopoMode && obj.selected && !visible.empty() && shading != ShadingMode::Rendered) {
        ImU32 outline = obj.active ? IM_COL32(255, 160, 60, 235) : IM_COL32(220, 120, 40, 200);
        // Silhouette = edges shared by a front-facing and a back-facing triangle, or by only one.
        static std::unordered_map<uint64_t, int> edgeFaces;
        edgeFaces.clear();
        edgeFaces.reserve(visible.size() * 3);
        for (uint32_t vi : visible) {
            const Tri& t = mesh.tris[vi];
            edgeFaces[Mesh::EdgeKey(t.a, t.b)] += 1;
            edgeFaces[Mesh::EdgeKey(t.b, t.c)] += 1;
            edgeFaces[Mesh::EdgeKey(t.c, t.a)] += 1;
        }
        for (const auto& kv : edgeFaces) {
            if (kv.second != 1) continue;   // interior edge of the visible shell
            uint32_t a = uint32_t(kv.first >> 32), b = uint32_t(kv.first & 0xFFFFFFFF);
            if (proj[a].ok && proj[b].ok) dl->AddLine(proj[a].s, proj[b].s, outline, 2.0f);
        }
    }

    dl->Flags = savedFlags;
    return pick;
}

// Planar shadow: the silhouette projected onto y = 0 along the sun direction. Not a ray-traced
// shadow, but it grounds the object, which is most of what a "rendered" look needs.
void DrawGroundShadow(ImDrawList* dl, const Viewport3D& vp, MeshObject& obj, const ViewLighting& light) {
    if (!obj.visible || obj.mesh.positions.empty()) return;
    Float3 L = light.sunDir;
    float ll = std::sqrt(Dot3(L, L)); if (ll < 1e-6f) return;
    L = {L.x / ll, L.y / ll, L.z / ll};
    if (L.y < 0.15f) return;                     // sun at or below the horizon: no usable shadow

    const Mesh& mesh = obj.mesh;
    const size_t vn = mesh.positions.size();
    static std::vector<ImVec2> pts; static std::vector<char> ok;
    pts.resize(vn); ok.resize(vn);
    for (size_t i = 0; i < vn; ++i) {
        Float3 w = obj.transform.Apply(mesh.positions[i]);
        float t = w.y / L.y;                     // drop each point onto the ground along the sun
        Float3 g{w.x - L.x * t, 0.0f, w.z - L.z * t};
        float z; ImVec2 s;
        ok[i] = vp.ProjectPoint(g, z, s) ? 1 : 0;
        pts[i] = s;
    }
    ImDrawListFlags saved = dl->Flags;
    dl->Flags &= ~(ImDrawListFlags_AntiAliasedFill | ImDrawListFlags_AntiAliasedLines);
    const ImU32 col = IM_COL32(0, 0, 0, 38);     // layered triangles build up a soft pool
    const ImVec2 uv = dl->_Data->TexUvWhitePixel;
    dl->PrimReserve((int)mesh.tris.size() * 3, (int)mesh.tris.size() * 3);
    int written = 0;
    for (const Tri& t : mesh.tris) {
        if (!ok[t.a] || !ok[t.b] || !ok[t.c]) continue;
        unsigned int base = dl->_VtxCurrentIdx;
        dl->PrimWriteVtx(pts[t.a], uv, col);
        dl->PrimWriteVtx(pts[t.b], uv, col);
        dl->PrimWriteVtx(pts[t.c], uv, col);
        dl->PrimWriteIdx((ImDrawIdx)base); dl->PrimWriteIdx((ImDrawIdx)(base + 1)); dl->PrimWriteIdx((ImDrawIdx)(base + 2));
        ++written;
    }
    dl->PrimUnreserve(((int)mesh.tris.size() - written) * 3, ((int)mesh.tris.size() - written) * 3);
    dl->Flags = saved;
}

// Bones drawn as Blender-style octahedra, with the selected one highlighted.
void DrawArmature(ImDrawList* dl, const Viewport3D& vp, MeshObject& obj, int activeBone) {
    const Armature& arm = obj.armature;
    if (arm.bones.empty()) return;
    std::vector<Mat4> mats;
    arm.Evaluate(mats);
    for (size_t i = 0; i < arm.bones.size(); ++i) {
        const Bone& b = arm.bones[i];
        Float3 head = obj.transform.Apply(mats[i].Apply(b.head));
        Float3 tail = obj.transform.Apply(mats[i].Apply(b.tail));
        float z0, z1; ImVec2 s0, s1;
        if (!vp.ProjectPoint(head, z0, s0) || !vp.ProjectPoint(tail, z1, s1)) continue;
        bool sel = (int)i == activeBone || b.selected;
        ImU32 col = sel ? IM_COL32(255, 170, 60, 240) : IM_COL32(190, 200, 220, 200);

        ImVec2 d(s1.x - s0.x, s1.y - s0.y);
        float len = std::sqrt(d.x * d.x + d.y * d.y);
        if (len < 2.0f) { dl->AddCircle(s0, 4.0f, col, 12, 1.6f); continue; }
        ImVec2 dir(d.x / len, d.y / len), nrm(-dir.y, dir.x);
        float w = std::clamp(len * 0.12f, 3.0f, 14.0f);
        ImVec2 a(s0.x + dir.x * len * 0.18f + nrm.x * w, s0.y + dir.y * len * 0.18f + nrm.y * w);
        ImVec2 c(s0.x + dir.x * len * 0.18f - nrm.x * w, s0.y + dir.y * len * 0.18f - nrm.y * w);
        dl->AddLine(s0, a, col, 1.6f); dl->AddLine(a, s1, col, 1.6f);
        dl->AddLine(s0, c, col, 1.6f); dl->AddLine(c, s1, col, 1.6f);
        dl->AddCircleFilled(s0, 3.0f, col);
        if (sel) dl->AddCircle(s1, 4.5f, col, 12, 1.6f);
    }
}
