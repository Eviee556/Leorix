#include "mesh/IO.h"
#include "anim/Animation.h"
#include "anim/Armature.h"
#include <climits>
#include <cmath>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_map>

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"
#include "ufbx.h"

#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_EXTERNAL_IMAGE
#include "tiny_gltf.h"

static std::string Ext(const std::string& path) {
    size_t dot = path.find_last_of('.');
    std::string e = dot == std::string::npos ? "" : path.substr(dot + 1);
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return std::tolower(c); });
    return e;
}

const char* ImportFilterSpec() { return "obj;fbx;gltf;glb;usd;usda;usdc"; }
const char* ExportFilterSpec() { return "obj;fbx;gltf;glb;usda"; }

// ---------------------------------------------------------------- OBJ (tinyobjloader)
static bool ImportOBJ(const std::string& path, Mesh& out, std::string& error) {
    tinyobj::ObjReader reader;
    tinyobj::ObjReaderConfig cfg; cfg.triangulate = false; // keep polygons so quads stay quads
    if (!reader.ParseFromFile(path, cfg)) { error = reader.Error().empty() ? "Could not parse OBJ" : reader.Error(); return false; }
    const auto& attrib = reader.GetAttrib();
    out.Clear();
    // One vertex per OBJ position: the file's vertex and face counts are preserved exactly, so a
    // Blender cube stays 8 vertices / 6 faces. UVs are stored per face corner instead.
    for (size_t i = 0; i + 2 < attrib.vertices.size(); i += 3)
        out.positions.push_back({attrib.vertices[i], attrib.vertices[i + 1], attrib.vertices[i + 2]});
    const bool hasUV = !attrib.texcoords.empty();

    uint32_t poly = 0;
    for (const auto& shape : reader.GetShapes()) {
        size_t offset = 0;
        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); ++f) {
            int fv = shape.mesh.num_face_vertices[f];
            std::vector<uint32_t> idx(fv);
            std::vector<Float2>   cornerUV(fv, Float2{});
            for (int v = 0; v < fv; ++v) {
                const tinyobj::index_t& id = shape.mesh.indices[offset + v];
                idx[v] = static_cast<uint32_t>(id.vertex_index);
                if (hasUV && id.texcoord_index >= 0)
                    cornerUV[v] = {attrib.texcoords[2 * id.texcoord_index], attrib.texcoords[2 * id.texcoord_index + 1]};
            }
            for (int v = 1; v + 1 < fv; ++v) {
                out.tris.push_back({idx[0], idx[v], idx[v + 1], poly});
                if (hasUV) {
                    out.loopUVs.push_back(cornerUV[0]);
                    out.loopUVs.push_back(cornerUV[v]);
                    out.loopUVs.push_back(cornerUV[v + 1]);
                }
            }
            ++poly;
            offset += fv;
        }
    }
    out.polyCount = poly;
    out.name = "OBJ mesh";
    out.HarmonizeNormals();   // imports often arrive with mixed winding; fix it up front
    out.RecenterAndNormalize();
    return true;
}

static bool ExportOBJ(const std::string& path, const Mesh& mesh, std::string& error) {
    std::ofstream f(path);
    if (!f) { error = "Could not open file for writing"; return false; }
    f << "# Exported by Leorix\n";
    for (const auto& p : mesh.positions) f << "v " << p.x << ' ' << p.y << ' ' << p.z << '\n';
    if (!mesh.uvs.empty()) for (const auto& uv : mesh.uvs) f << "vt " << uv.u << ' ' << uv.v << '\n';
    // Group triangles by polygon id to write quads/n-gons back out.
    std::unordered_map<uint32_t, std::vector<const Tri*>> byPoly;
    for (const auto& t : mesh.tris) byPoly[t.poly].push_back(&t);
    for (auto& kv : byPoly) {
        std::vector<uint32_t> ring; ring.push_back(kv.second[0]->a); ring.push_back(kv.second[0]->b); ring.push_back(kv.second[0]->c);
        for (size_t i = 1; i < kv.second.size(); ++i) ring.push_back(kv.second[i]->c); // fan reconstruction
        f << 'f';
        for (uint32_t vi : ring) { f << ' ' << (vi + 1); if (!mesh.uvs.empty()) f << '/' << (vi + 1); }
        f << '\n';
    }
    return true;
}

