#include "paint/Project.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include "stb_image.h"
#include "json.hpp"
#include "miniz.h"
#include "mesh/Scene.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

using nlohmann::json;

namespace {
const char* kMapKindIds[] = {"albedo", "normal", "roughness", "metallic", "ao",
                             "cavity", "height", "emissive", "opacity", "orm", "custom"};
const char* kBlendIds[] = {"normal", "multiply", "screen", "overlay", "darken", "lighten",
                           "color-dodge", "color-burn", "hard-light", "soft-light", "difference",
                           "exclusion", "hue", "saturation", "color", "luminosity"};

MapKind KindFromId(const std::string& s) {
    for (int i = 0; i < (int)MapKind::Count; ++i) if (s == kMapKindIds[i]) return (MapKind)i;
    return MapKind::Custom;
}
Blend BlendFromId(const std::string& s) {
    for (int i = 0; i < (int)Blend::Count; ++i) if (s == kBlendIds[i]) return (Blend)i;
    return Blend::Normal;
}

// stb_image_write into memory
void PngWriter(void* ctx, void* data, int size) {
    auto* out = static_cast<std::vector<unsigned char>*>(ctx);
    out->insert(out->end(), (unsigned char*)data, (unsigned char*)data + size);
}
bool EncodePng(const Layer& l, int w, int h, std::vector<unsigned char>& out) {
    out.clear();
    if (l.pixels.size() != (size_t)w * h * 4) return false;
    return stbi_write_png_to_func(PngWriter, &out, w, h, 4, l.pixels.data(), w * 4) != 0;
}
// ---------------------------------------------------------------- 3D scene
// Per object: parameters in manifest.json, the heavy arrays in objects/O{n}.bin, textures as
// objects/O{n}_{slot}.png. Every value read back is checked, so a damaged file can't crash us.
constexpr uint32_t kBlobMagic = 0x424F584Cu;   // "LXOB"
constexpr size_t   kMaxEntry = 512u << 20;     // refuse zip entries that unpack beyond this

struct BlobOut {
    std::vector<unsigned char> b;
    void Raw(const void* p, size_t n) { b.insert(b.end(), (const unsigned char*)p, (const unsigned char*)p + n); }
    void U32(uint32_t v) { Raw(&v, 4); }
    template <class T> void Vec(const std::vector<T>& v) { U32((uint32_t)v.size()); if (!v.empty()) Raw(v.data(), v.size() * sizeof(T)); }
};
struct BlobIn {
    const unsigned char* p; size_t left; bool ok = true;
    bool Raw(void* out, size_t n) { if (!ok || n > left) return ok = false; std::memcpy(out, p, n); p += n; left -= n; return true; }
    uint32_t U32() { uint32_t v = 0; Raw(&v, 4); return v; }
    template <class T> void Vec(std::vector<T>& v) {
        uint32_t n = U32();
        if (!ok || (size_t)n > left / sizeof(T)) { ok = false; v.clear(); return; }
        v.resize(n);
        if (n) Raw(v.data(), (size_t)n * sizeof(T));
    }
};

bool ReadEntry(mz_zip_archive& zip, const std::string& name, std::vector<unsigned char>& out) {
    out.clear();
    int at = mz_zip_reader_locate_file(&zip, name.c_str(), nullptr, 0);
    mz_zip_archive_file_stat st{};
    if (at < 0 || !mz_zip_reader_file_stat(&zip, (mz_uint)at, &st) || st.m_uncomp_size > kMaxEntry) return false;
    size_t n = 0;
    void* d = mz_zip_reader_extract_to_heap(&zip, (mz_uint)at, &n, 0);
    if (!d) return false;
    out.assign((unsigned char*)d, (unsigned char*)d + n);
    mz_free(d);
    return true;
}

json J3(const Float3& v) { return json::array({v.x, v.y, v.z}); }
float F(const json& j, const char* k, float d, float lo = -1e9f, float hi = 1e9f) {
    auto it = j.find(k);
    if (it == j.end() || !it->is_number()) return d;
    float v = it->get<float>();
    return std::isfinite(v) ? std::clamp(v, lo, hi) : d;
}
int I(const json& j, const char* k, int d, int lo, int hi) {
    auto it = j.find(k);
    if (it == j.end() || !it->is_number()) return d;
    double v = it->get<double>();
    return std::isfinite(v) ? (int)std::clamp(v, (double)lo, (double)hi) : d;
}
bool B(const json& j, const char* k, bool d) { auto it = j.find(k); return it != j.end() && it->is_boolean() ? it->get<bool>() : d; }
std::string S(const json& j, const char* k, const std::string& d) { auto it = j.find(k); return it != j.end() && it->is_string() ? it->get<std::string>() : d; }
Float3 V3(const json& j, const char* k, Float3 d) {
    auto it = j.find(k);
    if (it == j.end() || !it->is_array() || it->size() != 3) return d;
    float c[3] = {d.x, d.y, d.z};
    for (int i = 0; i < 3; ++i) if ((*it)[i].is_number()) { float v = (*it)[i].get<float>(); if (std::isfinite(v)) c[i] = std::clamp(v, -1e9f, 1e9f); }
    return {c[0], c[1], c[2]};
}
void Arr3(const json& j, const char* k, float* dst) { Float3 v = V3(j, k, {dst[0], dst[1], dst[2]}); dst[0] = v.x; dst[1] = v.y; dst[2] = v.z; }
const json& Obj(const json& j, const char* k) { static const json empty = json::object(); auto it = j.find(k); return it != j.end() && it->is_object() ? *it : empty; }
const json& Arr(const json& j, const char* k) { static const json empty = json::array(); auto it = j.find(k); return it != j.end() && it->is_array() ? *it : empty; }

json SaveMaterial(const Material& m) {
    return {{"baseColor", json::array({m.baseColor[0], m.baseColor[1], m.baseColor[2]})},
            {"roughness", m.roughness}, {"metallic", m.metallic}, {"emissiveStrength", m.emissiveStrength},
            {"emissionColor", json::array({m.emissionColor[0], m.emissionColor[1], m.emissionColor[2]})},
            {"emissiveStrengthRT", m.emissiveStrengthRT}, {"transmission", m.transmission}, {"transmissionIOR", m.transmissionIOR},
            {"sssWeight", m.sssWeight}, {"sssRadius", json::array({m.sssRadius[0], m.sssRadius[1], m.sssRadius[2]})},
            {"sssScale", m.sssScale}, {"sssAnisotropy", m.sssAnisotropy}, {"sssIOR", m.sssIOR}, {"sssMethod", m.sssMethod},
            {"useTextures", m.useTextures}, {"parallax", m.parallax}, {"parallaxScale", m.parallaxScale},
            {"parallaxSteps", m.parallaxSteps}, {"specular", m.specular}, {"opacity", m.opacity}};
}
void LoadMaterial(const json& j, Material& m) {
    Arr3(j, "baseColor", m.baseColor);
    m.roughness = F(j, "roughness", m.roughness, 0, 1);
    m.metallic = F(j, "metallic", m.metallic, 0, 1);
    m.emissiveStrength = F(j, "emissiveStrength", m.emissiveStrength, 0, 1e6f);
    Arr3(j, "emissionColor", m.emissionColor);
    m.emissiveStrengthRT = F(j, "emissiveStrengthRT", m.emissiveStrengthRT, 0, 1e6f);
    m.transmission = F(j, "transmission", m.transmission, 0, 1);
    m.transmissionIOR = F(j, "transmissionIOR", m.transmissionIOR, 1, 4);
    m.sssWeight = F(j, "sssWeight", m.sssWeight, 0, 1);
    Arr3(j, "sssRadius", m.sssRadius);
    m.sssScale = F(j, "sssScale", m.sssScale, 0, 100);
    m.sssAnisotropy = F(j, "sssAnisotropy", m.sssAnisotropy, -1, 1);
    m.sssIOR = F(j, "sssIOR", m.sssIOR, 1, 4);
    m.sssMethod = I(j, "sssMethod", m.sssMethod, 0, 1);
    m.useTextures = B(j, "useTextures", m.useTextures);
    m.parallax = B(j, "parallax", m.parallax);
    m.parallaxScale = F(j, "parallaxScale", m.parallaxScale, 0, 1);
    m.parallaxSteps = I(j, "parallaxSteps", m.parallaxSteps, 1, 256);
    m.specular = F(j, "specular", m.specular, 0, 1);
    m.opacity = F(j, "opacity", m.opacity, 0, 1);
}

json SaveModifier(const Modifier& m) {
    return {{"type", (int)m.type}, {"enabled", m.enabled}, {"showInEditMode", m.showInEditMode},
            {"arrayCount", m.arrayCount}, {"arrayOffset", json::array({m.arrayOffset[0], m.arrayOffset[1], m.arrayOffset[2]})},
            {"mirrorX", m.mirrorX}, {"mirrorY", m.mirrorY}, {"mirrorZ", m.mirrorZ}, {"mirrorMerge", m.mirrorMerge},
            {"mirrorThreshold", m.mirrorThreshold}, {"solidifyThickness", m.solidifyThickness}, {"solidifyRim", m.solidifyRim},
            {"subdivLevels", m.subdivLevels}, {"subdivSmooth", m.subdivSmooth}, {"smoothFactor", m.smoothFactor},
            {"smoothIterations", m.smoothIterations}, {"weldDistance", m.weldDistance}, {"shrinkTarget", m.shrinkTarget},
            {"shrinkOffset", m.shrinkOffset}, {"screwSteps", m.screwSteps}, {"screwAngle", m.screwAngle},
            {"screwOffset", m.screwOffset}, {"screwAxis", m.screwAxis}, {"remeshVoxel", m.remeshVoxel},
            {"remeshSmooth", m.remeshSmooth}, {"displaceStrength", m.displaceStrength}, {"displaceMidLevel", m.displaceMidLevel},
            {"displaceNoiseScale", m.displaceNoiseScale}, {"displaceUseNoise", m.displaceUseNoise},
            {"decimateRatio", m.decimateRatio}, {"curveTarget", m.curveTarget}, {"curveAxis", m.curveAxis},
            {"curveStretch", m.curveStretch}};
}
// Ranges keep a hostile file from asking for a billion array copies or a 1e-9 voxel remesh.
Modifier LoadModifier(const json& j) {
    Modifier m;
    m.type = (ModifierType)I(j, "type", 0, 0, (int)ModifierType::Count - 1);
    m.enabled = B(j, "enabled", m.enabled);
    m.showInEditMode = B(j, "showInEditMode", m.showInEditMode);
    m.arrayCount = I(j, "arrayCount", m.arrayCount, 1, 1000);
    Arr3(j, "arrayOffset", m.arrayOffset);
    m.mirrorX = B(j, "mirrorX", m.mirrorX); m.mirrorY = B(j, "mirrorY", m.mirrorY); m.mirrorZ = B(j, "mirrorZ", m.mirrorZ);
    m.mirrorMerge = B(j, "mirrorMerge", m.mirrorMerge);
    m.mirrorThreshold = F(j, "mirrorThreshold", m.mirrorThreshold, 0, 10);
    m.solidifyThickness = F(j, "solidifyThickness", m.solidifyThickness, -100, 100);
    m.solidifyRim = B(j, "solidifyRim", m.solidifyRim);
    m.subdivLevels = I(j, "subdivLevels", m.subdivLevels, 0, 6);
    m.subdivSmooth = B(j, "subdivSmooth", m.subdivSmooth);
    m.smoothFactor = F(j, "smoothFactor", m.smoothFactor, -10, 10);
    m.smoothIterations = I(j, "smoothIterations", m.smoothIterations, 0, 200);
    m.weldDistance = F(j, "weldDistance", m.weldDistance, 0, 100);
    m.shrinkTarget = I(j, "shrinkTarget", m.shrinkTarget, -1, 1 << 20);
    m.shrinkOffset = F(j, "shrinkOffset", m.shrinkOffset, -100, 100);
    m.screwSteps = I(j, "screwSteps", m.screwSteps, 1, 1024);
    m.screwAngle = F(j, "screwAngle", m.screwAngle, -36000, 36000);
    m.screwOffset = F(j, "screwOffset", m.screwOffset, -1000, 1000);
    m.screwAxis = I(j, "screwAxis", m.screwAxis, 0, 2);
    m.remeshVoxel = F(j, "remeshVoxel", m.remeshVoxel, 0.005f, 100);
    m.remeshSmooth = I(j, "remeshSmooth", m.remeshSmooth, 0, 50);
    m.displaceStrength = F(j, "displaceStrength", m.displaceStrength, -100, 100);
    m.displaceMidLevel = F(j, "displaceMidLevel", m.displaceMidLevel, -10, 10);
    m.displaceNoiseScale = F(j, "displaceNoiseScale", m.displaceNoiseScale, 0, 1000);
    m.displaceUseNoise = B(j, "displaceUseNoise", m.displaceUseNoise);
    m.decimateRatio = F(j, "decimateRatio", m.decimateRatio, 0, 1);
    m.curveTarget = I(j, "curveTarget", m.curveTarget, -1, 1 << 20);
    m.curveAxis = I(j, "curveAxis", m.curveAxis, 0, 2);
    m.curveStretch = F(j, "curveStretch", m.curveStretch, -100, 100);
    return m;
}

json SaveArmature(const Armature& a) {
    json bones = json::array();
    for (const Bone& b : a.bones) {
        json cons = json::array();
        for (const BoneConstraint& c : b.constraints)
            cons.push_back({{"type", (int)c.type}, {"enabled", c.enabled}, {"target", c.target}, {"point", J3(c.point)},
                            {"usePole", c.usePole}, {"poleTarget", c.poleTarget}, {"polePoint", J3(c.polePoint)},
                            {"limit", json::array({c.limit[0], c.limit[1], c.limit[2]})},
                            {"minRot", J3(c.minRot)}, {"maxRot", J3(c.maxRot)},
                            {"targetObject", c.targetObject}, {"poleObject", c.poleObject}});
        bones.push_back({{"name", b.name}, {"parent", b.parent}, {"head", J3(b.head)}, {"tail", J3(b.tail)},
                         {"poseRotation", J3(b.poseRotation)}, {"poseLocation", J3(b.poseLocation)},
                         {"poseScale", J3(b.poseScale)}, {"envelope", b.envelope}, {"deform", b.deform},
                         {"constraints", cons}});
    }
    return {{"enabled", a.enabled}, {"bones", bones}};
}
void LoadArmature(const json& j, Armature& a) {
    a.enabled = B(j, "enabled", true);
    const json& jb = Arr(j, "bones");
    const int n = (int)std::min<size_t>(jb.size(), 4096);
    for (int i = 0; i < n; ++i) {
        const json& x = jb[i];
        if (!x.is_object()) continue;
        Bone b;
        b.name = S(x, "name", "Bone");
        b.parent = I(x, "parent", -1, -1, 1 << 20);
        b.head = V3(x, "head", b.head); b.tail = V3(x, "tail", b.tail);
        b.poseRotation = V3(x, "poseRotation", b.poseRotation);
        b.poseLocation = V3(x, "poseLocation", b.poseLocation);
        b.poseScale = V3(x, "poseScale", b.poseScale);
        b.envelope = F(x, "envelope", b.envelope, 0, 1000);
        b.deform = B(x, "deform", b.deform);
        for (const json& y : Arr(x, "constraints")) {
            if (!y.is_object() || b.constraints.size() >= 64) continue;
            BoneConstraint c;
            c.type = (BoneConstraint::Type)I(y, "type", (int)c.type, 0, (int)BoneConstraint::IK);
            c.enabled = B(y, "enabled", c.enabled);
            c.target = I(y, "target", -1, -1, 1 << 20);
            c.point = V3(y, "point", c.point);
            c.usePole = B(y, "usePole", c.usePole);
            c.poleTarget = I(y, "poleTarget", -1, -1, 1 << 20);
            c.polePoint = V3(y, "polePoint", c.polePoint);
            const json& lim = Arr(y, "limit");
            for (int k = 0; k < 3 && k < (int)lim.size(); ++k) if (lim[k].is_boolean()) c.limit[k] = lim[k].get<bool>();
            c.minRot = V3(y, "minRot", c.minRot); c.maxRot = V3(y, "maxRot", c.maxRot);
            c.targetObject = (uint32_t)I(y, "targetObject", 0, 0, 1 << 30);   // checked once every object is loaded
            c.poleObject = (uint32_t)I(y, "poleObject", 0, 0, 1 << 30);
            b.constraints.push_back(c);
        }
        a.bones.push_back(std::move(b));
    }
    // indices must point at bones that exist; parents must come first, as Evaluate assumes
    const int nb = (int)a.bones.size();
    for (int i = 0; i < nb; ++i) {
        Bone& b = a.bones[i];
        if (b.parent >= i) b.parent = -1;
        for (BoneConstraint& c : b.constraints) {
            if (c.target >= nb) c.target = -1;
            if (c.poleTarget >= nb) c.poleTarget = -1;
        }
    }
}

json SaveAction(const Action& a) {
    json chans = json::array();
    for (const Channel& c : a.channels) {
        json keys = json::array();
        for (const Keyframe& k : c.keys) keys.push_back(json::array({k.frame, k.value, (int)k.interp, (int)k.easing}));
        chans.push_back({{"target", (int)c.target}, {"bone", c.boneIndex}, {"prop", (int)c.prop}, {"axis", c.axis}, {"keys", keys}});
    }
    return {{"name", a.name}, {"frameStart", a.frameStart}, {"frameEnd", a.frameEnd}, {"channels", chans}};
}
void LoadAction(const json& j, Action& a, int boneCount) {
    a.name = S(j, "name", a.name);
    a.frameStart = I(j, "frameStart", a.frameStart, -1000000, 1000000);
    a.frameEnd = std::max(a.frameStart, I(j, "frameEnd", a.frameEnd, -1000000, 1000000));
    for (const json& x : Arr(j, "channels")) {
        if (!x.is_object() || a.channels.size() >= 100000) continue;
        Channel c;
        c.target = (ChannelTarget)I(x, "target", 0, 0, 1);
        c.boneIndex = I(x, "bone", -1, -1, 1 << 20);
        if (c.target == ChannelTarget::Bone && c.boneIndex >= boneCount) continue;   // its bone is gone
        c.prop = (ChannelProp)I(x, "prop", 0, 0, 2);
        c.axis = I(x, "axis", 0, 0, 2);
        for (const json& k : Arr(x, "keys")) {
            if (!k.is_array() || k.size() < 2 || !k[0].is_number() || !k[1].is_number()) continue;
            Keyframe kf;
            double fr = k[0].get<double>(); float v = k[1].get<float>();
            if (!std::isfinite(fr) || !std::isfinite(v)) continue;
            kf.frame = (int)std::clamp(fr, -1e6, 1e6); kf.value = v;
            if (k.size() > 2 && k[2].is_number_integer()) kf.interp = (Interp)std::clamp(k[2].get<int>(), 0, (int)Interp::Count - 1);
            if (k.size() > 3 && k[3].is_number_integer()) kf.easing = (Easing)std::clamp(k[3].get<int>(), 0, 2);
            c.keys.push_back(kf);
        }
        std::sort(c.keys.begin(), c.keys.end(), [](const Keyframe& p, const Keyframe& q) { return p.frame < q.frame; });
        c.keys.erase(std::unique(c.keys.begin(), c.keys.end(), [](const Keyframe& p, const Keyframe& q) { return p.frame == q.frame; }), c.keys.end());
        a.channels.push_back(std::move(c));
    }
}

json SaveCamera(const SceneCamera& c) {
    return {{"target", J3(Float3{c.target.x, c.target.y, c.target.z})}, {"yaw", c.yaw}, {"pitch", c.pitch}, {"distance", c.distance},
            {"orthographic", c.orthographic}, {"orthoScale", c.orthoScale}, {"focalLengthMm", c.focalLengthMm},
            {"sensorWidthMm", c.sensorWidthMm}, {"clipStart", c.clipStart}, {"clipEnd", c.clipEnd}, {"dof", c.dof},
            {"fStop", c.fStop}, {"focusDistance", c.focusDistance}, {"showPassepartout", c.showPassepartout},
            {"passepartoutAlpha", c.passepartoutAlpha}};
}
SceneCamera LoadCamera(const json& j) {
    SceneCamera c;
    Float3 t = V3(j, "target", Float3{c.target.x, c.target.y, c.target.z});
    c.target = {t.x, t.y, t.z};
    c.yaw = F(j, "yaw", c.yaw); c.pitch = F(j, "pitch", c.pitch, -1.57f, 1.57f);
    c.distance = F(j, "distance", c.distance, 0.001f, 1e6f);
    c.orthographic = B(j, "orthographic", c.orthographic);
    c.orthoScale = F(j, "orthoScale", c.orthoScale, 0.001f, 1e6f);
    c.focalLengthMm = F(j, "focalLengthMm", c.focalLengthMm, 1, 5000);
    c.sensorWidthMm = F(j, "sensorWidthMm", c.sensorWidthMm, 1, 200);
    c.clipStart = F(j, "clipStart", c.clipStart, 1e-5f, 1e5f);
    c.clipEnd = std::max(c.clipStart * 2, F(j, "clipEnd", c.clipEnd, 1e-4f, 1e7f));
    c.dof = B(j, "dof", c.dof);
    c.fStop = F(j, "fStop", c.fStop, 0.1f, 128);
    c.focusDistance = F(j, "focusDistance", c.focusDistance, 0.001f, 1e6f);
    c.showPassepartout = B(j, "showPassepartout", c.showPassepartout);
    c.passepartoutAlpha = F(j, "passepartoutAlpha", c.passepartoutAlpha, 0, 1);
    return c;
}

json SaveScene(mz_zip_archive& zip, const Scene& scene, const PlaybackState* playback, ProjectSaveInfo& info) {
    json objs = json::array();
    for (size_t n = 0; n < scene.objects.size(); ++n) {
        const MeshObject& o = *scene.objects[n];
        const Mesh& m = o.mesh;
        const std::string base = "objects/O" + std::to_string(n + 1);
        BlobOut blob;
        blob.U32(kBlobMagic); blob.U32(1);
        blob.Vec(m.positions); blob.Vec(m.normals); blob.Vec(m.uvs); blob.Vec(m.loopUVs); blob.Vec(m.tris);
        blob.U32(m.polyCount);
        blob.Vec(std::vector<uint64_t>(m.seamEdges.begin(), m.seamEdges.end()));
        blob.Vec(std::vector<uint64_t>(m.sharpEdges.begin(), m.sharpEdges.end()));
        blob.Vec(o.armature.restPositions); blob.Vec(o.armature.weights);
        blob.Vec(o.multires.valid ? o.multires.displacement : std::vector<Float3>{});
        mz_zip_writer_add_mem(&zip, (base + ".bin").c_str(), blob.b.data(), blob.b.size(), MZ_BEST_SPEED);
        info.bytes += blob.b.size();

        json maps = json::array();
        for (int i = 0; i < (int)MapSlot::Count; ++i) {
            const TextureData& t = o.material.cpu[i];
            if (!t.Valid() || t.pixels.size() != (size_t)t.width * t.height * 4) continue;
            std::vector<unsigned char> png;
            if (!stbi_write_png_to_func(PngWriter, &png, t.width, t.height, 4, t.pixels.data(), t.width * 4)) continue;
            std::string file = base + "_map" + std::to_string(i) + ".png";
            mz_zip_writer_add_mem(&zip, file.c_str(), png.data(), png.size(), MZ_BEST_SPEED);
            info.bytes += png.size();
            maps.push_back({{"slot", i}, {"label", o.material.paths[i]}, {"file", file}});
        }
        json jm = SaveMaterial(o.material);
        jm["maps"] = maps;

        json mods = json::array();
        for (const Modifier& md : o.modifiers) mods.push_back(SaveModifier(md));
        json curvePts = json::array(), curveL = json::array(), curveR = json::array(), curveT = json::array(), curveRad = json::array();
        CurvePath cv = o.curve;
        cv.Sync();
        for (size_t i = 0; i < cv.points.size(); ++i) {
            curvePts.push_back(J3(cv.points[i]));
            curveL.push_back(J3(cv.handleL[i]));
            curveR.push_back(J3(cv.handleR[i]));
            curveT.push_back((int)cv.handleType[i]);
            curveRad.push_back(cv.radius[i]);
        }

        objs.push_back({{"name", o.name}, {"visible", o.visible}, {"shadeSmooth", o.shadeSmooth},
                        // a baked rigid body is saved where it starts; the bake itself is not stored
                        {"transform", {{"position", J3(o.rigidBody.Baked() ? o.rigidBody.rest.position : o.transform.position)},
                                       {"rotation", J3(o.rigidBody.Baked() ? o.rigidBody.rest.rotation : o.transform.rotation)},
                                       {"scale", J3(o.transform.scale)}}},
                        {"data", base + ".bin"}, {"material", jm}, {"modifiers", mods},
                        {"isCurve", o.isCurve},
                        {"id", o.id}, {"armatureObject", o.armatureObject}, {"isControl", o.isControl},
                        {"curve", {{"points", curvePts}, {"handleL", curveL}, {"handleR", curveR}, {"handleType", curveT}, {"radius", curveRad},
                                   {"cyclic", o.curve.cyclic}, {"resolution", o.curve.resolution},
                                   {"bevelDepth", o.curve.bevelDepth}, {"bevelResolution", o.curve.bevelResolution}}},
                        {"multires", {{"levels", o.multires.valid ? o.multires.levels : 0}, {"viewLevel", o.multires.viewLevel}}},
                        {"armature", SaveArmature(o.armature)}, {"action", SaveAction(o.action)},
                        {"rigidBody", {{"enabled", o.rigidBody.enabled}, {"passive", o.rigidBody.passive},
                                       {"mass", o.rigidBody.mass}, {"friction", o.rigidBody.friction},
                                       {"bounciness", o.rigidBody.bounciness}, {"shape", (int)o.rigidBody.shape}}}});
    }
    json cams = json::array();
    for (size_t i = 0; i < scene.cameras.size(); ++i)
        cams.push_back({{"name", i < scene.cameraNames.size() ? scene.cameraNames[i] : std::string("Camera")},
                        {"camera", SaveCamera(scene.cameras[i])}});
    json js = {{"objects", objs}, {"activeObject", scene.activeIndex}, {"cameras", cams}, {"activeCamera", scene.activeCamera}};
    if (playback) js["playback"] = {{"fps", playback->fps}, {"frame", playback->frame}, {"loop", playback->loop}};
    return js;
}

// Weights must name bones that exist and hold finite values.
void ClampWeights(Armature& a, int nb) {
    for (VertexWeights& w : a.weights)
        for (int k = 0; k < 4; ++k)
            if (w.bone[k] >= nb || w.bone[k] < -1 || !std::isfinite(w.weight[k])) { w.bone[k] = -1; w.weight[k] = 0; }
}

// Loads one object. Returns false (with a reason) when its mesh data is unusable.
bool LoadObject(mz_zip_archive& zip, const json& x, MeshObject& o, std::string& why) {
    o.name = S(x, "name", "Mesh");
    o.visible = B(x, "visible", true);
    o.shadeSmooth = B(x, "shadeSmooth", false);
    const json& tf = Obj(x, "transform");
    o.transform.position = V3(tf, "position", o.transform.position);
    o.transform.rotation = V3(tf, "rotation", o.transform.rotation);
    o.transform.scale = V3(tf, "scale", o.transform.scale);

    std::vector<unsigned char> raw;
    if (!ReadEntry(zip, S(x, "data", ""), raw)) { why = "its mesh data is missing"; return false; }
    BlobIn in{raw.data(), raw.size()};
    if (in.U32() != kBlobMagic || in.U32() != 1) { why = "its mesh data is not in a known format"; return false; }
    Mesh& m = o.mesh;
    std::vector<uint64_t> seams, sharp;
    std::vector<Float3> disp;
    in.Vec(m.positions); in.Vec(m.normals); in.Vec(m.uvs); in.Vec(m.loopUVs); in.Vec(m.tris);
    m.polyCount = in.U32();
    in.Vec(seams); in.Vec(sharp);
    in.Vec(o.armature.restPositions); in.Vec(o.armature.weights);
    in.Vec(disp);
    if (!in.ok) { why = "its mesh data is cut short"; return false; }
    const size_t nv = m.positions.size();
    for (Float3& p : m.positions) if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) p = {0, 0, 0};
    uint32_t maxPoly = 0;
    for (const Tri& t : m.tris) {
        if (t.a >= nv || t.b >= nv || t.c >= nv) { why = "a triangle points past its vertex list"; return false; }
        maxPoly = std::max(maxPoly, t.poly + 1);
    }
    m.polyCount = std::max(m.polyCount, maxPoly);
    if (m.normals.size() != nv) m.ComputeNormals();
    if (m.uvs.size() != nv) m.uvs.clear();
    if (m.loopUVs.size() != m.tris.size() * 3) m.loopUVs.clear();
    for (uint64_t e : seams) m.seamEdges.insert(e);
    for (uint64_t e : sharp) m.sharpEdges.insert(e);
    m.EnsureUVs();
    m.ComputeBounds();

