#include "mesh/MeshIO.h"
#include "ufbx.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace {
std::string Lower(std::string s) { for (char& c : s) c = static_cast<char>(std::tolower(c)); return s; }
std::string Ext(const std::string& path) {
    size_t dot = path.find_last_of('.');
    return dot == std::string::npos ? "" : Lower(path.substr(dot + 1));
}

// ------------------------------------------------------------------ OBJ
bool ImportOBJ(const std::string& path, Mesh& m, std::string& error) {
    std::ifstream f(path);
    if (!f) { error = "Could not open the file."; return false; }
    std::vector<Vec3> verts;
    std::string line;
    while (std::getline(f, line)) {
        if (line.size() < 2) continue;
        if (line[0] == 'v' && line[1] == ' ') {
            std::istringstream ss(line.substr(2)); Vec3 p; ss >> p.x >> p.y >> p.z; verts.push_back(p);
        } else if (line[0] == 'f' && line[1] == ' ') {
            std::istringstream ss(line.substr(2));
            std::string tok; std::vector<uint32_t> idx;
            while (ss >> tok) {
                int vi = std::atoi(tok.c_str());            // ignore /vt/vn
                if (vi < 0) vi = static_cast<int>(verts.size()) + vi + 1;
                if (vi >= 1 && vi <= static_cast<int>(verts.size())) idx.push_back(static_cast<uint32_t>(vi - 1));
            }
            if (idx.size() >= 3) m.AddFace(idx.data(), static_cast<uint32_t>(idx.size()));
        }
    }
    if (verts.empty()) { error = "No vertices found in the OBJ."; return false; }
    m.pos = std::move(verts);
    size_t slash = path.find_last_of("/\\");
    m.name = slash == std::string::npos ? path : path.substr(slash + 1);
    m.Finalize();
    return true;
}

// ------------------------------------------------------------------ FBX / glTF / USD via ufbx
// ufbx loads FBX, OBJ and (recent versions) can be extended; we use it for FBX only and keep
// dedicated readers for the others so behavior is predictable.
bool ImportFBX(const std::string& path, Mesh& m, std::string& error) {
    ufbx_load_opts opts{};
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters = 1.0f;
    opts.generate_missing_normals = true;
    ufbx_error err{};
    ufbx_scene* scene = ufbx_load_file(path.c_str(), &opts, &err);
    if (!scene) { error = std::string("FBX: ") + err.description.data; return false; }

    uint32_t base = 0;
    for (size_t mi = 0; mi < scene->meshes.count; ++mi) {
        ufbx_mesh* fm = scene->meshes.data[mi];
        for (size_t vi = 0; vi < fm->num_vertices; ++vi) {
            ufbx_vec3 v = fm->vertex_position.values.data[vi];
            m.AddVertex({(float)v.x, (float)v.y, (float)v.z});
        }
        for (size_t fi = 0; fi < fm->faces.count; ++fi) {
            ufbx_face face = fm->faces.data[fi];
            std::vector<uint32_t> idx;
            for (uint32_t c = 0; c < face.num_indices; ++c)
                idx.push_back(base + fm->vertex_indices.data[face.index_begin + c]);
            if (idx.size() >= 3) m.AddFace(idx.data(), static_cast<uint32_t>(idx.size()));
        }
        base = static_cast<uint32_t>(m.pos.size());
    }
    ufbx_free_scene(scene);
    if (m.pos.empty()) { error = "The FBX contains no mesh geometry."; return false; }
    size_t slash = path.find_last_of("/\\");
    m.name = slash == std::string::npos ? path : path.substr(slash + 1);
    m.Finalize();
    return true;
}

// ------------------------------------------------------------------ glTF / GLB (JSON + binary)
// A compact reader: pulls POSITION + indices from every mesh primitive. Enough to import geometry
// for editing; materials and animation are ignored at this milestone.
struct Bytes { std::vector<uint8_t> data; };
static bool ReadFile(const std::string& p, std::vector<uint8_t>& out) {
    std::ifstream f(p, std::ios::binary); if (!f) return false;
    f.seekg(0, std::ios::end); out.resize(static_cast<size_t>(f.tellg())); f.seekg(0);
    f.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    return true;
}
// Minimal JSON scanning helpers (the glTF JSON we need is simple and regular).
static size_t FindKey(const std::string& j, const std::string& key, size_t from = 0) {
    std::string q = "\"" + key + "\"";
    return j.find(q, from);
}
static long JsonInt(const std::string& j, const std::string& key, size_t from, long def) {
    size_t k = FindKey(j, key, from); if (k == std::string::npos) return def;
    size_t c = j.find(':', k); if (c == std::string::npos) return def;
    return std::strtol(j.c_str() + c + 1, nullptr, 10);
}