// ---------------------------------------------------------------- FBX (ufbx)
static bool ImportFBX(const std::string& path, Mesh& out, std::string& error) {
    ufbx_load_opts opts{};
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters = 1.0f;
    opts.generate_missing_normals = true;
    ufbx_error err{};
    ufbx_scene* scene = ufbx_load_file(path.c_str(), &opts, &err);
    if (!scene) { error = std::string("FBX: ") + err.description.data; return false; }
    out.Clear();
    uint32_t poly = 0;
    for (size_t mi = 0; mi < scene->meshes.count; ++mi) {
        ufbx_mesh* m = scene->meshes.data[mi];
        uint32_t base = static_cast<uint32_t>(out.positions.size());
        for (size_t i = 0; i < m->num_vertices; ++i) {
            ufbx_vec3 p = m->vertices.data[i];
            out.positions.push_back({(float)p.x, (float)p.y, (float)p.z});
        }
        bool hasUV = m->vertex_uv.exists;
        for (size_t fi = 0; fi < m->faces.count; ++fi) {
            ufbx_face face = m->faces.data[fi];
            std::vector<uint32_t> idx(face.num_indices);
            std::vector<Float2>   cornerUV(face.num_indices, Float2{});
            for (uint32_t c = 0; c < face.num_indices; ++c) {
                idx[c] = base + m->vertex_indices.data[face.index_begin + c];
                if (hasUV) {
                    ufbx_vec2 uv = ufbx_get_vertex_vec2(&m->vertex_uv, face.index_begin + c);
                    cornerUV[c] = {(float)uv.x, (float)uv.y};
                }
            }
            for (uint32_t c = 1; c + 1 < face.num_indices; ++c) {
                if (hasUV) {
                    out.loopUVs.push_back(cornerUV[0]);
                    out.loopUVs.push_back(cornerUV[c]);
                    out.loopUVs.push_back(cornerUV[c + 1]);
                }
            }
            for (uint32_t c = 1; c + 1 < face.num_indices; ++c) out.tris.push_back({idx[0], idx[c], idx[c + 1], poly});
            ++poly;
        }
    }
    ufbx_free_scene(scene);
    out.polyCount = poly;
    if (out.positions.empty()) { error = "FBX had no mesh geometry"; return false; }
    out.name = "FBX mesh";
    out.HarmonizeNormals();   // imports often arrive with mixed winding; fix it up front
    out.RecenterAndNormalize();
    return true;
}

// ---------------------------------------------------------------- glTF / GLB (tinygltf)
static void ReadGltfMesh(const tinygltf::Model& model, Mesh& out) {
    uint32_t poly = 0;
    for (const auto& mesh : model.meshes) {
        for (const auto& prim : mesh.primitives) {
            if (prim.mode != TINYGLTF_MODE_TRIANGLES) continue;
            auto posIt = prim.attributes.find("POSITION");
            if (posIt == prim.attributes.end()) continue;
            const tinygltf::Accessor& posAcc = model.accessors[posIt->second];
            const tinygltf::BufferView& posView = model.bufferViews[posAcc.bufferView];
            const tinygltf::Buffer& posBuf = model.buffers[posView.buffer];
            const float* pos = reinterpret_cast<const float*>(&posBuf.data[posView.byteOffset + posAcc.byteOffset]);
            uint32_t base = static_cast<uint32_t>(out.positions.size());
            for (size_t i = 0; i < posAcc.count; ++i) out.positions.push_back({pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2]});

            auto uvIt = prim.attributes.find("TEXCOORD_0");
            out.uvs.resize(out.positions.size());
            if (uvIt != prim.attributes.end()) {
                const tinygltf::Accessor& a = model.accessors[uvIt->second];
                const tinygltf::BufferView& bv = model.bufferViews[a.bufferView];
                const float* uv = reinterpret_cast<const float*>(&model.buffers[bv.buffer].data[bv.byteOffset + a.byteOffset]);
                for (size_t i = 0; i < a.count; ++i) out.uvs[base + i] = {uv[i * 2], uv[i * 2 + 1]};
            }
            if (prim.indices >= 0) {
                const tinygltf::Accessor& ia = model.accessors[prim.indices];
                const tinygltf::BufferView& iv = model.bufferViews[ia.bufferView];
                const unsigned char* data = &model.buffers[iv.buffer].data[iv.byteOffset + ia.byteOffset];
                auto index = [&](size_t k) -> uint32_t {
                    switch (ia.componentType) {
                        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: return reinterpret_cast<const uint32_t*>(data)[k];
                        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: return reinterpret_cast<const uint16_t*>(data)[k];
                        default: return data[k];
                    }
                };
                for (size_t i = 0; i + 2 < ia.count; i += 3)
                    out.tris.push_back({base + index(i), base + index(i + 1), base + index(i + 2), poly++});
            }
        }
    }
    out.polyCount = poly;
}

