#include "mesh/Primitives.h"
#include <algorithm>
#include <array>
#include <cmath>

static const float kPi = 3.14159265358979323846f;

const char* PrimitiveName(Primitive t) {
    switch (t) {
        case Primitive::Plane: return "Plane"; case Primitive::Cube: return "Cube";
        case Primitive::Circle: return "Circle"; case Primitive::UVSphere: return "UV Sphere";
        case Primitive::IcoSphere: return "Ico Sphere"; case Primitive::Cylinder: return "Cylinder";
        case Primitive::Cone: return "Cone"; case Primitive::Torus: return "Torus";
        case Primitive::Grid: return "Grid"; case Primitive::Monkey: return "Monkey";
    }
    return "";
}

PrimitiveParams DefaultParams(Primitive t) {
    PrimitiveParams p;
    switch (t) {
        case Primitive::Circle:    p.verticesRing = 32; p.radius = 1.0f; break;
        case Primitive::UVSphere:  p.verticesRing = 32; p.rings = 16; p.radius = 1.0f; break;
        case Primitive::IcoSphere: p.subdivisions = 2;  p.radius = 1.0f; break;
        case Primitive::Cylinder:
        case Primitive::Cone:      p.verticesRing = 32; p.radius = 1.0f; p.depth = 2.0f; break;
        case Primitive::Torus:     p.verticesRing = 48; p.rings = 12; p.radius = 1.0f; p.radius2 = 0.25f; break;
        case Primitive::Grid:      p.gridX = 10; p.gridY = 10; p.size = 2.0f; break;
        default:                   p.size = 2.0f; break;   // Plane, Cube
    }
    return p;
}

namespace {
// Builds meshes as polygons (quads/tris/n-gons), then triangulates with a fan while
// tagging every triangle with its source polygon id, so edit mode can rebuild the quad
// wireframe. This is exactly how Blender treats its primitives: quad-based, un-subdivided.
struct Builder {
    Mesh m;
    uint32_t poly = 0;
    uint32_t addVert(float x, float y, float z, float u = 0, float v = 0) {
        m.positions.push_back({x, y, z}); m.uvs.push_back({u, v});
        return static_cast<uint32_t>(m.positions.size() - 1);
    }
    void face(std::initializer_list<uint32_t> verts) {
        uint32_t idx[16]; int n = 0;
        for (uint32_t vi : verts) if (n < 16) idx[n++] = vi;
        for (int i = 1; i + 1 < n; ++i) m.tris.push_back({idx[0], idx[i], idx[i + 1], poly});
        ++poly;
    }
    Mesh finish() { m.polyCount = poly; m.RecenterAndNormalize(m.bmax.y - m.bmin.y > 0 ? std::max({m.bmax.x - m.bmin.x, m.bmax.y - m.bmin.y, m.bmax.z - m.bmin.z}) : 2.0f); return m; }
    Mesh finishKeepScale() { m.polyCount = poly; m.ComputeBounds(); m.ComputeNormals(); return m; }
};
}