bool ImportGLTF(const std::string& path, Mesh& m, std::string& error) {
    std::vector<uint8_t> file;
    if (!ReadFile(path, file)) { error = "Could not open the file."; return false; }

    std::string json; std::vector<uint8_t> bin;
    if (file.size() > 12 && std::memcmp(file.data(), "glTF", 4) == 0) { // GLB container
        uint32_t p = 12;
        while (p + 8 <= file.size()) {
            uint32_t len; std::memcpy(&len, &file[p], 4);
            uint32_t type; std::memcpy(&type, &file[p + 4], 4);
            const uint8_t* chunk = &file[p + 8];
            if (type == 0x4E4F534Au) json.assign(reinterpret_cast<const char*>(chunk), len);       // JSON
            else if (type == 0x004E4942u) bin.assign(chunk, chunk + len);                           // BIN
            p += 8 + len;
        }
    } else {
        json.assign(file.begin(), file.end());
    }
    if (json.empty()) { error = "glTF JSON not found."; return false; }

    // Load external .bin buffers referenced by URI, plus data: URIs (base64) are skipped for brevity.
    std::string dir = path.substr(0, path.find_last_of("/\\") + 1);
    if (bin.empty()) {
        size_t b = FindKey(json, "buffers");
        size_t uriPos = json.find("\"uri\"", b);
        if (uriPos != std::string::npos) {
            size_t q1 = json.find('"', json.find(':', uriPos) + 1), q2 = json.find('"', q1 + 1);
            std::string uri = json.substr(q1 + 1, q2 - q1 - 1);
            if (uri.rfind("data:", 0) != 0) ReadFile(dir + uri, bin);
        }
    }

    // Parse accessors and bufferViews into flat arrays we can index.
    struct Accessor { long bufferView, count, componentType; std::string type; };
    std::vector<Accessor> accessors;
    size_t accBlock = FindKey(json, "accessors");
    if (accBlock != std::string::npos) {
        size_t p = json.find('[', accBlock), end = json.find(']', p);
        size_t o = p;
        while ((o = json.find('{', o)) != std::string::npos && o < end) {
            size_t oe = json.find('}', o);
            std::string obj = json.substr(o, oe - o);
            Accessor a;
            a.bufferView = JsonInt(obj, "bufferView", 0, -1);
            a.count = JsonInt(obj, "count", 0, 0);
            a.componentType = JsonInt(obj, "componentType", 0, 5126);
            size_t tp = FindKey(obj, "type");
            if (tp != std::string::npos) { size_t q1 = obj.find('"', obj.find(':', tp) + 1), q2 = obj.find('"', q1 + 1); a.type = obj.substr(q1 + 1, q2 - q1 - 1); }
            accessors.push_back(a);
            o = oe + 1;
        }
    }
    struct BV { long buffer, byteOffset, byteLength, byteStride; };
    std::vector<BV> views;
    size_t bvBlock = FindKey(json, "bufferViews");
    if (bvBlock != std::string::npos) {
        size_t p = json.find('[', bvBlock), end = json.find(']', p), o = p;
        while ((o = json.find('{', o)) != std::string::npos && o < end) {
            size_t oe = json.find('}', o); std::string obj = json.substr(o, oe - o);
            views.push_back({JsonInt(obj, "buffer", 0, 0), JsonInt(obj, "byteOffset", 0, 0), JsonInt(obj, "byteLength", 0, 0), JsonInt(obj, "byteStride", 0, 0)});
            o = oe + 1;
        }
    }
    auto accessorFloats = [&](long acc, int comps, std::vector<float>& out) {
        if (acc < 0 || acc >= (long)accessors.size()) return;
        const Accessor& a = accessors[acc];
        if (a.bufferView < 0 || a.bufferView >= (long)views.size()) return;
        const BV& v = views[a.bufferView];
        size_t stride = v.byteStride ? v.byteStride : comps * 4;
        out.resize(a.count * comps);
        for (long i = 0; i < a.count; ++i)
            std::memcpy(&out[i * comps], &bin[v.byteOffset + i * stride], comps * 4);
    };
    auto accessorIndices = [&](long acc, std::vector<uint32_t>& out) {
        if (acc < 0 || acc >= (long)accessors.size()) return;
        const Accessor& a = accessors[acc];
        if (a.bufferView < 0 || a.bufferView >= (long)views.size()) return;
        const BV& v = views[a.bufferView];
        out.resize(a.count);
        for (long i = 0; i < a.count; ++i) {
            if (a.componentType == 5125) { uint32_t x; std::memcpy(&x, &bin[v.byteOffset + i * 4], 4); out[i] = x; }
            else if (a.componentType == 5123) { uint16_t x; std::memcpy(&x, &bin[v.byteOffset + i * 2], 2); out[i] = x; }
            else { uint8_t x = bin[v.byteOffset + i]; out[i] = x; }
        }
    };

    // Walk every primitive: POSITION accessor + indices.
    size_t pr = 0; uint32_t base = 0;
    while ((pr = json.find("\"POSITION\"", pr)) != std::string::npos) {
        long posAcc = JsonInt(json, "POSITION", pr - 10 > 0 ? pr - 10 : 0, -1);
        // Re-scan the local primitive object for POSITION and indices accessors.
        size_t objStart = json.rfind('{', pr), objEnd = json.find('}', pr);
        std::string obj = json.substr(objStart, objEnd - objStart);
        long pa = JsonInt(obj, "POSITION", 0, -1);
        long ia = JsonInt(obj, "indices", 0, -1);
        std::vector<float> vpos; accessorFloats(pa, 3, vpos);
        for (size_t i = 0; i + 2 < vpos.size(); i += 3) m.AddVertex({vpos[i], vpos[i + 1], vpos[i + 2]});
        std::vector<uint32_t> idx; accessorIndices(ia, idx);
        if (idx.empty()) { for (uint32_t i = 0; i + 2 < vpos.size() / 3 * 3 / 3 * 3; i += 3) {} }
        for (size_t i = 0; i + 2 < idx.size(); i += 3) m.AddTri(base + idx[i], base + idx[i + 1], base + idx[i + 2]);
        base = static_cast<uint32_t>(m.pos.size());
        pr = objEnd;
        (void)posAcc;
    }
    if (m.pos.empty()) { error = "No POSITION data found in the glTF (external .bin may be missing)."; return false; }
    size_t slash = path.find_last_of("/\\");
    m.name = slash == std::string::npos ? path : path.substr(slash + 1);
    m.Finalize();
    return true;
}