    LoadMaterial(Obj(x, "material"), o.material);
    for (const json& jm : Arr(Obj(x, "material"), "maps")) {
        if (!jm.is_object()) continue;
        int slot = I(jm, "slot", -1, -1, (int)MapSlot::Count - 1);
        if (slot < 0 || !ReadEntry(zip, S(jm, "file", ""), raw)) continue;
        int w = 0, h = 0, ch = 0;
        unsigned char* px = stbi_load_from_memory(raw.data(), (int)raw.size(), &w, &h, &ch, 4);
        if (!px) continue;
        o.material.cpu[slot].width = w; o.material.cpu[slot].height = h;
        o.material.cpu[slot].pixels.assign(px, px + (size_t)w * h * 4);
        o.material.paths[slot] = S(jm, "label", "Saved map");
        stbi_image_free(px);
    }

    for (const json& jm : Arr(x, "modifiers")) if (jm.is_object() && o.modifiers.size() < 64) o.modifiers.push_back(LoadModifier(jm));
    o.isCurve = B(x, "isCurve", false);
    const json& jc = Obj(x, "curve");
    for (const json& p : Arr(jc, "points"))
        if (o.curve.points.size() < 100000) { json w = {{"p", p}}; o.curve.points.push_back(V3(w, "p", {0, 0, 0})); }
    o.curve.cyclic = B(jc, "cyclic", false);
    o.curve.resolution = I(jc, "resolution", o.curve.resolution, 1, 256);
    o.curve.bevelDepth = F(jc, "bevelDepth", 0.0f, 0.0f, 1000.0f);
    o.curve.bevelResolution = I(jc, "bevelResolution", o.curve.bevelResolution, 0, 32);
    {
        // Handles, types and radii are optional (older projects); each array is used only when complete.
        const size_t n = o.curve.points.size();
        auto vecs = [&](const char* key, std::vector<Float3>& out) {
            const json& a = Arr(jc, key);
            if (a.size() != n) return;
            for (const json& p : a) { json w = {{"p", p}}; out.push_back(V3(w, "p", {0, 0, 0})); }
        };
        vecs("handleL", o.curve.handleL);
        vecs("handleR", o.curve.handleR);
        if (o.curve.handleL.size() != n || o.curve.handleR.size() != n) { o.curve.handleL.clear(); o.curve.handleR.clear(); }
        const json& ht = Arr(jc, "handleType");
        if (ht.size() == n)
            for (const json& t : ht) { json w = {{"t", t}}; o.curve.handleType.push_back((HandleType)I(w, "t", 0, 0, 3)); }
        const json& rd = Arr(jc, "radius");
        if (rd.size() == n)
            for (const json& v : rd) { json w = {{"r", v}}; o.curve.radius.push_back(F(w, "r", 1.0f, 0.0f, 1000.0f)); }
        o.curve.Sync();
    }

