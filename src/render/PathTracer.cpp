#include "render/PathTracer.h"
#include "stb_image_write.h"
#include "stb_image.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <random>

bool HdriImage::Load(const std::string& path, std::string& error) {
    int w = 0, h = 0, ch = 0;
    // HDR files come back as linear float; LDR images are converted from sRGB below.
    if (stbi_is_hdr(path.c_str())) {
        float* data = stbi_loadf(path.c_str(), &w, &h, &ch, 3);
        if (!data) { error = stbi_failure_reason(); return false; }
        pixels.assign(data, data + (size_t)w * h * 3);
        stbi_image_free(data);
    } else {
        unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, 3);
        if (!data) { error = stbi_failure_reason(); return false; }
        pixels.resize((size_t)w * h * 3);
        for (size_t i = 0; i < pixels.size(); ++i) {
            float v = data[i] / 255.0f;
            pixels[i] = v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
        }
        stbi_image_free(data);
    }
    width = w; height = h;
    return true;
}

void HdriImage::Sample(float dx, float dy, float dz, float rotation, float out[3]) const {
    if (!Loaded()) { out[0] = out[1] = out[2] = 0.0f; return; }
    float u = std::atan2(dz, dx) + rotation;
    while (u < 0) u += 6.2831853f;
    while (u > 6.2831853f) u -= 6.2831853f;
    u /= 6.2831853f;
    float v = std::acos(std::clamp(dy, -1.0f, 1.0f)) / 3.14159265f;
    int x = std::clamp((int)(u * width), 0, width - 1);
    int y = std::clamp((int)(v * height), 0, height - 1);
    size_t i = ((size_t)y * width + x) * 3;
    out[0] = pixels[i]; out[1] = pixels[i + 1]; out[2] = pixels[i + 2];
}