// ------------------------------------------------------------------ USDA (ASCII USD subset)
bool ImportUSDA(const std::string& path, Mesh& m, std::string& error) {
    std::vector<uint8_t> raw;
    if (!ReadFile(path, raw)) { error = "Could not open the file."; return false; }
    if (raw.size() > 8 && std::memcmp(raw.data(), "PXR-USDC", 8) == 0) {
        error = "This is a binary .usdc file. Export it as .usda (ASCII) to import here."; return false;
    }
    std::string j(raw.begin(), raw.end());
    auto readVec3Array = [&](const std::string& key, std::vector<Vec3>& out) {
        size_t k = j.find(key); if (k == std::string::npos) return;
        size_t lb = j.find('[', k); size_t rb = j.find(']', lb);
        if (lb == std::string::npos || rb == std::string::npos) return;
        std::string body = j.substr(lb + 1, rb - lb - 1);
        size_t p = 0;
        while ((p = body.find('(', p)) != std::string::npos) {
            size_t e = body.find(')', p);
            Vec3 v; sscanf(body.c_str() + p + 1, "%f , %f , %f", &v.x, &v.y, &v.z);
            out.push_back(v); p = e + 1;
        }
    };
    auto readIntArray = [&](const std::string& key, std::vector<int>& out) {
        size_t k = j.find(key); if (k == std::string::npos) return;
        size_t lb = j.find('[', k); size_t rb = j.find(']', lb);
        if (lb == std::string::npos) return;
        std::string body = j.substr(lb + 1, rb - lb - 1);
        std::istringstream ss(body); std::string tok;
        while (std::getline(ss, tok, ',')) { try { out.push_back(std::stoi(tok)); } catch (...) {} }
    };
    std::vector<Vec3> pts; readVec3Array("point3f[] points", pts);
    if (pts.empty()) readVec3Array("points", pts);
    std::vector<int> counts, indices;
    readIntArray("faceVertexCounts", counts);
    readIntArray("faceVertexIndices", indices);
    if (pts.empty() || indices.empty()) { error = "No USD mesh geometry found."; return false; }
    for (auto& p : pts) m.AddVertex(p);
    size_t off = 0;
    if (counts.empty()) for (size_t i = 0; i + 2 < indices.size(); i += 3) m.AddTri(indices[i], indices[i + 1], indices[i + 2]);
    else for (int c : counts) {
        std::vector<uint32_t> face;
        for (int k = 0; k < c && off < indices.size(); ++k) face.push_back(static_cast<uint32_t>(indices[off++]));
        if (face.size() >= 3) m.AddFace(face.data(), static_cast<uint32_t>(face.size()));
    }
    size_t slash = path.find_last_of("/\\");
    m.name = slash == std::string::npos ? path : path.substr(slash + 1);
    m.Finalize();
    return true;
}
} // namespace