    LoadArmature(Obj(x, "armature"), o.armature);
    o.id = (uint32_t)I(x, "id", 0, 0, 1 << 30);
    o.armatureObject = (uint32_t)I(x, "armatureObject", 0, 0, 1 << 30);
    o.isControl = B(x, "isControl", false);
    Armature& a = o.armature;
    const int nb = (int)a.bones.size();
    if (a.weights.size() != a.restPositions.size() || a.restPositions.size() != nv) { a.weights.clear(); a.restPositions.clear(); }
    if (!o.armatureObject) ClampWeights(a, nb);   // a linked mesh is checked against its armature's bones later
    LoadAction(Obj(x, "action"), o.action, nb);

    const json& rb = Obj(x, "rigidBody");
    o.rigidBody.enabled = B(rb, "enabled", false);
    o.rigidBody.passive = B(rb, "passive", false);
    o.rigidBody.mass = F(rb, "mass", 1, 0.001f, 1e6f);
    o.rigidBody.friction = F(rb, "friction", 0.5f, 0, 10);
    o.rigidBody.bounciness = F(rb, "bounciness", 0, 0, 1);
    o.rigidBody.shape = (RigidShape)I(rb, "shape", (int)RigidShape::ConvexHull, 0, (int)RigidShape::ConvexHull);