namespace {

struct V3 {
    float x = 0, y = 0, z = 0;
    V3() = default;
    V3(float a, float b, float c) : x(a), y(b), z(c) {}
    V3(const Float3& f) : x(f.x), y(f.y), z(f.z) {}
    V3 operator+(const V3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    V3 operator-(const V3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    V3 operator*(float s) const { return {x * s, y * s, z * s}; }
    V3 operator*(const V3& o) const { return {x * o.x, y * o.y, z * o.z}; }
};
inline float Dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 Cross(const V3& a, const V3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float Len(const V3& a) { return std::sqrt(Dot(a, a)); }
inline V3 Norm(const V3& a) { float l = Len(a); return l > 1e-12f ? a * (1.0f / l) : V3{0, 1, 0}; }

struct RTTri {
    V3 a, b, c;
    V3 na, nb, nc;
    float ua = 0, va = 0, ub = 0, vb = 0, uc = 0, vc = 0;
    const Material* mat = nullptr;
    V3 baseColor;
    V3 emission;
    float roughness = 0.5f, metallic = 0.0f;
    float transmission = 0.0f, ior = 1.45f;
    float sss = 0.0f;
    V3 sssTint{1, 1, 1};
    V3 tu, tv;                 // dP/du, dP/dv: the tangent frame normal maps perturb in
    V3 gn;                     // geometric normal
    bool light = false;        // in the NEE light list, so BSDF hits on it are MIS-weighted
};

struct Hit {
    float t = 1e30f;
    const RTTri* tri = nullptr;
    float u = 0, v = 0;
};

// --- BVH over triangle centroids; median split, which is plenty for viewport-scale scenes.
struct Node { V3 mn, mx; int left = -1, right = -1, start = 0, count = 0; };

struct BVH {
    std::vector<RTTri> tris;
    std::vector<int> index;
    std::vector<Node> nodes;
    std::vector<int> lights;          // emissive triangles, sampled by area
    std::vector<float> lightCdf;      // running area sum, parallel to lights
    float lightArea = 0.0f;

    void BuildLights() {
        for (int i = 0; i < (int)tris.size(); ++i) {
            if (!tris[i].light) continue;
            lightArea += 0.5f * Len(Cross(tris[i].b - tris[i].a, tris[i].c - tris[i].a));
            lights.push_back(i);
            lightCdf.push_back(lightArea);
        }
    }

    void Build() {
        index.resize(tris.size());
        for (size_t i = 0; i < index.size(); ++i) index[i] = (int)i;
        nodes.clear();
        nodes.reserve(tris.size() * 2 + 1);
        BuildNode(0, (int)index.size());
    }

    int BuildNode(int start, int count) {
        int id = (int)nodes.size();
        nodes.push_back({});
        V3 mn{1e30f, 1e30f, 1e30f}, mx{-1e30f, -1e30f, -1e30f};
        for (int i = start; i < start + count; ++i) {
            const RTTri& t = tris[index[i]];
            for (const V3& p : {t.a, t.b, t.c}) {
                mn = {std::min(mn.x, p.x), std::min(mn.y, p.y), std::min(mn.z, p.z)};
                mx = {std::max(mx.x, p.x), std::max(mx.y, p.y), std::max(mx.z, p.z)};
            }
        }
        nodes[id].mn = mn; nodes[id].mx = mx;
        if (count <= 4) { nodes[id].start = start; nodes[id].count = count; return id; }

        V3 ext = mx - mn;
        int axis = ext.x > ext.y ? (ext.x > ext.z ? 0 : 2) : (ext.y > ext.z ? 1 : 2);
        auto centroid = [&](int i) {
            const RTTri& t = tris[i];
            float c = axis == 0 ? (t.a.x + t.b.x + t.c.x) : axis == 1 ? (t.a.y + t.b.y + t.c.y) : (t.a.z + t.b.z + t.c.z);
            return c / 3.0f;
        };
        int mid = start + count / 2;
        std::nth_element(index.begin() + start, index.begin() + mid, index.begin() + start + count,
                         [&](int l, int r) { return centroid(l) < centroid(r); });
        int l = BuildNode(start, mid - start);
        int r = BuildNode(mid, start + count - mid);
        nodes[id].left = l; nodes[id].right = r; nodes[id].count = 0;
        return id;
    }

    static bool SlabHit(const Node& n, const V3& o, const V3& inv, float tmax) {
        float t0 = (n.mn.x - o.x) * inv.x, t1 = (n.mx.x - o.x) * inv.x;
        float tmin = std::min(t0, t1), tmx = std::max(t0, t1);
        t0 = (n.mn.y - o.y) * inv.y; t1 = (n.mx.y - o.y) * inv.y;
        tmin = std::max(tmin, std::min(t0, t1)); tmx = std::min(tmx, std::max(t0, t1));
        t0 = (n.mn.z - o.z) * inv.z; t1 = (n.mx.z - o.z) * inv.z;
        tmin = std::max(tmin, std::min(t0, t1)); tmx = std::min(tmx, std::max(t0, t1));
        return tmx >= std::max(0.0f, tmin) && tmin < tmax;
    }

    bool Trace(const V3& o, const V3& d, Hit& hit, float tmax = 1e30f) const {
        if (nodes.empty()) return false;
        V3 inv{1.0f / (d.x == 0 ? 1e-20f : d.x), 1.0f / (d.y == 0 ? 1e-20f : d.y), 1.0f / (d.z == 0 ? 1e-20f : d.z)};
        int stack[64], sp = 0;
        stack[sp++] = 0;
        bool found = false;
        while (sp) {
            const Node& n = nodes[stack[--sp]];
            if (!SlabHit(n, o, inv, hit.t)) continue;
            if (n.count > 0) {
                for (int i = n.start; i < n.start + n.count; ++i) {
                    const RTTri& t = tris[index[i]];
                    V3 e1 = t.b - t.a, e2 = t.c - t.a;
                    V3 pv = Cross(d, e2);
                    float det = Dot(e1, pv);
                    if (std::fabs(det) < 1e-12f) continue;
                    float invDet = 1.0f / det;
                    V3 tv = o - t.a;
                    float u = Dot(tv, pv) * invDet;
                    if (u < 0 || u > 1) continue;
                    V3 qv = Cross(tv, e1);
                    float v = Dot(d, qv) * invDet;
                    if (v < 0 || u + v > 1) continue;
                    float dist = Dot(e2, qv) * invDet;
                    if (dist > 1e-4f && dist < hit.t && dist < tmax) {
                        hit.t = dist; hit.tri = &t; hit.u = u; hit.v = v; found = true;
                    }
                }
            } else {
                if (n.left >= 0) stack[sp++] = n.left;
                if (n.right >= 0) stack[sp++] = n.right;
            }
        }
        return found;
    }

    bool Occluded(const V3& o, const V3& d, float dist) const {
        Hit h; h.t = dist;
        return Trace(o, d, h, dist);
    }
};

struct RNG {
    uint32_t s;
    explicit RNG(uint32_t seed) : s(seed ? seed : 1u) {}
    float operator()() {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (s & 0xFFFFFF) / 16777216.0f;
    }
};

V3 CosineHemisphere(const V3& n, RNG& rng) {
    float u1 = rng(), u2 = rng();
    float r = std::sqrt(u1), theta = 6.2831853f * u2;
    V3 up = std::fabs(n.y) < 0.95f ? V3{0, 1, 0} : V3{1, 0, 0};
    V3 tx = Norm(Cross(up, n)), ty = Cross(n, tx);
    return Norm(tx * (r * std::cos(theta)) + ty * (r * std::sin(theta)) + n * std::sqrt(std::max(0.0f, 1 - u1)));
}

V3 SkyColor(const V3& d, const RayTraceWorld& w) {
    if (w.hdri && w.hdri->Loaded()) {
        float c[3];
        w.hdri->Sample(d.x, d.y, d.z, w.hdriRotation, c);
        return V3{c[0], c[1], c[2]} * w.hdriStrength;
    }
    float t = std::clamp(d.y * 0.5f + 0.5f, 0.0f, 1.0f);
    V3 sky{w.skyColor[0], w.skyColor[1], w.skyColor[2]};
    V3 hor{w.horizonColor[0], w.horizonColor[1], w.horizonColor[2]};
    V3 c = w.background == 1 ? hor * (1 - t) + sky * t : sky;
    return c * w.skyStrength;
}

} // namespace

PathTracer::~PathTracer() { Cancel(); Join(); }

void PathTracer::Cancel() { cancel = true; }

void PathTracer::Join() {
    for (auto& t : workers) if (t.joinable()) t.join();
    workers.clear();
}

bool PathTracer::Start(const Scene& scene, const SceneCamera& camera, float viewYaw, float viewPitch,
                       float viewDistance, const Float3& viewTarget, bool useSceneCamera,
                       const RayTraceSettings& st, const RayTraceWorld& world) {
    if (running) return false;
    Join();
    cancel = false;
    progress = 0.0f;
    width = std::max(16, st.width);
    height = std::max(16, st.height);
    pixels.assign((size_t)width * height * 4, 0);
    nextRow = 0;

    // ---- flatten the scene into world-space triangles
    auto bvh = std::make_shared<BVH>();
    for (const auto& up : scene.objects) {
        const MeshObject& o = *up;
        if (!o.visible || o.isCurve) continue;
        const Mesh& mesh = o.hasDisplay ? o.displayMesh : o.mesh;
        if (mesh.tris.empty()) continue;
        Mesh shaded = mesh;
        if (shaded.normals.size() != shaded.positions.size()) shaded.ComputeNormals();
        for (const Tri& t : shaded.tris) {
            RTTri r;
            r.a = o.transform.Apply(shaded.positions[t.a]);
            r.b = o.transform.Apply(shaded.positions[t.b]);
            r.c = o.transform.Apply(shaded.positions[t.c]);
            // normals rotate with the object; for the preview, recomputing per face is enough
            V3 gn = Norm(Cross(r.b - r.a, r.c - r.a));
            r.na = r.nb = r.nc = gn;
            if (o.shadeSmooth) {
                r.na = Norm(V3(o.transform.Apply(shaded.normals[t.a])) - V3(o.transform.Apply({0, 0, 0})));
                r.nb = Norm(V3(o.transform.Apply(shaded.normals[t.b])) - V3(o.transform.Apply({0, 0, 0})));
                r.nc = Norm(V3(o.transform.Apply(shaded.normals[t.c])) - V3(o.transform.Apply({0, 0, 0})));
            }
            const Material& m = o.material;
            r.mat = &o.material;
            if (shaded.HasLoopUVs()) {
                size_t li = (size_t)(&t - shaded.tris.data()) * 3;
                r.ua = shaded.loopUVs[li].u;     r.va = shaded.loopUVs[li].v;
                r.ub = shaded.loopUVs[li + 1].u; r.vb = shaded.loopUVs[li + 1].v;
                r.uc = shaded.loopUVs[li + 2].u; r.vc = shaded.loopUVs[li + 2].v;
            }
            r.baseColor = {m.baseColor[0], m.baseColor[1], m.baseColor[2]};
            r.roughness = std::clamp(m.roughness, 0.02f, 1.0f);
            r.metallic = std::clamp(m.metallic, 0.0f, 1.0f);
            r.sss = m.sssWeight;
            r.sssTint = {m.sssRadius[0], m.sssRadius[1], m.sssRadius[2]};
            float mx = std::max({r.sssTint.x, r.sssTint.y, r.sssTint.z, 1e-4f});
            r.sssTint = r.sssTint * (1.0f / mx);
            r.emission = V3{m.emissionColor[0], m.emissionColor[1], m.emissionColor[2]} * m.emissiveStrengthRT;
            r.transmission = m.transmission;
            r.ior = m.transmissionIOR;
            r.gn = gn;
            {
                V3 e1 = r.b - r.a, e2 = r.c - r.a;
                float du1 = r.ub - r.ua, dv1 = r.vb - r.va, du2 = r.uc - r.ua, dv2 = r.vc - r.va;
                float det = du1 * dv2 - du2 * dv1;
                if (std::fabs(det) > 1e-12f) {
                    r.tu = (e1 * dv2 - e2 * dv1) * (1.0f / det);
                    r.tv = (e2 * du1 - e1 * du2) * (1.0f / det);
                }
            }
            // Only constant emitters with no cutouts are light-sampled; anything mapped stays
            // BSDF-only, which is still unbiased (its hits are just not MIS-weighted).
            r.light = (r.emission.x + r.emission.y + r.emission.z) > 0.0f && m.opacity >= 0.999f &&
                      !m.HasCpu(MapSlot::Emissive) && !m.HasCpu(MapSlot::Opacity) && !m.HasCpu(MapSlot::BaseColor);
            bvh->tris.push_back(r);
        }
    }
    if (bvh->tris.empty()) return false;
    bvh->Build();
    if (st.emissiveLights && st.lightSampling) bvh->BuildLights();
    stats.triangles = bvh->tris.size();

    // ---- camera basis
    float yaw = useSceneCamera ? camera.yaw : viewYaw;
    float pitch = useSceneCamera ? camera.pitch : viewPitch;
    float dist = useSceneCamera ? camera.distance : viewDistance;
    Float3 tgt = useSceneCamera ? camera.target : viewTarget;
    V3 target(tgt);
    V3 forward = Norm(V3{-std::sin(yaw) * std::cos(pitch), -std::sin(pitch), -std::cos(yaw) * std::cos(pitch)});
    V3 eye = target - forward * dist;
    V3 right = Norm(Cross(forward, V3{0, 1, 0}));
    V3 up = Cross(right, forward);
    float aspect = (float)width / height;
    float fovY = useSceneCamera ? camera.FovYDegrees(aspect) : 45.0f;
    float tanHalf = std::tan(fovY * 0.5f * 3.14159265f / 180.0f);

    auto settings = st;
    auto worldCopy = world;
    int threadCount = settings.threads > 0 ? settings.threads
                                           : std::max(1u, std::thread::hardware_concurrency());
    stats.threadsUsed = threadCount;
    running = true;
    auto started = std::chrono::steady_clock::now();
    auto startTime = std::make_shared<std::chrono::steady_clock::time_point>(started);

    for (int w = 0; w < threadCount; ++w) {
        workers.emplace_back([this, bvh, settings, worldCopy, eye, forward, right, up, tanHalf, aspect, startTime]() {
            V3 sun = Norm(V3(worldCopy.sunDir));
            const float sunRadius = std::tan(std::max(0.05f, settings.sunAngleDeg) * 3.14159265f / 180.0f);
            while (true) {
                int y = nextRow.fetch_add(1);
                if (y >= height || cancel) break;
                RNG rng((uint32_t)(y * 9781 + settings.seed * 6271 + 1));
                for (int x = 0; x < width; ++x) {
                    V3 sum{0, 0, 0};
                    for (int s = 0; s < settings.samples; ++s) {
                        float px = (x + rng()) / width * 2.0f - 1.0f;
                        float py = 1.0f - (y + rng()) / height * 2.0f;
                        V3 dir = Norm(forward + right * (px * tanHalf * aspect) + up * (py * tanHalf));
                        V3 origin = eye;
                        V3 throughput{1, 1, 1}, radiance{0, 0, 0};
                        bool misPrev = false;   // last bounce was the diffuse lobe, which NEE also covers
                        float pdfPrev = 0.0f;   // its solid-angle pdf

                        for (int bounce = 0; bounce <= settings.maxBounces; ++bounce) {
                            Hit hit;
                            if (!bvh->Trace(origin, dir, hit)) {
                                V3 sky = SkyColor(dir, worldCopy);
                                radiance = radiance + throughput * sky;
                                break;
                            }
                            const RTTri& t = *hit.tri;
                            float w0 = 1 - hit.u - hit.v;
                            float uvU = t.ua * w0 + t.ub * hit.u + t.uc * hit.v;
                            float uvV = t.va * w0 + t.vb * hit.u + t.vc * hit.v;

                            // Per-hit material values, taken from the maps where they exist.
                            V3 albedo = t.baseColor;
                            float rough = t.roughness, metal = t.metallic, alpha = 1.0f;
                            V3 emissive = t.emission;
                            float sssHere = t.sss, specular = 0.5f;
                            if (t.mat) {
                                const Material& M = *t.mat;
                                if (M.parallax && M.HasCpu(MapSlot::Height)) {
                                    // Parallax occlusion: march along the view direction in UV space
                                    // until the height map is no longer above the ray.
                                    V3 nrm = Norm(t.na);
                                    V3 up2 = std::fabs(nrm.y) < 0.95f ? V3{0, 1, 0} : V3{1, 0, 0};
                                    V3 tx = Norm(Cross(up2, nrm)), ty = Cross(nrm, tx);
                                    float vx = Dot(dir, tx), vy = Dot(dir, ty), vz = std::fabs(Dot(dir, nrm)) + 1e-3f;
                                    int steps = std::clamp(M.parallaxSteps, 4, 64);
                                    float layer = 1.0f / steps;
                                    float du = (vx / vz) * M.parallaxScale / steps;
                                    float dv = (vy / vz) * M.parallaxScale / steps;
                                    float depth = 0.0f, cu = uvU, cv = uvV;
                                    for (int st = 0; st < steps; ++st) {
                                        float h = 1.0f - M.cpu[(int)MapSlot::Height].SampleScalar(cu, cv);
                                        if (depth >= h) break;
                                        cu += du; cv += dv; depth += layer;
                                    }
                                    uvU = cu; uvV = cv;
                                }
                                if (M.HasCpu(MapSlot::BaseColor)) {
                                    float c4[4]; M.cpu[(int)MapSlot::BaseColor].Sample(uvU, uvV, c4);
                                    auto srgb = [](float x) { return x <= 0.04045f ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f); };
                                    albedo = V3{srgb(c4[0]), srgb(c4[1]), srgb(c4[2])} * V3{t.baseColor.x, t.baseColor.y, t.baseColor.z};
                                    alpha *= c4[3];
                                }
                                if (M.HasCpu(MapSlot::Roughness)) rough = std::clamp(M.cpu[(int)MapSlot::Roughness].SampleScalar(uvU, uvV), 0.02f, 1.0f);
                                if (M.HasCpu(MapSlot::Metallic))  metal = std::clamp(M.cpu[(int)MapSlot::Metallic].SampleScalar(uvU, uvV), 0.0f, 1.0f);
                                if (M.HasCpu(MapSlot::Specular))  specular = M.cpu[(int)MapSlot::Specular].SampleScalar(uvU, uvV);
                                else specular = M.specular;
                                if (M.HasCpu(MapSlot::Opacity))   alpha *= M.cpu[(int)MapSlot::Opacity].SampleScalar(uvU, uvV);
                                alpha *= M.opacity;
                                if (M.HasCpu(MapSlot::Subsurface)) sssHere = t.sss * M.cpu[(int)MapSlot::Subsurface].SampleScalar(uvU, uvV);
                                if (M.HasCpu(MapSlot::AO)) {
                                    float ao = M.cpu[(int)MapSlot::AO].SampleScalar(uvU, uvV);
                                    albedo = albedo * ao;
                                }
                                if (M.HasCpu(MapSlot::Emissive)) {
                                    float e4[4]; M.cpu[(int)MapSlot::Emissive].Sample(uvU, uvV, e4);
                                    emissive = V3{e4[0], e4[1], e4[2]} * M.emissiveStrengthRT;
                                }
                            }

                            // Opacity: let the ray pass straight through the transparent parts.
                            if (alpha < 0.999f && rng() > alpha) {
                                origin = origin + dir * (hit.t + 1e-3f);
                                misPrev = false;   // shadow rays treat cutouts as opaque, so NEE missed this light path
                                continue;
                            }

                            if (settings.emissiveLights && (emissive.x + emissive.y + emissive.z) > 0.0f) {
                                float weight = 1.0f;
                                if (misPrev && t.light && !bvh->lights.empty()) {
                                    float cosL = std::max(std::fabs(Dot(t.gn, dir)), 1e-6f);
                                    float pdfL = hit.t * hit.t / (cosL * bvh->lightArea);
                                    weight = pdfPrev * pdfPrev / (pdfPrev * pdfPrev + pdfL * pdfL);
                                }
                                radiance = radiance + throughput * emissive * weight;
                            }
                            V3 n = Norm(t.na * w0 + t.nb * hit.u + t.nc * hit.v);
                            bool backFace = Dot(n, dir) > 0;
                            if (t.mat && t.mat->HasCpu(MapSlot::Normal) && Dot(t.tu, t.tu) > 0.0f) {
                                // shortcut: assumes OpenGL (+Y) normal maps; DirectX ones need green flipped.
                                float c4[4]; t.mat->cpu[(int)MapSlot::Normal].Sample(uvU, uvV, c4);
                                V3 T = Norm(t.tu - n * Dot(n, t.tu));
                                V3 B = Cross(n, T);
                                if (Dot(B, t.tv) < 0) B = B * -1.0f;   // mirrored UVs
                                n = Norm(T * (c4[0] * 2 - 1) + B * (c4[1] * 2 - 1) + n * (c4[2] * 2 - 1));
                            }
                            if (backFace) n = n * -1.0f;
                            V3 p = origin + dir * hit.t;

                            // direct sun, with a soft disc for penumbra
                            V3 ls = Norm(sun + V3{(rng() - 0.5f) * sunRadius, (rng() - 0.5f) * sunRadius, (rng() - 0.5f) * sunRadius});
                            float ndl = Dot(n, ls);
                            if (ndl > 0) {
                                bool shadowed = settings.shadows && bvh->Occluded(p + n * 1e-3f, ls, 1e5f);
                                if (!shadowed) {
                                    V3 sunCol{worldCopy.sunColor[0], worldCopy.sunColor[1], worldCopy.sunColor[2]};
                                    V3 diffuse = albedo * (1.0f - metal);
                                    radiance = radiance + throughput * diffuse * sunCol * (worldCopy.sunStrength * ndl);
                                }
                            }
                            // subsurface: a cheap wrap term so skin and wax do not go black
                            if (sssHere > 0.001f) {
                                float wrap = std::max(0.0f, (Dot(n, ls) + 0.7f) / 1.7f);
                                radiance = radiance + throughput * t.sssTint * (sssHere * wrap * 0.4f * worldCopy.sunStrength);
                            }

                            if (bounce == settings.maxBounces) break;
                            float specChance = std::clamp(metal + (1.0f - metal) * specular * 0.08f, 0.0f, 1.0f);
                            // Probability of the diffuse lobe below, so its BRDF is albedo * pDiffuse / pi.
                            float pDiffuse = (1.0f - (t.transmission > 0.001f ? t.transmission : 0.0f)) * (1.0f - specChance);

                            // Next-event estimation: one emissive triangle picked by area, MIS-weighted
                            // against the diffuse lobe hitting it (power heuristic).
                            if (!bvh->lights.empty() && pDiffuse > 0.0f) {
                                float pick = rng() * bvh->lightArea;
                                size_t li = std::min(bvh->lights.size() - 1,
                                    (size_t)(std::upper_bound(bvh->lightCdf.begin(), bvh->lightCdf.end(), pick) - bvh->lightCdf.begin()));
                                const RTTri& L = bvh->tris[bvh->lights[li]];
                                float r1 = std::sqrt(rng()), r2 = rng();
                                V3 q = L.a * (1 - r1) + L.b * (r1 * (1 - r2)) + L.c * (r1 * r2);
                                V3 wi = q - p;
                                float d2 = Dot(wi, wi), dist = std::sqrt(d2);
                                wi = wi * (1.0f / std::max(dist, 1e-8f));
                                float cosS = Dot(n, wi), cosL = std::fabs(Dot(L.gn, wi));
                                if (cosS > 0 && cosL > 1e-6f && !bvh->Occluded(p + n * 1e-3f, wi, dist * 0.999f - 1e-3f)) {
                                    float pdfL = d2 / (cosL * bvh->lightArea);
                                    float pdfB = pDiffuse * cosS / 3.14159265f;
                                    float w = pdfL * pdfL / (pdfL * pdfL + pdfB * pdfB);
                                    radiance = radiance + throughput * albedo * L.emission * (pDiffuse / 3.14159265f * cosS / pdfL * w);
                                }
                            }

                            // next direction: cosine diffuse, or a rough mirror for metal/glossy
                            V3 next;
                            misPrev = false;
                            if (t.transmission > 0.001f && rng() < t.transmission) {
                                // refraction with Schlick reflectance; total internal reflection falls back to a mirror
                                bool entering = Dot(dir, n) < 0;
                                float eta = entering ? 1.0f / t.ior : t.ior;
                                V3 nn = entering ? n : n * -1.0f;
                                float cosi = std::clamp(-Dot(dir, nn), -1.0f, 1.0f);
                                float k = 1.0f - eta * eta * (1.0f - cosi * cosi);
                                float r0 = (1 - t.ior) / (1 + t.ior); r0 *= r0;
                                float fresnel = r0 + (1 - r0) * std::pow(1 - cosi, 5.0f);
                                if (k < 0 || rng() < fresnel) next = Norm(dir - nn * (2.0f * Dot(dir, nn)));
                                else next = Norm(dir * eta + nn * (eta * cosi - std::sqrt(k)));
                                throughput = throughput * t.baseColor;
                                origin = p + next * 1e-3f;
                                dir = next;
                                continue;
                            }
                            if (rng() < specChance) {
                                V3 refl = dir - n * (2.0f * Dot(dir, n));
                                next = Norm(refl + CosineHemisphere(n, rng) * (rough * rough));
                                // metals tint their reflection, dielectrics reflect white
                                throughput = throughput * (metal > 0.5f ? albedo : V3{1, 1, 1});
                            } else {
                                next = CosineHemisphere(n, rng);
                                throughput = throughput * albedo;
                                misPrev = true;
                                pdfPrev = pDiffuse * std::max(Dot(n, next), 1e-6f) / 3.14159265f;
                            }
                            if (settings.clampEnabled) {
                                throughput.x = std::min(throughput.x, settings.clampIndirect);
                                throughput.y = std::min(throughput.y, settings.clampIndirect);
                                throughput.z = std::min(throughput.z, settings.clampIndirect);
                            }
                            // Russian roulette once the path is dim
                            float q = std::max({throughput.x, throughput.y, throughput.z});
                            if (bounce > 2) {
                                if (rng() > q) break;
                                throughput = throughput * (1.0f / std::max(0.05f, q));
                            }
                            origin = p + n * 1e-3f;
                            dir = next;
                        }
                        sum = sum + radiance;
                    }
                    V3 c = sum * (1.0f / settings.samples) * settings.exposure;
                    // filmic-ish curve, then sRGB
                    auto tone = [](float v) {
                        v = 1.0f - std::exp(-std::max(0.0f, v));
                        return std::pow(v, 1.0f / 2.2f);
                    };
                    size_t i = ((size_t)y * width + x) * 4;
                    pixels[i + 0] = (uint8_t)std::clamp(tone(c.x) * 255.0f, 0.0f, 255.0f);
                    pixels[i + 1] = (uint8_t)std::clamp(tone(c.y) * 255.0f, 0.0f, 255.0f);
                    pixels[i + 2] = (uint8_t)std::clamp(tone(c.z) * 255.0f, 0.0f, 255.0f);
                    pixels[i + 3] = 255;
                }
                progress = std::min(1.0f, (float)nextRow.load() / height);
            }
            if (nextRow.load() >= height || cancel) {
                bool expected = true;
                if (running.compare_exchange_strong(expected, false)) {
                    if (settings.denoise && !cancel) Denoise(settings.denoiseStrength);
                    stats.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - *startTime).count();
                    progress = 1.0f;
                }
            }
        });
    }
    return true;
}

// A small edge-aware (bilateral) pass. Not OIDN, but it clears most of the sampling grain
// without smearing silhouettes, which is what makes low sample counts usable.
void PathTracer::Denoise(float strength) {
    if (pixels.empty() || strength <= 0.0f) return;
    std::vector<uint8_t> src = pixels;
    const int radius = 2;
    const float sigmaRange = 28.0f / std::max(0.2f, strength);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            size_t ci = ((size_t)y * width + x) * 4;
            float cr = src[ci], cg = src[ci + 1], cb = src[ci + 2];
            float accR = 0, accG = 0, accB = 0, wsum = 0;
            for (int dy = -radius; dy <= radius; ++dy) {
                int sy = y + dy;
                if (sy < 0 || sy >= height) continue;
                for (int dx = -radius; dx <= radius; ++dx) {
                    int sx = x + dx;
                    if (sx < 0 || sx >= width) continue;
                    size_t si = ((size_t)sy * width + sx) * 4;
                    float dr = src[si] - cr, dg = src[si + 1] - cg, db = src[si + 2] - cb;
                    float range = (dr * dr + dg * dg + db * db) / (3.0f * sigmaRange * sigmaRange);
                    float spatial = (float)(dx * dx + dy * dy) / (2.0f * radius * radius);
                    float w = std::exp(-range - spatial);
                    accR += src[si] * w; accG += src[si + 1] * w; accB += src[si + 2] * w;
                    wsum += w;
                }
            }
            if (wsum > 0) {
                pixels[ci] = (uint8_t)std::clamp(accR / wsum, 0.0f, 255.0f);
                pixels[ci + 1] = (uint8_t)std::clamp(accG / wsum, 0.0f, 255.0f);
                pixels[ci + 2] = (uint8_t)std::clamp(accB / wsum, 0.0f, 255.0f);
            }
        }
    }
}

bool PathTracer::SavePng(const std::string& path) const {
    if (pixels.empty()) return false;
    return stbi_write_png(path.c_str(), width, height, 4, pixels.data(), width * 4) != 0;
}