namespace MeshIO {

bool Import(const std::string& path, Mesh& out, std::string& error) {
    out.Clear();
    std::string ext = Ext(path);
    if (ext == "obj") return ImportOBJ(path, out, error);
    if (ext == "fbx") return ImportFBX(path, out, error);
    if (ext == "gltf" || ext == "glb") return ImportGLTF(path, out, error);
    if (ext == "usd" || ext == "usda" || ext == "usdc") return ImportUSDA(path, out, error);
    error = "Unsupported file type: ." + ext;
    return false;
}

bool ExportOBJ(const std::string& path, const Mesh& m, std::string& error) {
    std::ofstream f(path);
    if (!f) { error = "Could not write the file."; return false; }
    f << "# Exported by Leorix\n";
    for (const auto& p : m.pos) f << "v " << p.x << ' ' << p.y << ' ' << p.z << '\n';
    for (const auto& face : m.faces) {
        f << 'f';
        for (uint32_t i = 0; i < face.count; ++i) f << ' ' << (m.faceVerts[face.start + i] + 1);
        f << '\n';
    }
    return true;
}

bool ExportUSDA(const std::string& path, const Mesh& m, std::string& error) {
    std::ofstream f(path);
    if (!f) { error = "Could not write the file."; return false; }
    f << "#usda 1.0\n(\n    defaultPrim = \"Leorix\"\n    upAxis = \"Y\"\n    metersPerUnit = 1\n)\n\n";
    f << "def Xform \"Leorix\"\n{\n    def Mesh \"" << m.name << "\"\n    {\n";
    f << "        uniform bool doubleSided = 1\n";
    f << "        int[] faceVertexCounts = [";
    for (size_t i = 0; i < m.faces.size(); ++i) f << (i ? ", " : "") << m.faces[i].count;
    f << "]\n        int[] faceVertexIndices = [";
    for (size_t i = 0; i < m.faceVerts.size(); ++i) f << (i ? ", " : "") << m.faceVerts[i];
    f << "]\n        point3f[] points = [";
    for (size_t i = 0; i < m.pos.size(); ++i) { const Vec3& p = m.pos[i]; f << (i ? ", " : "") << '(' << p.x << ", " << p.y << ", " << p.z << ')'; }
    f << "]\n    }\n}\n";
    return true;
}

bool ExportGLTF(const std::string& path, const Mesh& m, std::string& error) {
    // Write a .glb: JSON chunk + binary chunk (positions + uint32 indices from the triangulation).
    std::vector<uint8_t> bin;
    auto append = [&](const void* p, size_t n) { const uint8_t* b = static_cast<const uint8_t*>(p); bin.insert(bin.end(), b, b + n); };
    AABB bb = m.bounds;
    size_t posOffset = 0;
    for (const auto& v : m.pos) append(&v, 12);
    while (bin.size() % 4) bin.push_back(0);
    size_t idxOffset = bin.size();
    for (uint32_t i : m.tris) append(&i, 4);
    while (bin.size() % 4) bin.push_back(0);

    char json[2048];
    std::snprintf(json, sizeof(json),
        "{\"asset\":{\"version\":\"2.0\",\"generator\":\"Leorix\"},"
        "\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}],"
        "\"buffers\":[{\"byteLength\":%zu}],"
        "\"bufferViews\":["
        "{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu,\"target\":34962},"
        "{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu,\"target\":34963}],"
        "\"accessors\":["
        "{\"bufferView\":0,\"componentType\":5126,\"count\":%zu,\"type\":\"VEC3\",\"min\":[%g,%g,%g],\"max\":[%g,%g,%g]},"
        "{\"bufferView\":1,\"componentType\":5125,\"count\":%zu,\"type\":\"SCALAR\"}]}",
        bin.size(), posOffset, m.pos.size() * 12, idxOffset, m.tris.size() * 4,
        m.pos.size(), bb.lo.x, bb.lo.y, bb.lo.z, bb.hi.x, bb.hi.y, bb.hi.z, m.tris.size());
    std::string jsonStr = json;
    while (jsonStr.size() % 4) jsonStr.push_back(' ');

    std::ofstream f(path, std::ios::binary);
    if (!f) { error = "Could not write the file."; return false; }
    auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<char*>(&v), 4); };
    uint32_t total = 12 + 8 + static_cast<uint32_t>(jsonStr.size()) + 8 + static_cast<uint32_t>(bin.size());
    f.write("glTF", 4); u32(2); u32(total);
    u32(static_cast<uint32_t>(jsonStr.size())); u32(0x4E4F534Au); f.write(jsonStr.data(), jsonStr.size());
    u32(static_cast<uint32_t>(bin.size())); u32(0x004E4942u); f.write(reinterpret_cast<char*>(bin.data()), bin.size());
    return true;
}

const char* ImportFilters() { return "obj;gltf;glb;fbx;usd;usda;usdc"; }
bool CanExport(const std::string& ext) {
    std::string e = Lower(ext);
    return e == "obj" || e == "gltf" || e == "glb" || e == "usd" || e == "usda";
}

} // namespace MeshIO