    const json& mr = Obj(x, "multires");
    int levels = I(mr, "levels", 0, 0, 5);
    if (levels > 0 && !o.isCurve && !m.tris.empty()) {
        o.multires.Rebuild(m, levels);
        if (disp.size() == o.multires.displacement.size()) o.multires.displacement = std::move(disp);
        o.multires.viewLevel = I(mr, "viewLevel", levels, 0, o.multires.levels);
    }
    o.MarkTopoDirty();
    o.stackDirty = true;
    return true;
}

void LoadScene(mz_zip_archive& zip, const json& js, Scene& out, PlaybackState* playback, std::string& notes) {
    const json& jo = Arr(js, "objects");
    int skipped = 0;
    for (size_t i = 0; i < jo.size() && i < 10000; ++i) {
        if (!jo[i].is_object()) { ++skipped; continue; }
        auto o = std::make_unique<MeshObject>();
        std::string why;
        if (!LoadObject(zip, jo[i], *o, why)) {
            ++skipped;
            if (notes.empty()) notes = "skipped " + S(jo[i], "name", "an object") + ": " + why;
            continue;
        }
        out.objects.push_back(std::move(o));
    }
    if (skipped > 1) notes += " (+" + std::to_string(skipped - 1) + " more)";
    // Ids must be unique; links that point nowhere (or at an object without bones) are dropped.
    for (size_t i = 0; i < out.objects.size(); ++i)
        for (size_t j = 0; j < i; ++j)
            if (out.objects[i]->id && out.objects[i]->id == out.objects[j]->id) { out.objects[i]->id = 0; break; }
    for (auto& o : out.objects) {
        if (o->armatureObject) {
            const MeshObject* src = out.FindById(o->armatureObject);
            if (!src || src == o.get() || src->armature.bones.empty()) o->armatureObject = 0;
            ClampWeights(o->armature, o->armatureObject ? (int)src->armature.bones.size() : (int)o->armature.bones.size());
        }
        for (Bone& b : o->armature.bones)
            for (BoneConstraint& c : b.constraints)
                for (uint32_t* t : {&c.targetObject, &c.poleObject})
                    if (*t && !out.FindById(*t)) *t = 0;
    }
    for (auto& o : out.objects)   // targets are object indices; one that no longer exists means "none"
        for (Modifier& md : o->modifiers)
            if (md.shrinkTarget >= (int)out.objects.size()) md.shrinkTarget = -1;

    const json& jc = Arr(js, "cameras");
    if (!jc.empty()) { out.cameras.clear(); out.cameraNames.clear(); }
    for (size_t i = 0; i < jc.size() && i < 1000; ++i) {
        if (!jc[i].is_object()) continue;
        out.cameras.push_back(LoadCamera(Obj(jc[i], "camera")));
        out.cameraNames.push_back(S(jc[i], "name", "Camera"));
    }
    if (out.cameras.empty()) { out.cameras.push_back(SceneCamera{}); out.cameraNames.assign(1, "Camera"); }
    out.activeCamera = I(js, "activeCamera", 0, 0, (int)out.cameras.size() - 1);
    int active = I(js, "activeObject", -1, -1, (int)out.objects.size() - 1);
    if (active >= 0) out.SelectOnly(active); else out.activeIndex = -1;

    if (playback && js.contains("playback")) {
        const json& p = Obj(js, "playback");
        playback->fps = I(p, "fps", playback->fps, 1, 1000);
        playback->frame = F(p, "frame", playback->frame, -1e6f, 1e6f);
        playback->loop = B(p, "loop", playback->loop);
        playback->playing = false;
    }
}
} // namespace

