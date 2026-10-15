#include "mesh/IO.h"
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

static bool ExportGLTF(const std::string& path, const Mesh& mesh, std::string& error, bool binary) {
    tinygltf::Model model; tinygltf::Scene scene; tinygltf::Mesh gmesh; tinygltf::Primitive prim;
    tinygltf::Buffer buffer;
    // Interleave positions then indices into one buffer.
    std::vector<unsigned char>& data = buffer.data;
    size_t posBytes = mesh.positions.size() * 12;
    data.resize(posBytes);
    std::memcpy(data.data(), mesh.positions.data(), posBytes);
    size_t idxOffset = data.size();
    std::vector<uint32_t> indices; indices.reserve(mesh.tris.size() * 3);
    for (const auto& t : mesh.tris) { indices.push_back(t.a); indices.push_back(t.b); indices.push_back(t.c); }
    data.resize(idxOffset + indices.size() * 4);
    std::memcpy(data.data() + idxOffset, indices.data(), indices.size() * 4);

    tinygltf::BufferView posView; posView.buffer = 0; posView.byteOffset = 0; posView.byteLength = posBytes; posView.target = TINYGLTF_TARGET_ARRAY_BUFFER;
    tinygltf::BufferView idxView; idxView.buffer = 0; idxView.byteOffset = idxOffset; idxView.byteLength = indices.size() * 4; idxView.target = TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER;
    tinygltf::Accessor posAcc; posAcc.bufferView = 0; posAcc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT; posAcc.count = mesh.positions.size(); posAcc.type = TINYGLTF_TYPE_VEC3;
    Float3 mn = mesh.bmin, mx = mesh.bmax; posAcc.minValues = {mn.x, mn.y, mn.z}; posAcc.maxValues = {mx.x, mx.y, mx.z};
    tinygltf::Accessor idxAcc; idxAcc.bufferView = 1; idxAcc.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT; idxAcc.count = indices.size(); idxAcc.type = TINYGLTF_TYPE_SCALAR;

    prim.attributes["POSITION"] = 0; prim.indices = 1; prim.mode = TINYGLTF_MODE_TRIANGLES;
    gmesh.primitives.push_back(prim);
    tinygltf::Node node; node.mesh = 0; scene.nodes.push_back(0);
    model.meshes.push_back(gmesh); model.nodes.push_back(node); model.scenes.push_back(scene);
    model.buffers.push_back(buffer); model.bufferViews = {posView, idxView}; model.accessors = {posAcc, idxAcc};
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
    if (e == "gltf") return ExportGLTF(path, mesh, error, false);
    if (e == "glb") return ExportGLTF(path, mesh, error, true);
    if (e == "usd" || e == "usda") return ExportUSDA(path, mesh, error);
    if (e == "fbx") { error = "FBX export isn't available yet - export OBJ, glTF or USDA instead."; return false; }
    error = "Unsupported export format: ." + e; return false;
}