Mesh BuildPrimitive(Primitive type, const PrimitiveParams& p) {
    Builder b;
    const float h = p.size * 0.5f;
    switch (type) {
    case Primitive::Plane: {
        // Blender plane: 2 m, one quad, verts CCW from (-,-). UVs 0..1.
        uint32_t v0 = b.addVert(-h, 0, -h, 0, 0), v1 = b.addVert(h, 0, -h, 1, 0);
        uint32_t v2 = b.addVert(h, 0, h, 1, 1),  v3 = b.addVert(-h, 0, h, 0, 1);
        b.face({v0, v1, v2, v3});
        break;
    }
    case Primitive::Cube: {
        // 8 corners, 6 quad faces, matching Blender's vertex and face order.
        uint32_t v[8];
        int i = 0;
        for (int z = -1; z <= 1; z += 2) for (int y = -1; y <= 1; y += 2) for (int x = -1; x <= 1; x += 2)
            v[i++] = b.addVert(x * h, y * h, z * h);
        // Blender face order: -X, +X, -Y, +Y, -Z, +Z (quads, outward winding)
        // Rings run counter-clockwise seen from outside, so the face normals point outward.
        b.face({v[4], v[6], v[2], v[0]}); // -X
        b.face({v[7], v[5], v[1], v[3]}); // +X
        b.face({v[1], v[5], v[4], v[0]}); // -Y
        b.face({v[6], v[7], v[3], v[2]}); // +Y
        b.face({v[2], v[3], v[1], v[0]}); // -Z
        b.face({v[5], v[7], v[6], v[4]}); // +Z
        break;
    }
    case Primitive::Circle: {
        int N = std::max(3, p.verticesRing);
        std::vector<uint32_t> ring(N);
        for (int i = 0; i < N; ++i) { float a = 2 * kPi * i / N; ring[i] = b.addVert(std::cos(a) * p.radius, 0, std::sin(a) * p.radius); }
        if (p.capFill) {
            for (int i = 1; i + 1 < N; ++i) b.m.tris.push_back({ring[0], ring[N - i], ring[N - i - 1], b.poly});
            b.poly++;
        }
        break;
    }
    case Primitive::UVSphere: {
        // Blender UV sphere: 32 segments x 16 rings, quad body with triangle fans at the poles.
        int seg = std::max(3, p.verticesRing), ringN = std::max(2, p.rings);
        uint32_t top = b.addVert(0, p.radius, 0), bot = b.addVert(0, -p.radius, 0);
        std::vector<std::vector<uint32_t>> rows(ringN - 1, std::vector<uint32_t>(seg));
        for (int r = 0; r < ringN - 1; ++r) {
            float phi = kPi * (r + 1) / ringN, y = std::cos(phi) * p.radius, rad = std::sin(phi) * p.radius;
            for (int s = 0; s < seg; ++s) { float th = 2 * kPi * s / seg; rows[r][s] = b.addVert(std::cos(th) * rad, y, std::sin(th) * rad); }
        }
        for (int s = 0; s < seg; ++s) b.face({top, rows[0][(s + 1) % seg], rows[0][s]});         // top fan
        for (int r = 0; r < ringN - 2; ++r) for (int s = 0; s < seg; ++s)                          // quad body
            b.face({rows[r][s], rows[r][(s + 1) % seg], rows[r + 1][(s + 1) % seg], rows[r + 1][s]});
        for (int s = 0; s < seg; ++s) b.face({bot, rows[ringN - 2][s], rows[ringN - 2][(s + 1) % seg]}); // bottom fan
        break;
    }
    case Primitive::IcoSphere: {
        // Real icosahedron subdivided N times: even, all-triangle topology (Blender's ico sphere).
        const float t = (1 + std::sqrt(5.0f)) / 2;
        std::vector<Float3> v = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
                                 {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
        std::vector<std::array<int, 3>> f = {{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11},
            {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8}, {3, 9, 4}, {3, 4, 2}, {3, 2, 6},
            {3, 6, 8}, {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
        int subs = std::clamp(p.subdivisions, 1, 6);
        for (int it = 1; it < subs; ++it) {
            std::vector<std::array<int, 3>> nf;
            std::vector<std::pair<uint64_t, int>> cache;
            auto mid = [&](int a, int c) {
                uint64_t key = a < c ? (uint64_t(a) << 32) | c : (uint64_t(c) << 32) | a;
                for (auto& kv : cache) if (kv.first == key) return kv.second;
                Float3 m{(v[a].x + v[c].x) / 2, (v[a].y + v[c].y) / 2, (v[a].z + v[c].z) / 2};
                v.push_back(m); int id = static_cast<int>(v.size() - 1); cache.push_back({key, id}); return id;
            };
            for (auto& tr : f) { int a = mid(tr[0], tr[1]), bb = mid(tr[1], tr[2]), cc = mid(tr[2], tr[0]);
                nf.push_back({tr[0], a, cc}); nf.push_back({tr[1], bb, a}); nf.push_back({tr[2], cc, bb}); nf.push_back({a, bb, cc}); }
            f.swap(nf);
        }
        std::vector<uint32_t> ids(v.size());
        for (size_t i = 0; i < v.size(); ++i) { float l = std::sqrt(v[i].x * v[i].x + v[i].y * v[i].y + v[i].z * v[i].z);
            ids[i] = b.addVert(v[i].x / l * p.radius, v[i].y / l * p.radius, v[i].z / l * p.radius); }
        for (auto& tr : f) b.face({ids[tr[0]], ids[tr[1]], ids[tr[2]]});
        break;
    }
    case Primitive::Cylinder: case Primitive::Cone: {
        int N = std::max(3, p.verticesRing); float d = p.depth * 0.5f;
        bool cone = type == Primitive::Cone;
        std::vector<uint32_t> bottom(N), topR(cone ? 0 : N);
        for (int i = 0; i < N; ++i) { float a = 2 * kPi * i / N, cx = std::cos(a) * p.radius, cz = std::sin(a) * p.radius;
            bottom[i] = b.addVert(cx, -d, cz); if (!cone) topR[i] = b.addVert(cx, d, cz); }
        if (cone) { uint32_t apex = b.addVert(0, d, 0); for (int i = 0; i < N; ++i) b.face({bottom[(i + 1) % N], bottom[i], apex}); }
        else for (int i = 0; i < N; ++i) b.face({topR[i], topR[(i + 1) % N], bottom[(i + 1) % N], bottom[i]});
        if (p.capFill) {
            uint32_t idx[512]; int n = 0; for (int i = 0; i < N; ++i) if (n < 512) idx[n++] = bottom[i];
            for (int i = 1; i + 1 < n; ++i) b.m.tris.push_back({idx[0], idx[i], idx[i + 1], b.poly}); b.poly++;
            if (!cone) { n = 0; for (int i = N - 1; i >= 0; --i) if (n < 512) idx[n++] = topR[i];
                for (int i = 1; i + 1 < n; ++i) b.m.tris.push_back({idx[0], idx[i], idx[i + 1], b.poly}); b.poly++; }
        }
        break;
    }
    case Primitive::Torus: {
        int major = std::max(3, p.verticesRing), minor = std::max(3, p.rings);
        std::vector<std::vector<uint32_t>> g(major, std::vector<uint32_t>(minor));
        for (int i = 0; i < major; ++i) { float u = 2 * kPi * i / major;
            for (int j = 0; j < minor; ++j) { float v = 2 * kPi * j / minor;
                float cx = (p.radius + p.radius2 * std::cos(v)) * std::cos(u);
                float cz = (p.radius + p.radius2 * std::cos(v)) * std::sin(u);
                float cy = p.radius2 * std::sin(v);
                g[i][j] = b.addVert(cx, cy, cz); } }
        for (int i = 0; i < major; ++i) for (int j = 0; j < minor; ++j)
            b.face({g[i][(j + 1) % minor], g[(i + 1) % major][(j + 1) % minor], g[(i + 1) % major][j], g[i][j]});
        break;
    }
    case Primitive::Grid: {
        // Blender counts subdivisions (cuts), so 10 x 10 gives 11 x 11 vertices and 100 faces.
        int nx = std::max(2, p.gridX + 1), ny = std::max(2, p.gridY + 1);
        std::vector<std::vector<uint32_t>> g(ny, std::vector<uint32_t>(nx));
        for (int y = 0; y < ny; ++y) for (int x = 0; x < nx; ++x)
            g[y][x] = b.addVert((x / float(nx - 1) - 0.5f) * p.size, 0, (y / float(ny - 1) - 0.5f) * p.size, x / float(nx - 1), y / float(ny - 1));
        for (int y = 0; y < ny - 1; ++y) for (int x = 0; x < nx - 1; ++x)
            b.face({g[y][x], g[y][x + 1], g[y + 1][x + 1], g[y + 1][x]});
        break;
    }
    case Primitive::Monkey: {
        // A blocky stand-in until the real Suzanne vertex table is embedded (Milestone note).
        PrimitiveParams cp; cp.verticesRing = 16; cp.rings = 8; cp.radius = 1.0f;
        Mesh head = BuildPrimitive(Primitive::UVSphere, cp);
        return head;
    }
    }
    return type == Primitive::UVSphere || type == Primitive::IcoSphere || type == Primitive::Torus ? b.finishKeepScale() : b.finishKeepScale();
}