static bool ImportGLTF(const std::string& path, Mesh& out, std::string& error, bool binary) {
    tinygltf::Model model; tinygltf::TinyGLTF loader; std::string err, warn;
    bool ok = binary ? loader.LoadBinaryFromFile(&model, &err, &warn, path) : loader.LoadASCIIFromFile(&model, &err, &warn, path);
    if (!ok) { error = err.empty() ? "Could not parse glTF" : err; return false; }
    out.Clear();
    ReadGltfMesh(model, out);
    if (out.positions.empty()) { error = "glTF had no triangle geometry"; return false; }
    out.name = "glTF mesh";
    out.HarmonizeNormals();   // imports often arrive with mixed winding; fix it up front
    out.RecenterAndNormalize();
    return true;
}

// Quaternions (x, y, z, w) for glTF rotations.
struct Quat { float x = 0, y = 0, z = 0, w = 1; };
static Quat QMul(const Quat& a, const Quat& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
static Quat QAxis(int axis, float angle) {
    Quat q; float s = std::sin(angle * 0.5f); q.w = std::cos(angle * 0.5f);
    (axis == 0 ? q.x : axis == 1 ? q.y : q.z) = s;
    return q;
}
// Bones use Mat4::RotateXYZ = Rx * Ry * Rz; objects use Transform::Apply, which rotates X first.
static Quat BoneQuat(const Float3& e) { return QMul(QMul(QAxis(0, e.x), QAxis(1, e.y)), QAxis(2, e.z)); }
static Quat ObjectQuat(const Float3& e) { return QMul(QMul(QAxis(2, e.z), QAxis(1, e.y)), QAxis(0, e.x)); }

// Appends raw data as its own buffer view + accessor; returns the accessor index.
static int AddAccessor(tinygltf::Model& model, const void* src, size_t bytes, size_t count, int comp, int type,
                       int target = 0, std::vector<double> mn = {}, std::vector<double> mx = {}) {
    std::vector<unsigned char>& d = model.buffers[0].data;
    while (d.size() % 4) d.push_back(0);
    size_t off = d.size();
    d.resize(off + bytes);
    if (bytes) std::memcpy(d.data() + off, src, bytes);
    tinygltf::BufferView v; v.buffer = 0; v.byteOffset = off; v.byteLength = bytes; if (target) v.target = target;
    model.bufferViews.push_back(v);
    tinygltf::Accessor a; a.bufferView = (int)model.bufferViews.size() - 1; a.componentType = comp; a.count = count; a.type = type;
    a.minValues = std::move(mn); a.maxValues = std::move(mx);
    model.accessors.push_back(a);
    return (int)model.accessors.size() - 1;
}

// One node's baked keys: frame times -> translation / rotation / scale, LINEAR.
struct BakedTrack {
    int node = -1;
    std::vector<float> times;
    std::vector<Float3> t, s; std::vector<Quat> r;
    bool hasT = false, hasR = false, hasS = false;
};
static void WriteTrack(tinygltf::Model& model, tinygltf::Animation& anim, const BakedTrack& k) {
    if (k.times.empty()) return;
    int in = AddAccessor(model, k.times.data(), k.times.size() * 4, k.times.size(), TINYGLTF_COMPONENT_TYPE_FLOAT,
                         TINYGLTF_TYPE_SCALAR, 0, {k.times.front()}, {k.times.back()});
    auto add = [&](const void* data, size_t bytes, int type, const char* path) {
        tinygltf::AnimationSampler sm; sm.input = in; sm.interpolation = "LINEAR";
        sm.output = AddAccessor(model, data, bytes, k.times.size(), TINYGLTF_COMPONENT_TYPE_FLOAT, type);
        anim.samplers.push_back(sm);
        tinygltf::AnimationChannel ch; ch.sampler = (int)anim.samplers.size() - 1; ch.target_node = k.node; ch.target_path = path;
        anim.channels.push_back(ch);
    };
    if (k.hasT) add(k.t.data(), k.t.size() * 12, TINYGLTF_TYPE_VEC3, "translation");
    if (k.hasR) add(k.r.data(), k.r.size() * 16, TINYGLTF_TYPE_VEC4, "rotation");
    if (k.hasS) add(k.s.data(), k.s.size() * 12, TINYGLTF_TYPE_VEC3, "scale");
}

// Bakes every frame between the first and last key of the channels `pick` accepts, so Bezier and
// easing curves survive glTF's LINEAR sampling. `rest` turns a frame's (loc, rot, scale) into TRS.
template <class Pick, class Make>
static void BakeTrack(tinygltf::Model& model, tinygltf::Animation& anim, const Action& action, int node, float rate,
                      ChannelTarget target, int bone, Float3 loc0, Float3 rot0, Float3 scl0, Pick pick, Make make) {
    int f0 = INT_MAX, f1 = INT_MIN;
    for (const Channel& c : action.channels)
        if (pick(c) && !c.keys.empty()) { f0 = std::min(f0, c.keys.front().frame); f1 = std::max(f1, c.keys.back().frame); }
    if (f0 > f1) return;
    BakedTrack k; k.node = node;
    auto has = [&](ChannelProp pr) {
        for (int a = 0; a < 3; ++a) { const Channel* c = action.Get(target, bone, pr, a); if (c && !c->keys.empty()) return true; }
        return false;
    };
    k.hasT = has(ChannelProp::Location); k.hasR = has(ChannelProp::Rotation); k.hasS = has(ChannelProp::Scale);
    auto eval = [&](ChannelProp pr, Float3 v, float f) {
        float* out[3] = {&v.x, &v.y, &v.z};
        for (int a = 0; a < 3; ++a) if (const Channel* c = action.Get(target, bone, pr, a)) c->Evaluate(f, *out[a]);
        return v;
    };
    for (int f = f0; f <= f1; ++f) {
        k.times.push_back((f - std::min(1, f0)) / rate);   // frame 1 is t = 0
        Float3 t; Quat r; Float3 s;
        make(eval(ChannelProp::Location, loc0, (float)f), eval(ChannelProp::Rotation, rot0, (float)f),
             eval(ChannelProp::Scale, scl0, (float)f), t, r, s);
        k.t.push_back(t); k.r.push_back(r); k.s.push_back(s);
    }
    WriteTrack(model, anim, k);
}

// Inverse of an affine Mat4 (row-major, translation in m[3], m[7], m[11]).
static Mat4 AffineInverse(const Mat4& a) {
    const float* m = a.m;
    float c00 = m[5] * m[10] - m[6] * m[9], c01 = m[2] * m[9] - m[1] * m[10], c02 = m[1] * m[6] - m[2] * m[5];
    float c10 = m[6] * m[8] - m[4] * m[10], c11 = m[0] * m[10] - m[2] * m[8], c12 = m[2] * m[4] - m[0] * m[6];
    float c20 = m[4] * m[9] - m[5] * m[8], c21 = m[1] * m[8] - m[0] * m[9], c22 = m[0] * m[5] - m[1] * m[4];
    float det = m[0] * c00 + m[1] * c10 + m[2] * c20;
    float id = std::fabs(det) > 1e-20f ? 1.0f / det : 0.0f;
    Mat4 r;
    float R[9] = {c00 * id, c01 * id, c02 * id, c10 * id, c11 * id, c12 * id, c20 * id, c21 * id, c22 * id};
    for (int i = 0; i < 3; ++i) {
        r.m[i * 4] = R[i * 3]; r.m[i * 4 + 1] = R[i * 3 + 1]; r.m[i * 4 + 2] = R[i * 3 + 2];
        r.m[i * 4 + 3] = -(R[i * 3] * m[3] + R[i * 3 + 1] * m[7] + R[i * 3 + 2] * m[11]);
    }
    return r;
}

// A joint's parent-relative TRS taken from Armature::Evaluate, so constraints (IK, Track To, limits...)
// are exported as posed. Joint rest frames sit at each head with no rotation: joint world = M_i * T(head_i).
static void EvaluatedJointTRS(const Armature& a, const std::vector<Mat4>& M, int i, int p, Float3& t, Quat& q, Float3& s) {
    Mat4 w = M[i] * Mat4::Translate(a.bones[i].head);
    if (p >= 0) w = AffineInverse(M[p] * Mat4::Translate(a.bones[p].head)) * w;
    const float* m = w.m;
    t = {m[3], m[7], m[11]};
    s = {std::sqrt(m[0] * m[0] + m[4] * m[4] + m[8] * m[8]), std::sqrt(m[1] * m[1] + m[5] * m[5] + m[9] * m[9]),
         std::sqrt(m[2] * m[2] + m[6] * m[6] + m[10] * m[10])};
    float sx = s.x > 1e-12f ? s.x : 1, sy = s.y > 1e-12f ? s.y : 1, sz = s.z > 1e-12f ? s.z : 1;
    float r00 = m[0] / sx, r01 = m[1] / sy, r02 = m[2] / sz;
    float r10 = m[4] / sx, r11 = m[5] / sy, r12 = m[6] / sz;
    float r20 = m[8] / sx, r21 = m[9] / sy, r22 = m[10] / sz;
    float tr = r00 + r11 + r22;
    if (tr > 0) { float k = std::sqrt(tr + 1) * 2; q = {(r21 - r12) / k, (r02 - r20) / k, (r10 - r01) / k, 0.25f * k}; }
    else if (r00 > r11 && r00 > r22) { float k = std::sqrt(1 + r00 - r11 - r22) * 2; q = {0.25f * k, (r01 + r10) / k, (r02 + r20) / k, (r21 - r12) / k}; }
    else if (r11 > r22) { float k = std::sqrt(1 + r11 - r00 - r22) * 2; q = {(r01 + r10) / k, 0.25f * k, (r12 + r21) / k, (r02 - r20) / k}; }
    else { float k = std::sqrt(1 + r22 - r00 - r11) * 2; q = {(r02 + r20) / k, (r12 + r21) / k, 0.25f * k, (r10 - r01) / k}; }
}

static bool ExportGLTF(const std::string& path, const Mesh& mesh, const Armature* arm, const Action* action,
                       int fps, std::string& error, bool binary) {
    const bool skinned = arm && !arm->bones.empty() && arm->Bound() && arm->restPositions.size() == mesh.positions.size();
    // A skinned mesh is written in its bind pose; the joints carry the current pose.
    const std::vector<Float3>& pos = skinned ? arm->restPositions : mesh.positions;

    tinygltf::Model model; tinygltf::Scene scene; tinygltf::Mesh gmesh; tinygltf::Primitive prim;
    model.buffers.emplace_back();
    Float3 mn{1e30f, 1e30f, 1e30f}, mx{-1e30f, -1e30f, -1e30f};
    for (const Float3& p : pos) {
        mn = {std::min(mn.x, p.x), std::min(mn.y, p.y), std::min(mn.z, p.z)};
        mx = {std::max(mx.x, p.x), std::max(mx.y, p.y), std::max(mx.z, p.z)};
    }
    if (pos.empty()) mn = mx = {0, 0, 0};
    std::vector<uint32_t> indices; indices.reserve(mesh.tris.size() * 3);
    for (const auto& t : mesh.tris) { indices.push_back(t.a); indices.push_back(t.b); indices.push_back(t.c); }
    prim.attributes["POSITION"] = AddAccessor(model, pos.data(), pos.size() * 12, pos.size(), TINYGLTF_COMPONENT_TYPE_FLOAT,
        TINYGLTF_TYPE_VEC3, TINYGLTF_TARGET_ARRAY_BUFFER, {mn.x, mn.y, mn.z}, {mx.x, mx.y, mx.z});
    prim.indices = AddAccessor(model, indices.data(), indices.size() * 4, indices.size(), TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT,
        TINYGLTF_TYPE_SCALAR, TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER);
    prim.mode = TINYGLTF_MODE_TRIANGLES;

    tinygltf::Node meshNode; meshNode.mesh = 0; meshNode.name = mesh.name.empty() ? "Mesh" : mesh.name;
    model.nodes.push_back(meshNode); scene.nodes.push_back(0);
    const float rate = (float)std::max(1, fps);
    tinygltf::Animation anim; anim.name = action && !action->name.empty() ? action->name : "Action";

    if (skinned) {
        const int nb = (int)arm->bones.size();
        auto parentOf = [&](int i) { int p = arm->bones[i].parent; return p >= 0 && p < i ? p : -1; };   // as Armature::Evaluate
        auto restRel = [&](int i) {
            const Float3& h = arm->bones[i].head; int p = parentOf(i);
            return p >= 0 ? Float3{h.x - arm->bones[p].head.x, h.y - arm->bones[p].head.y, h.z - arm->bones[p].head.z} : h;
        };
        // Top four influences, normalized. A vertex with none goes to an extra still joint so it
        // stays put, as Armature::Skin leaves it at rest.
        std::vector<uint16_t> joints(pos.size() * 4, 0); std::vector<float> weights(pos.size() * 4, 0);
        bool needStill = false;
        for (size_t v = 0; v < pos.size(); ++v) {
            const VertexWeights& vw = arm->weights[v];
            float total = 0;
            for (int s = 0; s < 4; ++s) if (vw.bone[s] >= 0 && vw.bone[s] < nb && vw.weight[s] > 0) total += vw.weight[s];
            if (total <= 1e-6f) { joints[v * 4] = (uint16_t)nb; weights[v * 4] = 1; needStill = true; continue; }
            for (int s = 0; s < 4; ++s)
                if (vw.bone[s] >= 0 && vw.bone[s] < nb && vw.weight[s] > 0) {
                    joints[v * 4 + s] = (uint16_t)vw.bone[s]; weights[v * 4 + s] = vw.weight[s] / total;
                }
        }
        prim.attributes["JOINTS_0"] = AddAccessor(model, joints.data(), joints.size() * 2, pos.size(),
            TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT, TINYGLTF_TYPE_VEC4, TINYGLTF_TARGET_ARRAY_BUFFER);
        prim.attributes["WEIGHTS_0"] = AddAccessor(model, weights.data(), weights.size() * 4, pos.size(),
            TINYGLTF_COMPONENT_TYPE_FLOAT, TINYGLTF_TYPE_VEC4, TINYGLTF_TARGET_ARRAY_BUFFER);

        // Joint rest frames sit at each bone's head with no rotation, so the inverse bind matrix
        // is a translation by -head, and joint * IBM reproduces Armature::Evaluate exactly.
        tinygltf::Skin skin; skin.name = "Armature";
        const int firstJoint = (int)model.nodes.size();
        std::vector<float> ibm;
        auto boneTRS = [](Float3 rel) {
            return [rel](Float3 l, Float3 r, Float3 s, Float3& t, Quat& q, Float3& sc) {
                t = {rel.x + l.x, rel.y + l.y, rel.z + l.z}; q = BoneQuat(r); sc = s;
            };
        };
        bool constrained = false;   // constrained rigs are exported from Armature::Evaluate, the rest exactly from the pose
        for (const Bone& b : arm->bones) for (const BoneConstraint& c : b.constraints) constrained |= c.enabled;
        std::vector<Mat4> posed;
        if (constrained) arm->Evaluate(posed);
        for (int i = 0; i < nb; ++i) {
            const Bone& b = arm->bones[i];
            Float3 t, sc; Quat q;
            if (constrained) EvaluatedJointTRS(*arm, posed, i, parentOf(i), t, q, sc);
            else boneTRS(restRel(i))(b.poseLocation, b.poseRotation, b.poseScale, t, q, sc);
            tinygltf::Node n; n.name = b.name;
            n.translation = {t.x, t.y, t.z}; n.rotation = {q.x, q.y, q.z, q.w}; n.scale = {sc.x, sc.y, sc.z};
            model.nodes.push_back(n);
            skin.joints.push_back(firstJoint + i);
            float m[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, -b.head.x,-b.head.y,-b.head.z,1};   // column-major
            ibm.insert(ibm.end(), m, m + 16);
        }
        for (int i = 0; i < nb; ++i) {
            int p = parentOf(i);
            if (p >= 0) model.nodes[firstJoint + p].children.push_back(firstJoint + i);
            else scene.nodes.push_back(firstJoint + i);
        }
        if (needStill) {
            tinygltf::Node n; n.name = "Unweighted";
            model.nodes.push_back(n);
            skin.joints.push_back((int)model.nodes.size() - 1);
            scene.nodes.push_back((int)model.nodes.size() - 1);
            float m[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
            ibm.insert(ibm.end(), m, m + 16);
        }
        skin.inverseBindMatrices = AddAccessor(model, ibm.data(), ibm.size() * 4, skin.joints.size(),
            TINYGLTF_COMPONENT_TYPE_FLOAT, TINYGLTF_TYPE_MAT4);
        model.skins.push_back(skin);
        model.nodes[0].skin = 0;

        if (action && constrained) {
            // Constraints couple bones (an IK target's keys move the whole chain), so every joint is baked
            // over the range of all bone keys from a posed copy of the armature.
            int f0 = INT_MAX, f1 = INT_MIN;
            for (const Channel& c : action->channels)
                if (c.target == ChannelTarget::Bone && !c.keys.empty()) { f0 = std::min(f0, c.keys.front().frame); f1 = std::max(f1, c.keys.back().frame); }
            if (f0 <= f1) {
                Armature a2 = *arm;
                std::vector<BakedTrack> tracks(nb);
                for (int i = 0; i < nb; ++i) { tracks[i].node = firstJoint + i; tracks[i].hasT = tracks[i].hasR = tracks[i].hasS = true; }
                for (int f = f0; f <= f1; ++f) {
                    for (int i = 0; i < nb; ++i) {
                        const Bone& b = arm->bones[i];
                        Float3 v[3] = {b.poseLocation, b.poseRotation, b.poseScale};
                        const ChannelProp props[3] = {ChannelProp::Location, ChannelProp::Rotation, ChannelProp::Scale};
                        for (int pr = 0; pr < 3; ++pr) {
                            float* o[3] = {&v[pr].x, &v[pr].y, &v[pr].z};
                            for (int ax = 0; ax < 3; ++ax)
                                if (const Channel* c = action->Get(ChannelTarget::Bone, i, props[pr], ax)) c->Evaluate((float)f, *o[ax]);
                        }
                        a2.bones[i].poseLocation = v[0]; a2.bones[i].poseRotation = v[1]; a2.bones[i].poseScale = v[2];
                    }
                    a2.Evaluate(posed);
                    for (int i = 0; i < nb; ++i) {
                        Float3 t, sc; Quat q;
                        EvaluatedJointTRS(*arm, posed, i, parentOf(i), t, q, sc);
                        tracks[i].times.push_back((f - std::min(1, f0)) / rate);   // frame 1 is t = 0, as BakeTrack
                        tracks[i].t.push_back(t); tracks[i].r.push_back(q); tracks[i].s.push_back(sc);
                    }
                }
                for (const BakedTrack& k : tracks) WriteTrack(model, anim, k);
            }
        } else if (action)
            for (int i = 0; i < nb; ++i) {
                const Bone& b = arm->bones[i];
                BakeTrack(model, anim, *action, firstJoint + i, rate, ChannelTarget::Bone, i, b.poseLocation, b.poseRotation, b.poseScale,
                          [i](const Channel& c) { return c.target == ChannelTarget::Bone && c.boneIndex == i; }, boneTRS(restRel(i)));
            }
        // shortcut: object-level keys are not exported for skinned meshes (glTF ignores a skinned
        // node's own transform); add a parent node for the object if rigs need to travel.
    } else if (action) {
        BakeTrack(model, anim, *action, 0, rate, ChannelTarget::Object, -1, {0, 0, 0}, {0, 0, 0}, {1, 1, 1},
                  [](const Channel& c) { return c.target == ChannelTarget::Object; },
                  [](Float3 l, Float3 r, Float3 s, Float3& t, Quat& q, Float3& sc) { t = l; q = ObjectQuat(r); sc = s; });
    }
    if (!anim.channels.empty()) model.animations.push_back(anim);

    gmesh.primitives.push_back(prim);
    model.meshes.push_back(gmesh); model.scenes.push_back(scene);
    model.asset.version = "2.0"; model.asset.generator = "Leorix"; model.defaultScene = 0;

    tinygltf::TinyGLTF writer;
    if (!writer.WriteGltfSceneToFile(&model, path, false, binary, !binary, binary)) { error = "Could not write glTF"; return false; }
    return true;
}

// ---------------------------------------------------------------- USD (ASCII USDA reader/writer)
// A focused reader for the subset Leorix and Blender/Houdini write: def Mesh with points,
// faceVertexCounts and faceVertexIndices. Binary USDC is detected and reported.
static bool ImportUSDA(const std::string& path, Mesh& out, std::string& error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { error = "Could not open USD file"; return false; }
    std::stringstream ss; ss << f.rdbuf(); std::string text = ss.str();
    if (text.rfind("PXR-USDC", 0) == 0 || (text.size() > 8 && text[0] == 'P' && text[1] == 'X' && text[2] == 'R')) {
        error = "This is a binary USDC file. Re-export it as USDA (ASCII) - Leorix reads USDA and writes USDA.";
        return false;
    }
    // Values start after the '=' - searching for the first '[' would match the one inside the
    // type name ("point3f[] points"), which is what an earlier version did.
    auto readFloats = [](const std::string& s, size_t from, std::vector<float>& v) {
        size_t eq = s.find('=', from); if (eq == std::string::npos) return;
        size_t lb = s.find('[', eq); size_t rb = s.find(']', lb);
        if (lb == std::string::npos || rb == std::string::npos) return;
        std::string inner = s.substr(lb + 1, rb - lb - 1);
        for (char& c : inner) if (c == '(' || c == ')' || c == ',') c = ' ';
        std::stringstream is(inner); float x; while (is >> x) v.push_back(x);
    };
    auto readInts = [](const std::string& s, size_t from, std::vector<int>& v) {
        size_t eq = s.find('=', from); if (eq == std::string::npos) return;
        size_t lb = s.find('[', eq); size_t rb = s.find(']', lb);
        if (lb == std::string::npos || rb == std::string::npos) return;
        std::string inner = s.substr(lb + 1, rb - lb - 1);
        for (char& c : inner) if (c == ',') c = ' ';
        std::stringstream is(inner); int x; while (is >> x) v.push_back(x);
    };
    out.Clear();
    uint32_t poly = 0;
    // Walk each "def Mesh" block and read its three arrays. The attributes can appear in any
    // order inside the block (Blender writes counts/indices before points), so each lookup is
    // scoped to the block rather than searched forward from the previous match.
    size_t block = 0;
    while (true) {
        size_t meshAt = text.find("def Mesh", block);
        if (meshAt == std::string::npos) break;
        size_t next = text.find("def Mesh", meshAt + 8);
        size_t limit = next == std::string::npos ? text.size() : next;
        std::string blockText = text.substr(meshAt, limit - meshAt);

        std::vector<float> P; std::vector<int> counts, idx;
        size_t at;
        if ((at = blockText.find("points")) != std::string::npos) readFloats(blockText, at, P);
        if ((at = blockText.find("faceVertexCounts")) != std::string::npos) readInts(blockText, at, counts);
        if ((at = blockText.find("faceVertexIndices")) != std::string::npos) readInts(blockText, at, idx);

        uint32_t base = static_cast<uint32_t>(out.positions.size());
        for (size_t i = 0; i + 2 < P.size(); i += 3) out.positions.push_back({P[i], P[i + 1], P[i + 2]});
        size_t cursor = 0;
        for (int c : counts) {
            if (cursor + c > idx.size()) break;
            for (int v = 1; v + 1 < c; ++v)
                out.tris.push_back({base + (uint32_t)idx[cursor], base + (uint32_t)idx[cursor + v],
                                    base + (uint32_t)idx[cursor + v + 1], poly});
            ++poly; cursor += c;
        }
        block = limit;
    }
    out.polyCount = poly;
    if (out.positions.empty()) { error = "No mesh points found in the USD file"; return false; }
    out.name = "USD mesh";
    out.HarmonizeNormals();   // imports often arrive with mixed winding; fix it up front
    out.RecenterAndNormalize();
    return true;
}

static bool ExportUSDA(const std::string& path, const Mesh& mesh, std::string& error) {
    std::ofstream f(path);
    if (!f) { error = "Could not open file for writing"; return false; }
    f << "#usda 1.0\n(\n    defaultPrim = \"Leorix\"\n    metersPerUnit = 1\n    upAxis = \"Y\"\n)\n\n";
    f << "def Xform \"Leorix\"\n{\n    def Mesh \"Mesh\"\n    {\n        uniform bool doubleSided = 1\n";
    std::unordered_map<uint32_t, std::vector<const Tri*>> byPoly;
    for (const auto& t : mesh.tris) byPoly[t.poly].push_back(&t);
    f << "        int[] faceVertexCounts = [";
    bool first = true;
    std::vector<std::vector<uint32_t>> faces;
    for (auto& kv : byPoly) {
        std::vector<uint32_t> ring = {kv.second[0]->a, kv.second[0]->b, kv.second[0]->c};
        for (size_t i = 1; i < kv.second.size(); ++i) ring.push_back(kv.second[i]->c);
        faces.push_back(ring);
        f << (first ? "" : ", ") << ring.size(); first = false;
    }
    f << "]\n        int[] faceVertexIndices = [";
    first = true;
    for (auto& ring : faces) for (uint32_t vi : ring) { f << (first ? "" : ", ") << vi; first = false; }
    f << "]\n        point3f[] points = [";
    first = true;
    for (const auto& p : mesh.positions) { f << (first ? "" : ", ") << '(' << p.x << ", " << p.y << ", " << p.z << ')'; first = false; }
    f << "]\n";
    if (!mesh.uvs.empty()) {
        f << "        texCoord2f[] primvars:st = [";
        first = true;
        for (const auto& uv : mesh.uvs) { f << (first ? "" : ", ") << '(' << uv.u << ", " << uv.v << ')'; first = false; }
        f << "] (interpolation = \"vertex\")\n";
    }
    f << "    }\n}\n";
    return true;
}

// ---------------------------------------------------------------- dispatch
bool ImportMesh(const std::string& path, Mesh& out, std::string& error) {
    std::string e = Ext(path);
    if (e == "obj") return ImportOBJ(path, out, error);
    if (e == "fbx") return ImportFBX(path, out, error);
    if (e == "gltf") return ImportGLTF(path, out, error, false);
    if (e == "glb") return ImportGLTF(path, out, error, true);
    if (e == "usd" || e == "usda" || e == "usdc") return ImportUSDA(path, out, error);
    error = "Unsupported format: ." + e; return false;
}

bool ExportMesh(const std::string& path, const Mesh& mesh, std::string& error) {
    std::string e = Ext(path);
    if (e == "obj") return ExportOBJ(path, mesh, error);
    if (e == "gltf") return ExportGLTF(path, mesh, nullptr, nullptr, 24, error, false);
    if (e == "glb") return ExportGLTF(path, mesh, nullptr, nullptr, 24, error, true);
    if (e == "usd" || e == "usda") return ExportUSDA(path, mesh, error);
    if (e == "fbx") { error = "FBX export isn't available yet - export OBJ, glTF or USDA instead."; return false; }
    error = "Unsupported export format: ." + e; return false;
}

bool ExportMeshAnimated(const std::string& path, const Mesh& mesh, const Armature* armature, const Action* action,
                        int fps, std::string& error) {
    std::string e = Ext(path);
    if (e == "gltf" || e == "glb") return ExportGLTF(path, mesh, armature, action, fps, error, e == "glb");
    return ExportMesh(path, mesh, error);
}