ProjectSaveInfo SaveLeorixProject(const std::string& path, PaintProject& project, const BrushSettings& brush,
                                  const std::string& blocks, const Scene* scene, const PlaybackState* playback) {
    ProjectSaveInfo info;
    mz_zip_archive zip{};
    if (!mz_zip_writer_init_file(&zip, path.c_str(), 0)) { info.message = "Could not create the project file"; return info; }

    json manifest;
    manifest["format"] = "leorix";
    manifest["version"] = "1.0";
    manifest["generator"] = "Leorix Native";
    manifest["canvas"] = {{"width", project.width}, {"height", project.height},
                          {"colorSpace", "sRGB"}, {"bitDepth", 8}};
    manifest["project"] = {{"name", project.name}, {"activeMap", project.activeMap}};
    manifest["brush"] = {{"size", brush.size}, {"opacity", brush.opacity},
                         {"hardness", brush.hardness}, {"flow", brush.flow}, {"spacing", brush.spacing}};

    json maps = json::array();
    int layerId = 1;
    for (const MapDoc& m : project.maps) {
        json jm;
        jm["name"] = m.name;
        jm["kind"] = kMapKindIds[std::clamp((int)m.kind, 0, (int)MapKind::Count - 1)];
        jm["activeLayer"] = m.activeLayer;
        json jlayers = json::array();
        for (const Layer& l : m.layers) {
            std::vector<unsigned char> png;
            std::string file = "layers/L" + std::to_string(layerId) + "_" +
                               kMapKindIds[std::clamp((int)m.kind, 0, (int)MapKind::Count - 1)] + ".png";
            if (EncodePng(l, m.width, m.height, png)) {
                // MZ_BEST_SPEED: the PNG is already compressed, so deflate has little left to do
                mz_zip_writer_add_mem(&zip, file.c_str(), png.data(), png.size(), MZ_BEST_SPEED);
                ++info.layerFiles;
                info.bytes += png.size();
            }
            jlayers.push_back({{"id", layerId}, {"name", l.name}, {"opacity", l.opacity},
                               {"blend", kBlendIds[std::clamp((int)l.blend, 0, (int)Blend::Count - 1)]},
                               {"visible", l.visible}, {"locked", l.locked}, {"file", file}});
            ++layerId;
        }
        jm["layers"] = jlayers;
        maps.push_back(jm);
    }
    manifest["maps"] = maps;
    if (scene) manifest["scene"] = SaveScene(zip, *scene, playback, info);

    std::string text = manifest.dump(2);
    mz_zip_writer_add_mem(&zip, "manifest.json", text.data(), text.size(), MZ_BEST_COMPRESSION);
    if (!blocks.empty()) mz_zip_writer_add_mem(&zip, "blocks.json", blocks.data(), blocks.size(), MZ_BEST_COMPRESSION);

    mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);
    info.ok = true;
    info.message = "Saved " + std::to_string(info.layerFiles) + " layer image" +
                   (info.layerFiles == 1 ? "" : "s");
    return info;
}

