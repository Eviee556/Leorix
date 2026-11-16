#include "physics/Physics.h"
#include "mesh/Scene.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

const char* RigidShapeName(RigidShape s) {
    switch (s) {
        case RigidShape::Box:    return "Box";
        case RigidShape::Sphere: return "Sphere";
        default:                 return "Convex Hull";
    }
}

namespace {
struct V { float x = 0, y = 0, z = 0; };
V operator+(V a, V b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V operator-(V a, V b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V operator-(V a) { return {-a.x, -a.y, -a.z}; }
V operator*(V a, float s) { return {a.x * s, a.y * s, a.z * s}; }
V Mul(V a, V b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
float Dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V Cross(V a, V b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float Len(V a) { return std::sqrt(Dot(a, a)); }
V ToV(Float3 f) { return {f.x, f.y, f.z}; }
Float3 ToF(V v) { return {v.x, v.y, v.z}; }

struct Q { float w = 1, x = 0, y = 0, z = 0; };
Q operator*(Q a, Q b) {
    return {a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z, a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x, a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}
Q Normalized(Q q) {
    float l = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    return l > 0 ? Q{q.w / l, q.x / l, q.y / l, q.z / l} : Q{};
}
// Transform::Apply rotates about X, then Y, then Z.
Q FromEuler(Float3 e) {
    Q qx{std::cos(e.x / 2), std::sin(e.x / 2), 0, 0}, qy{std::cos(e.y / 2), 0, std::sin(e.y / 2), 0},
      qz{std::cos(e.z / 2), 0, 0, std::sin(e.z / 2)};
    return qz * qy * qx;
}

struct M3 {
    float r[3][3];
    V operator*(V v) const { return {r[0][0] * v.x + r[0][1] * v.y + r[0][2] * v.z, r[1][0] * v.x + r[1][1] * v.y + r[1][2] * v.z,
                                     r[2][0] * v.x + r[2][1] * v.y + r[2][2] * v.z}; }
    V T(V v) const { return {r[0][0] * v.x + r[1][0] * v.y + r[2][0] * v.z, r[0][1] * v.x + r[1][1] * v.y + r[2][1] * v.z,
                             r[0][2] * v.x + r[1][2] * v.y + r[2][2] * v.z}; }
};
M3 FromQ(Q q) {
    float w = q.w, x = q.x, y = q.y, z = q.z;
    return {{{1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)},
             {2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)},
             {2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)}}};
}
// Inverse of FromEuler for R = Rz Ry Rx.
Float3 ToEuler(const M3& m) {
    float sy = std::clamp(-m.r[2][0], -1.0f, 1.0f);
    float y = std::asin(sy);
    if (std::fabs(sy) < 0.99999f) return {std::atan2(m.r[2][1], m.r[2][2]), y, std::atan2(m.r[1][0], m.r[0][0])};
    return {std::atan2(-m.r[1][2], m.r[1][1]), y, 0};
}

struct Plane { V n; float d; };   // points p with Dot(n, p) <= d are inside

struct Shape {
    RigidShape type = RigidShape::Box;
    V half;               // box half extents
    float radius = 0;     // sphere radius, otherwise the bounding radius
    V com;                // centre in scaled object space
    V inertia;            // diagonal inertia per kg
    std::vector<V> verts; // relative to com
    std::vector<Plane> planes;
};

void MakeBox(Shape& s) {
    s.type = RigidShape::Box;
    V h = s.half;
    s.verts.clear(); s.planes.clear();
    for (int i = 0; i < 8; ++i) s.verts.push_back({i & 1 ? h.x : -h.x, i & 2 ? h.y : -h.y, i & 4 ? h.z : -h.z});
    s.planes = {{{1, 0, 0}, h.x}, {{-1, 0, 0}, h.x}, {{0, 1, 0}, h.y}, {{0, -1, 0}, h.y}, {{0, 0, 1}, h.z}, {{0, 0, -1}, h.z}};
    s.radius = Len(h);
}

// Hull faces by brute force: a plane through three points is a face when every point is on one side.
// shortcut: O(n^4) on at most ~100 support points, fine for one bake; use quickhull if bakes get slow.
std::vector<Plane> HullPlanes(const std::vector<V>& p) {
    std::vector<Plane> out;
    const size_t n = p.size();
    for (size_t i = 0; i < n; ++i)
        for (size_t j = i + 1; j < n; ++j)
            for (size_t k = j + 1; k < n; ++k) {
                V nn = Cross(p[j] - p[i], p[k] - p[i]);
                float l = Len(nn);
                if (l < 1e-8f) continue;
                nn = nn * (1 / l);
                float d = Dot(nn, p[i]), lo = 0, hi = 0;
                for (const V& q : p) { float s = Dot(nn, q) - d; lo = std::min(lo, s); hi = std::max(hi, s); }
                const float eps = 1e-4f;
                if (hi > eps && lo < -eps) continue;
                if (hi > eps) { nn = -nn; d = -d; }
                bool dup = false;
                for (const Plane& e : out) dup |= Dot(e.n, nn) > 0.9999f && std::fabs(e.d - d) < 1e-3f;
                if (!dup) out.push_back({nn, d});
            }
    return out;
}

Shape BuildShape(const MeshObject& o, RigidShape type) {
    const Mesh& m = o.hasDisplay ? o.displayMesh : o.mesh;
    V sc = ToV(o.transform.scale);
    std::vector<V> pts;
    pts.reserve(m.positions.size());
    for (const Float3& f : m.positions) pts.push_back(Mul(ToV(f), sc));
    if (pts.empty()) pts = {{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}};
    V mn = pts[0], mx = pts[0];
    for (const V& p : pts) {
        mn = {std::min(mn.x, p.x), std::min(mn.y, p.y), std::min(mn.z, p.z)};
        mx = {std::max(mx.x, p.x), std::max(mx.y, p.y), std::max(mx.z, p.z)};
    }
    Shape s;
    s.com = (mn + mx) * 0.5f;
    V half = (mx - mn) * 0.5f;
    const float minHalf = 0.005f;   // a flat plane still needs some thickness to collide with
    bool flat = half.x < minHalf || half.y < minHalf || half.z < minHalf;
    s.half = {std::max(half.x, minHalf), std::max(half.y, minHalf), std::max(half.z, minHalf)};
    V h2 = Mul(s.half, s.half);
    s.inertia = {(h2.y + h2.z) / 3, (h2.x + h2.z) / 3, (h2.x + h2.y) / 3};   // solid box
    if (type == RigidShape::Sphere) {
        s.type = RigidShape::Sphere;
        for (const V& p : pts) s.radius = std::max(s.radius, Len(p - s.com));
        s.radius = std::max(s.radius, minHalf);
        float i = 0.4f * s.radius * s.radius;
        s.inertia = {i, i, i};
        return s;
    }
    if (type == RigidShape::Box || flat) { MakeBox(s); return s; }

    // Convex hull: reduce to the points that are extreme along a spread of directions.
    std::vector<V> cand;
    for (const V& p : pts) cand.push_back(p - s.com);
    if (cand.size() > 64) {
        std::vector<char> keep(cand.size(), 0);
        const int nd = 100;
        for (int d = 0; d < nd + 6; ++d) {
            V dir;
            if (d < nd) {   // Fibonacci sphere
                float y = 1 - 2 * (d + 0.5f) / nd, r = std::sqrt(1 - y * y), phi = d * 2.39996323f;
                dir = {r * std::cos(phi), y, r * std::sin(phi)};
            } else {
                int a = (d - nd) / 2; float sgn = (d - nd) % 2 ? -1.0f : 1.0f;
                dir = {a == 0 ? sgn : 0, a == 1 ? sgn : 0, a == 2 ? sgn : 0};
            }
            size_t best = 0;
            for (size_t i = 1; i < cand.size(); ++i) if (Dot(cand[i], dir) > Dot(cand[best], dir)) best = i;
            keep[best] = 1;
        }
        std::vector<V> red;
        for (size_t i = 0; i < cand.size(); ++i) if (keep[i]) red.push_back(cand[i]);
        cand.swap(red);
    }
    s.planes = HullPlanes(cand);
    if (s.planes.size() < 4) { MakeBox(s); return s; }
    s.type = RigidShape::ConvexHull;
    s.verts = cand;
    for (const V& p : cand) s.radius = std::max(s.radius, Len(p));
    return s;
}

struct Body {
    MeshObject* obj = nullptr;
    Shape shape;
    bool dynamic = false, sleeping = false;
    int  stillSteps = 0;
    V x, v, w;
    Q q;
    float invMass = 0;
    V invI;               // local, diagonal
    float friction = 0.5f, restitution = 0;
    // world-space copies, refreshed each step
    M3 R;
    std::vector<V> wv;
    std::vector<Plane> wp;

    bool Moving() const { return dynamic && !sleeping; }
    float InvMass() const { return Moving() ? invMass : 0; }
    V InvInertia(V t) const { return Moving() ? R * Mul(invI, R.T(t)) : V{}; }
    V VelAt(V r) const { return v + Cross(w, r); }
    void UpdateWorld() {
        R = FromQ(q);
        wv.resize(shape.verts.size());
        wp.resize(shape.planes.size());
        for (size_t i = 0; i < wv.size(); ++i) wv[i] = R * shape.verts[i] + x;
        for (size_t i = 0; i < wp.size(); ++i) { V n = R * shape.planes[i].n; wp[i] = {n, shape.planes[i].d + Dot(n, x)}; }
    }
};

struct Contact {
    int a, b;
    V p, n;            // n points from a to b
    float pen;
    V ra, rb, t1, t2;
    float mN = 0, mT1 = 0, mT2 = 0, target = 0, mu = 0;
    float jn = 0, jt1 = 0, jt2 = 0;
};

constexpr float kMargin = 0.002f;     // contacts kept this far apart, so resting bodies stay in touch
constexpr float kSlop = 0.001f;       // penetration allowed before position correction pushes back
constexpr float kBeta = 0.2f;
constexpr float kBounceSpeed = 1.0f;  // slower impacts do not bounce, so resting bodies settle

void AddContact(std::vector<Contact>& out, int a, int b, V p, V n, float pen) {
    Contact c; c.a = a; c.b = b; c.p = p; c.n = n; c.pen = pen;
    out.push_back(c);
}

bool Inside(const std::vector<Plane>& planes, V p) {
    for (const Plane& pl : planes) if (Dot(pl.n, p) - pl.d > kMargin) return false;
    return true;
}

// Face-normal SAT, then every vertex of one body inside the other becomes a contact.
// shortcut: no edge-edge axes, so two boxes touching only edge to edge (no vertex inside) pass
// through each other; add edge contacts if crossed-edge stacking matters.
void PolyPoly(const std::vector<Body>& bs, int ia, int ib, std::vector<Contact>& out) {
    const Body& A = bs[ia]; const Body& B = bs[ib];
    float best = -1e30f; V N;
    for (const Plane& pl : A.wp) {
        float s = 1e30f;
        for (const V& v : B.wv) s = std::min(s, Dot(pl.n, v) - pl.d);
        if (s > kMargin) return;
        if (s > best) { best = s; N = pl.n; }
    }
    for (const Plane& pl : B.wp) {
        float s = 1e30f;
        for (const V& v : A.wv) s = std::min(s, Dot(pl.n, v) - pl.d);
        if (s > kMargin) return;
        if (s > best) { best = s; N = -pl.n; }
    }
    auto ref = [](const std::vector<Plane>& ps, V dir) {
        const Plane* r = &ps[0];
        for (const Plane& pl : ps) if (Dot(pl.n, dir) > Dot(r->n, dir)) r = &pl;
        return *r;
    };
    Plane ra = ref(A.wp, N), rb = ref(B.wp, -N);
    const size_t first = out.size();
    for (const V& v : B.wv) if (Inside(A.wp, v)) AddContact(out, ia, ib, v, N, ra.d - Dot(ra.n, v));
    for (const V& u : A.wv) if (Inside(B.wp, u)) AddContact(out, ia, ib, u, N, rb.d - Dot(rb.n, u));
    if (out.size() - first > 16) {   // many hull vertices: keep the deepest
        std::sort(out.begin() + first, out.end(), [](const Contact& l, const Contact& r) { return l.pen > r.pen; });
        out.resize(first + 16);
    }
}

// Sphere `is` against polyhedron `ip`; the contact normal points from ia to ib.
void SpherePoly(const std::vector<Body>& bs, int is, int ip, int ia, int ib, std::vector<Contact>& out) {
    const Body& S = bs[is]; const Body& P = bs[ip];
    const float r = S.shape.radius;
    V n, p; float pen;
    if (P.shape.type == RigidShape::Box) {
        V c = P.R.T(S.x - P.x), h = P.shape.half;
        V qc{std::clamp(c.x, -h.x, h.x), std::clamp(c.y, -h.y, h.y), std::clamp(c.z, -h.z, h.z)};
        V d = c - qc;
        float dist = Len(d);
        if (dist > 1e-6f) {
            if (dist > r + kMargin) return;
            n = P.R * (d * (1 / dist)); pen = r - dist; p = P.R * qc + P.x;
        } else {   // centre inside the box: push out through the nearest face
            float dx = h.x - std::fabs(c.x), dy = h.y - std::fabs(c.y), dz = h.z - std::fabs(c.z);
            V ln = dx <= dy && dx <= dz ? V{c.x < 0 ? -1.0f : 1.0f, 0, 0}
                 : dy <= dz ? V{0, c.y < 0 ? -1.0f : 1.0f, 0} : V{0, 0, c.z < 0 ? -1.0f : 1.0f};
            float depth = std::min({dx, dy, dz});
            n = P.R * ln; pen = r + depth; p = S.x;
        }
    } else {
        // shortcut: nearest face plane only, so near hull edges the sphere collides a little early.
        float s = -1e30f;
        for (const Plane& pl : P.wp) { float d = Dot(pl.n, S.x) - pl.d; if (d > s) { s = d; n = pl.n; } }
        if (s > r + kMargin) return;
        pen = r - s; p = S.x - n * s;
    }
    AddContact(out, ia, ib, p, ia == ip ? n : -n, pen);
}

void Collide(std::vector<Body>& bs, int a, int b, std::vector<Contact>& out) {
    const Body& A = bs[a]; const Body& B = bs[b];
    bool sa = A.shape.type == RigidShape::Sphere, sb = B.shape.type == RigidShape::Sphere;
    if (sa && sb) {
        V d = B.x - A.x;
        float dist = Len(d), rr = A.shape.radius + B.shape.radius;
        if (dist > rr + kMargin) return;
        V n = dist > 1e-6f ? d * (1 / dist) : V{0, 1, 0};
        AddContact(out, a, b, A.x + n * A.shape.radius, n, rr - dist);
    } else if (sa) SpherePoly(bs, a, b, a, b, out);
    else if (sb) SpherePoly(bs, b, a, a, b, out);
    else PolyPoly(bs, a, b, out);
}

void ApplyImpulse(Body& A, Body& B, const Contact& c, V j) {
    A.v = A.v - j * A.InvMass(); A.w = A.w - A.InvInertia(Cross(c.ra, j));
    B.v = B.v + j * B.InvMass(); B.w = B.w + B.InvInertia(Cross(c.rb, j));
}

float EffMass(const Body& A, const Body& B, V ra, V rb, V n) {
    float k = A.InvMass() + B.InvMass() + Dot(n, Cross(A.InvInertia(Cross(ra, n)), ra)) +
              Dot(n, Cross(B.InvInertia(Cross(rb, n)), rb));
    return k > 1e-12f ? 1 / k : 0;
}

void Step(std::vector<Body>& bs, V g, float dt) {
    for (Body& b : bs) if (b.Moving()) b.v = b.v + g * dt;
    for (Body& b : bs) b.UpdateWorld();

    std::vector<Contact> cs;
    for (int i = 0; i < (int)bs.size(); ++i)
        for (int j = i + 1; j < (int)bs.size(); ++j) {
            Body& A = bs[i]; Body& B = bs[j];
            if (!A.Moving() && !B.Moving()) continue;
            if (Len(B.x - A.x) > A.shape.radius + B.shape.radius + kMargin) continue;
            // a fast body hitting a sleeping one wakes it
            for (Body* s : {&A, &B}) {
                Body& o = s == &A ? B : A;
                if (s->dynamic && s->sleeping && o.Moving() && Dot(o.v, o.v) > 0.04f) { s->sleeping = false; s->stillSteps = 0; }
            }
            Collide(bs, i, j, cs);
        }

    for (Contact& c : cs) {
        Body& A = bs[c.a]; Body& B = bs[c.b];
        c.ra = c.p - A.x; c.rb = c.p - B.x;
        c.t1 = std::fabs(c.n.x) > 0.57f ? Cross(c.n, V{0, 1, 0}) : Cross(c.n, V{1, 0, 0});
        c.t1 = c.t1 * (1 / Len(c.t1));
        c.t2 = Cross(c.n, c.t1);
        c.mN = EffMass(A, B, c.ra, c.rb, c.n);
        c.mT1 = EffMass(A, B, c.ra, c.rb, c.t1);
        c.mT2 = EffMass(A, B, c.ra, c.rb, c.t2);
        c.mu = std::sqrt(std::max(A.friction, 0.0f) * std::max(B.friction, 0.0f));
        float vn = Dot(B.VelAt(c.rb) - A.VelAt(c.ra), c.n);
        c.target = c.pen > kSlop ? kBeta * (c.pen - kSlop) / dt : (c.pen < 0 ? c.pen / dt : 0);
        float e = std::max(A.restitution, B.restitution);
        if (vn < -kBounceSpeed) c.target = std::max(c.target, -e * vn);
    }
    for (int it = 0; it < 20; ++it)
        for (Contact& c : cs) {
            Body& A = bs[c.a]; Body& B = bs[c.b];
            V vr = B.VelAt(c.rb) - A.VelAt(c.ra);
            float jn = std::max(c.jn + c.mN * (c.target - Dot(vr, c.n)), 0.0f);
            ApplyImpulse(A, B, c, c.n * (jn - c.jn));
            c.jn = jn;
            float lim = c.mu * c.jn;
            vr = B.VelAt(c.rb) - A.VelAt(c.ra);
            float j1 = std::clamp(c.jt1 - c.mT1 * Dot(vr, c.t1), -lim, lim);
            float j2 = std::clamp(c.jt2 - c.mT2 * Dot(vr, c.t2), -lim, lim);
            ApplyImpulse(A, B, c, c.t1 * (j1 - c.jt1) + c.t2 * (j2 - c.jt2));
            c.jt1 = j1; c.jt2 = j2;
        }

    const int sleepAfter = (int)std::ceil(0.5f / dt);
    for (Body& b : bs) {
        if (!b.Moving()) continue;
        b.x = b.x + b.v * dt;
        Q wq{0, b.w.x, b.w.y, b.w.z};
        Q dq = wq * b.q;
        b.q = Normalized(Q{b.q.w + 0.5f * dt * dq.w, b.q.x + 0.5f * dt * dq.x, b.q.y + 0.5f * dt * dq.y, b.q.z + 0.5f * dt * dq.z});
        if (Dot(b.v, b.v) < 0.0025f && Dot(b.w, b.w) < 0.01f) {
            if (++b.stillSteps > sleepAfter) { b.sleeping = true; b.v = b.w = V{}; }
        } else b.stillSteps = 0;
    }
}

RigidTransform Pose(const Body& b) {
    M3 R = FromQ(b.q);
    return {ToF(b.x - R * b.shape.com), ToEuler(R)};
}

// Writes cloth positions into the base mesh and asks the viewport and modifiers to catch up.
void SetClothPositions(MeshObject& o, const std::vector<Float3>& p) {
    if (p.size() != o.mesh.positions.size()) return;
    o.mesh.positions = p;
    o.mesh.ComputeNormals();
    o.MarkTopoDirty();
    o.stackDirty = true;
}

// A collider for cloth: a rigid body's shape posed at one frame.
struct Collider {
    Shape shape;
    V x;      // shape centre in world space
    M3 R;
};
Collider PoseCollider(const MeshObject& o, const Shape& sh, int frame) {
    const RigidBody& rb = o.rigidBody;
    RigidTransform t{o.transform.position, o.transform.rotation};
    if (rb.Baked()) t = rb.cache[std::clamp(frame - rb.cacheStart, 0, (int)rb.cache.size() - 1)];
    Collider c;
    c.shape = sh;
    c.R = FromQ(FromEuler(t.rotation));
    c.x = c.R * sh.com + ToV(t.position);
    return c;
}
// Pushes p out of the collider to `thick` from its surface. Returns the push direction, or zero.
V PushOut(const Collider& c, V& p, float thick) {
    V l = c.R.T(p - c.x);
    if (c.shape.type == RigidShape::Sphere) {
        float d = Len(l), want = c.shape.radius + thick;
        if (d >= want) return {};
        V n = d > 1e-6f ? l * (1 / d) : V{0, 1, 0};
        p = c.R * (n * want) + c.x;
        return c.R * n;
    }
    // Box and hull: the plane the point is furthest outside of. It is within `thick` of the
    // convex shape only when every plane says so.
    float best = -1e30f; V n;
    for (const Plane& pl : c.shape.planes) { float d = Dot(pl.n, l) - pl.d; if (d > best) { best = d; n = pl.n; } }
    if (c.shape.planes.empty() || best >= thick) return {};
    p = c.R * (l + n * (thick - best)) + c.x;
    return c.R * n;
}

struct Link { uint32_t i, j; float rest; };
} // namespace

int BakeRigidBodies(Scene& scene, const PhysicsSettings& s) {
    std::vector<Body> bodies;
    for (auto& up : scene.objects) {
        MeshObject& o = *up;
        RigidBody& rb = o.rigidBody;
        if (rb.Baked()) { o.transform.position = rb.rest.position; o.transform.rotation = rb.rest.rotation; }
        rb.cache.clear();
        if (!rb.enabled || o.isCurve || o.cloth.enabled) continue;   // a cloth object is simulated as cloth
        rb.rest = {o.transform.position, o.transform.rotation};
        rb.cacheStart = s.frameStart;
        Body b;
        b.obj = &o;
        b.shape = BuildShape(o, rb.shape);
        b.dynamic = !rb.passive;
        b.q = FromEuler(o.transform.rotation);
        b.x = FromQ(b.q) * b.shape.com + ToV(o.transform.position);
        if (b.dynamic) {
            float m = std::clamp(std::isfinite(rb.mass) ? rb.mass : 1.0f, 0.001f, 1e6f);
            b.invMass = 1 / m;
            b.invI = {1 / (m * std::max(b.shape.inertia.x, 1e-8f)), 1 / (m * std::max(b.shape.inertia.y, 1e-8f)),
                      1 / (m * std::max(b.shape.inertia.z, 1e-8f))};
        }
        b.friction = std::clamp(rb.friction, 0.0f, 10.0f);
        b.restitution = std::clamp(rb.bounciness, 0.0f, 1.0f);
        bodies.push_back(std::move(b));
    }
    int moving = 0;
    for (const Body& b : bodies) moving += b.dynamic;
    if (!moving) return 0;

    const int fps = std::clamp(s.fps, 1, 1000);
    const int sub = std::max(1, (int)std::lround(240.0 / fps));   // about 240 steps per second
    const float dt = 1.0f / (float)(fps * sub);
    int frames = std::clamp(s.frameEnd - s.frameStart + 1, 1, kMaxBakeFrames);
    // Guard total work (steps x body pairs) so many convex hulls can't hang the app for minutes.
    const double pairs = std::max<double>(1.0, (double)bodies.size() * (double)bodies.size());
    frames = (int)std::max(1.0, std::min<double>(frames, 4e9 / (pairs * sub)));
    const V g = ToV(s.gravity);
    for (Body& b : bodies) if (b.dynamic) b.obj->rigidBody.cache.reserve(frames);
    auto record = [&] { for (const Body& b : bodies) if (b.dynamic) b.obj->rigidBody.cache.push_back(Pose(b)); };
    record();
    for (int f = 1; f < frames; ++f) {
        for (int k = 0; k < sub; ++k) Step(bodies, g, dt);
        record();
    }
    return moving;
}

void ApplyRigidBodyCache(Scene& scene, float frame) {
    for (auto& up : scene.objects) {
        RigidBody& rb = up->rigidBody;
        if (!rb.Baked()) continue;
        int i = std::clamp((int)std::lround(frame) - rb.cacheStart, 0, (int)rb.cache.size() - 1);
        up->transform.position = rb.cache[i].position;
        up->transform.rotation = rb.cache[i].rotation;
    }
    for (auto& up : scene.objects) {
        Cloth& cl = up->cloth;
        if (!cl.Baked()) continue;
        int i = std::clamp((int)std::lround(frame) - cl.cacheStart, 0, (int)cl.cache.size() - 1);
        if (i == cl.shown) continue;
        if (cl.cache[i].size() != up->mesh.positions.size()) { cl.cache.clear(); cl.rest.clear(); cl.shown = -1; continue; }   // mesh edited since
        SetClothPositions(*up, cl.cache[i]);
        cl.shown = i;
    }
}

void FreeRigidBodyBake(Scene& scene) {
    for (auto& up : scene.objects) {
        RigidBody& rb = up->rigidBody;
        if (!rb.Baked()) continue;
        up->transform.position = rb.rest.position;
        up->transform.rotation = rb.rest.rotation;
        rb.cache.clear();
    }
    for (auto& up : scene.objects) if (up->cloth.Baked()) DropClothBake(*up, false);
}

int BakeCloth(Scene& scene, const PhysicsSettings& s) {
    // Colliders: every object with a rigid body (active ones follow their bake).
    std::vector<std::pair<const MeshObject*, Shape>> colliders;
    for (auto& up : scene.objects) {
        const MeshObject& o = *up;
        if (o.rigidBody.enabled && !o.cloth.enabled && !o.isCurve) colliders.push_back({&o, BuildShape(o, o.rigidBody.shape)});
    }
    const int fps = std::clamp(s.fps, 1, 1000);
    const int sub = std::max(1, (int)std::lround(480.0 / fps));   // about 480 steps per second
    const float h = 1.0f / (float)(fps * sub);
    const V g = ToV(s.gravity);
    int baked = 0;
    for (auto& up : scene.objects) {
        MeshObject& o = *up;
        Cloth& cl = o.cloth;
        if (cl.Baked()) DropClothBake(o, false);
        if (!cl.enabled || o.isCurve || o.mesh.tris.empty()) continue;
        const size_t n = o.mesh.positions.size();
        cl.rest = o.mesh.positions;
        cl.cacheStart = s.frameStart;
        cl.shown = -1;

        // Stretch links on every triangle edge; bending links between the far corners of two
        // triangles sharing an edge (quad diagonals become shear links this way too).
        std::vector<Link> links, bends;
        std::unordered_map<uint64_t, uint32_t> edgeFar;   // edge -> far corner of the first triangle
        std::unordered_set<uint64_t> seen;
        auto restLen = [&](uint32_t a, uint32_t b) { return Len(ToV(o.transform.Apply(cl.rest[a])) - ToV(o.transform.Apply(cl.rest[b]))); };
        for (const Tri& t : o.mesh.tris) {
            if (t.a >= n || t.b >= n || t.c >= n) continue;
            const uint32_t c[3] = {t.a, t.b, t.c};
            for (int e = 0; e < 3; ++e) {
                uint32_t a = c[e], b = c[(e + 1) % 3], far = c[(e + 2) % 3];
                uint64_t key = Mesh::EdgeKey(a, b);
                auto it = edgeFar.find(key);
                if (it == edgeFar.end()) { edgeFar[key] = far; links.push_back({a, b, restLen(a, b)}); }
                else if (it->second != far && seen.insert(Mesh::EdgeKey(it->second, far)).second)
                    bends.push_back({it->second, far, restLen(it->second, far)});
            }
        }

        std::vector<V> x(n), prev(n), v(n);
        for (size_t i = 0; i < n; ++i) x[i] = ToV(o.transform.Apply(cl.rest[i]));
        const float mass = std::clamp(std::isfinite(cl.mass) ? cl.mass : 0.3f, 0.001f, 1e6f);
        std::vector<float> w(n, (float)n / mass);   // inverse mass per vertex
        for (uint32_t p : cl.pins) if (p < n) w[p] = 0;
        // Compliance from stiffness (1 rigid, 0 very soft), divided by the step squared as XPBD does.
        auto compliance = [](float k) { k = std::clamp(k, 0.0f, 1.0f); return 1e-3f * (1 - k) / (k + 0.01f); };
        const float aS = compliance(cl.stretch) / (h * h), aB = compliance(cl.bend) / (h * h);
        const float keep = std::pow(1 - std::clamp(cl.damping, 0.0f, 0.99f), h);
        const float thick = std::clamp(cl.thickness, 0.0f, 1.0f), fric = std::clamp(cl.friction, 0.0f, 1.0f);
        auto solve = [&](const std::vector<Link>& ls, float alpha) {
            for (const Link& l : ls) {
                float ws = w[l.i] + w[l.j];
                if (ws <= 0) continue;
                V d = x[l.i] - x[l.j];
                float len = Len(d);
                if (len < 1e-9f) continue;
                V corr = d * (-(len - l.rest) / (ws + alpha) / len);
                x[l.i] = x[l.i] + corr * w[l.i];
                x[l.j] = x[l.j] - corr * w[l.j];
            }
        };

        int frames = std::clamp(s.frameEnd - s.frameStart + 1, 1, kMaxBakeFrames);
        // Guard memory (the cache) and work (steps x constraints x colliders) so a bake can't hang the app.
        frames = (int)std::min<double>(frames, std::max(1.0, 3e6 / (double)n));
        const double perStep = (double)links.size() + bends.size() + (double)n * (1 + colliders.size());
        frames = (int)std::max(1.0, std::min<double>(frames, 4e10 / (perStep * sub)));
        cl.cache.reserve(frames);
        auto record = [&] {
            std::vector<Float3> f(n);
            for (size_t i = 0; i < n; ++i) f[i] = o.transform.InverseApply(ToF(x[i]));
            cl.cache.push_back(std::move(f));
        };
        record();
        std::vector<Collider> posed;
        for (int f = 1; f < frames; ++f) {
            posed.clear();
            for (auto& c : colliders) posed.push_back(PoseCollider(*c.first, c.second, s.frameStart + f));
            for (int k = 0; k < sub; ++k) {
                for (size_t i = 0; i < n; ++i) {
                    prev[i] = x[i];
                    if (w[i] == 0) continue;
                    v[i] = (v[i] + g * h) * keep;
                    x[i] = x[i] + v[i] * h;
                }
                solve(links, aS);
                solve(bends, aB);
                for (size_t i = 0; i < n; ++i) {
                    if (w[i] == 0) continue;
                    for (const Collider& c : posed) {
                        V nrm = PushOut(c, x[i], thick);
                        if (Dot(nrm, nrm) == 0) continue;
                        V d = x[i] - prev[i];   // friction: take away part of the sliding motion
                        x[i] = x[i] - (d - nrm * Dot(d, nrm)) * fric;
                    }
                }
                for (size_t i = 0; i < n; ++i) v[i] = w[i] == 0 ? V{} : (x[i] - prev[i]) * (1 / h);
            }
            record();
        }
        ++baked;
    }
    return baked;
}

void DropClothBake(MeshObject& o, bool keepShape) {
    Cloth& cl = o.cloth;
    if (!keepShape && cl.Baked()) SetClothPositions(o, cl.rest);
    cl.cache.clear();
    cl.rest.clear();
    cl.shown = -1;
}

int PinClothTop(MeshObject& o) {
    Cloth& cl = o.cloth;
    cl.pins.clear();
    if (o.mesh.positions.empty()) return 0;
    float lo = 1e30f, hi = -1e30f;
    for (const Float3& p : o.mesh.positions) { float y = o.transform.Apply(p).y; lo = std::min(lo, y); hi = std::max(hi, y); }
    const float cut = hi - std::max(0.02f * (hi - lo), 1e-4f);
    for (uint32_t i = 0; i < o.mesh.positions.size(); ++i)
        if (o.transform.Apply(o.mesh.positions[i]).y >= cut) cl.pins.push_back(i);
    return (int)cl.pins.size();
}