ProjectSaveInfo LoadLeorixProject(const std::string& path, PaintProject& project,
                                  BrushSettings& brush, VulkanContext& vk, std::string* blocks,
                                  Scene* scene, PlaybackState* playback) {
    ProjectSaveInfo info;
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, path.c_str(), 0)) { info.message = "Not a readable .leorix file"; return info; }

    size_t size = 0;
    void* raw = mz_zip_reader_extract_file_to_heap(&zip, "manifest.json", &size, 0);
    if (!raw) { mz_zip_reader_end(&zip); info.message = "No manifest.json inside the project"; return info; }
    std::string text((const char*)raw, size);
    mz_free(raw);
    if (blocks) {
        blocks->clear();
        int at = mz_zip_reader_locate_file(&zip, "blocks.json", nullptr, 0);
        mz_zip_archive_file_stat st{};
        size_t bsize = 0;
        if (at >= 0 && mz_zip_reader_file_stat(&zip, (mz_uint)at, &st) && st.m_uncomp_size <= (16u << 20))   // refuse oversized entries
            if (void* b = mz_zip_reader_extract_to_heap(&zip, (mz_uint)at, &bsize, 0)) { blocks->assign((const char*)b, bsize); mz_free(b); }
    }

    json manifest;
    try { manifest = json::parse(text); }
    catch (const std::exception& e) { mz_zip_reader_end(&zip); info.message = std::string("Manifest is not valid JSON: ") + e.what(); return info; }
    if (!manifest.is_object()) { mz_zip_reader_end(&zip); info.message = "The project manifest is not a JSON object"; return info; }

    std::string sceneNotes;
    if (scene && manifest.is_object() && manifest.contains("scene") && manifest["scene"].is_object()) {
        Scene loaded;
        LoadScene(zip, manifest["scene"], loaded, playback, sceneNotes);
        for (auto& o : scene->objects) o->material.Release(vk);
        if (vk.device != VK_NULL_HANDLE)   // headless (self-test) keeps the CPU copies only
            for (auto& o : loaded.objects)
                for (int i = 0; i < (int)MapSlot::Count; ++i) {
                    TextureData t = std::move(o->material.cpu[i]);
                    if (t.Valid()) SetMaterialMap(vk, o->material, (MapSlot)i, t.pixels.data(), t.width, t.height, o->material.paths[i]);
                }
        *scene = std::move(loaded);
    }

    // release the GPU textures of the project being replaced
    for (MapDoc& m : project.maps) m.texture.Destroy(vk);
    project.maps.clear();

    // Typed readers throughout: a wrong type or a NaN falls back to the default instead of throwing.
    // shortcut: canvas size and map/layer counts are capped but total memory is not, check it if huge files matter
    project.width = I(Obj(manifest, "canvas"), "width", 2048, 1, 16384);
    project.height = I(Obj(manifest, "canvas"), "height", 2048, 1, 16384);
    project.name = S(Obj(manifest, "project"), "name", "untitled");
    {
        const json& b = Obj(manifest, "brush");
        brush.size = F(b, "size", brush.size, 0.1f, 10000);
        brush.opacity = F(b, "opacity", brush.opacity, 0, 100);
        brush.hardness = F(b, "hardness", brush.hardness, 0, 100);
        brush.flow = F(b, "flow", brush.flow, 0, 100);
        brush.spacing = F(b, "spacing", brush.spacing, 0.01f, 1000);
    }

    for (const json& jm : Arr(manifest, "maps")) {
        if (!jm.is_object() || project.maps.size() >= 64) continue;
        MapDoc m;
        m.kind = KindFromId(S(jm, "kind", "albedo"));
        m.name = S(jm, "name", MapKindLabel(m.kind));
        m.width = project.width; m.height = project.height;
        for (const json& jl : Arr(jm, "layers")) {
            if (!jl.is_object() || m.layers.size() >= 256) continue;
            Layer l;
            l.name = S(jl, "name", "Layer");
            l.opacity = F(jl, "opacity", 100.0f, 0, 100);
            l.visible = B(jl, "visible", true);
            l.locked = B(jl, "locked", false);
            l.blend = BlendFromId(S(jl, "blend", "normal"));
            l.Fill(0, m.width, m.height);

            std::string file = S(jl, "file", "");
            std::vector<unsigned char> png;
            if (!file.empty() && ReadEntry(zip, file, png)) {
                {
                    int w = 0, h = 0, ch = 0;
                    unsigned char* px = stbi_load_from_memory(png.data(), (int)png.size(), &w, &h, &ch, 4);
                    if (px) {
                        if (w == m.width && h == m.height)
                            std::memcpy(l.pixels.data(), px, (size_t)w * h * 4);
                        stbi_image_free(px);
                        ++info.layerFiles;
                    }
                }
            }
            m.layers.push_back(std::move(l));
        }
        if (m.layers.empty()) { Layer bg; bg.name = "Background"; bg.Fill(MapKindFill(m.kind), m.width, m.height); m.layers.push_back(bg); }
        m.activeLayer = I(jm, "activeLayer", 0, 0, (int)m.layers.size() - 1);
        m.dirty.Whole(m.width, m.height);
        project.maps.push_back(std::move(m));
    }
    mz_zip_reader_end(&zip);

    if (project.maps.empty()) project.New(project.width, project.height);
    project.activeMap = I(Obj(manifest, "project"), "activeMap", 0, 0, (int)project.maps.size() - 1);
    info.ok = true;
    info.message = "Opened " + project.name + " - " + std::to_string(project.maps.size()) + " map(s), " +
                   std::to_string(info.layerFiles) + " layer image(s)";
    if (scene && manifest.contains("scene")) info.message += ", " + std::to_string(scene->objects.size()) + " object(s)";
    if (!sceneNotes.empty()) info.message += " - " + sceneNotes;
    return info;
}
