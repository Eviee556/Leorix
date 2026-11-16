#include "mesh/Curve.h"
#include <algorithm>
#include <cmath>

namespace {
const float kPi = 3.14159265358979323846f;
Float3 Add(Float3 a, Float3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Float3 Sub(Float3 a, Float3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Float3 Mul(Float3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float  Dot(Float3 a, Float3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float  Len(Float3 a) { return std::sqrt(Dot(a, a)); }
Float3 Cross(Float3 a, Float3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
Float3 Norm(Float3 a) { float l = Len(a); return l > 1e-12f ? Mul(a, 1.0f / l) : Float3{0, 0, 0}; }
Float3 Lerp(Float3 a, Float3 b, float t) { return Add(a, Mul(Sub(b, a), t)); }

// Tangents and rotation-minimising normals for a sampled path (cyclic: no repeated end sample).
void Frames(const std::vector<Float3>& path, bool cyclic, std::vector<Float3>& T, std::vector<Float3>& N) {
    const int m = (int)path.size();
    T.assign(m, {0, 1, 0});
    N.assign(m, {1, 0, 0});
    if (m < 2) return;
    for (int i = 0; i < m; ++i) {
        int a = cyclic ? (i + m - 1) % m : std::max(0, i - 1);
        int b = cyclic ? (i + 1) % m : std::min(m - 1, i + 1);
        Float3 t = Norm(Sub(path[b], path[a]));
        T[i] = Len(t) > 0 ? t : (i > 0 ? T[i - 1] : Float3{0, 1, 0});
    }
    auto perp = [](Float3 t) {
        Float3 ref = std::fabs(t.y) > 0.9f ? Float3{1, 0, 0} : Float3{0, 1, 0};
        return Norm(Cross(t, ref));
    };
    N[0] = perp(T[0]);
    for (int i = 1; i < m; ++i) {
        // parallel transport: drop the part of the last normal that now runs along the tangent
        Float3 n = Norm(Sub(N[i - 1], Mul(T[i], Dot(N[i - 1], T[i]))));
        N[i] = Len(n) > 0 ? n : perp(T[i]);
    }
    if (cyclic) {
        // carry the last frame round to the first; spread the leftover twist along the loop
        Float3 back = Norm(Sub(N[m - 1], Mul(T[0], Dot(N[m - 1], T[0]))));
        if (Len(back) > 0) {
            float ang = std::atan2(Dot(Cross(back, N[0]), T[0]), Dot(back, N[0]));
            for (int i = 1; i < m; ++i) {
                float a = ang * i / m, c = std::cos(a), s = std::sin(a);
                N[i] = Norm(Add(Mul(N[i], c), Mul(Cross(T[i], N[i]), s)));
            }
        }
    }
}

// NURBS by de Boor in homogeneous space (x*w, y*w, z*w, radius*w, w). A cyclic curve repeats its
// first order-1 points on uniform knots, which joins the end smoothly to the start.
std::vector<Float3> EvaluateNurbs(const CurvePath& c, std::vector<float>* radii) {
    const int n = (int)c.points.size();
    const int k = std::clamp(c.order, 2, std::min(6, n));   // order can't exceed the point count
    const int N = c.cyclic ? n + k - 1 : n;
    struct H { double v[5]; };
    std::vector<H> P(N);
    for (int j = 0; j < N; ++j) {
        int i = j % n;
        double w = c.WeightAt(i);
        const Float3& p = c.points[i];
        P[j] = {{p.x * w, p.y * w, p.z * w, c.RadiusAt(i) * w, w}};
    }
    std::vector<double> U(N + k);
    bool custom = !c.cyclic && c.knots.size() == U.size();
    for (size_t i = 0; custom && i < c.knots.size(); ++i)
        custom = std::isfinite(c.knots[i]) && (i == 0 || c.knots[i] >= c.knots[i - 1]);
    if (custom && !(c.knots[N] > c.knots[k - 1])) custom = false;
    for (int i = 0; i < N + k; ++i)
        U[i] = custom ? c.knots[i] : (c.endpoint && !c.cyclic) ? std::clamp(i - k + 1, 0, N - k + 1) : i;
    const double t0 = U[k - 1], t1 = U[N];
    const int samples = (c.cyclic ? n : n - 1) * std::clamp(c.resolution, 1, 64);
    std::vector<Float3> out;
    out.reserve(samples + 1);
    if (radii) radii->clear();
    for (int s = 0; s <= samples; ++s) {
        if (c.cyclic && s == samples) break;   // the closing sample is the first one again
        double t = t0 + (t1 - t0) * s / samples;
        int span = k - 1;
        while (span < N - 1 && U[span + 1] <= t) ++span;
        H d[6];
        for (int j = 0; j < k; ++j) d[j] = P[j + span - k + 1];
        for (int r = 1; r < k; ++r)
            for (int j = k - 1; j >= r; --j) {
                int i = j + span - k + 1;
                double den = U[i + k - r] - U[i];
                double a = den > 0 ? (t - U[i]) / den : 0;
                for (int q = 0; q < 5; ++q) d[j].v[q] = (1 - a) * d[j - 1].v[q] + a * d[j].v[q];
            }
        const H& h = d[k - 1];
        double w = std::max(h.v[4], 1e-12);
        out.push_back({float(h.v[0] / w), float(h.v[1] / w), float(h.v[2] / w)});
        if (radii) radii->push_back(float(h.v[3] / w));
    }
    if (c.cyclic) {
        out.push_back(out.front());
        if (radii) radii->push_back(radii->front());
    }
    return out;
}
} // namespace

const char* HandleTypeName(HandleType t) {
    switch (t) {
        case HandleType::Auto: return "Auto";
        case HandleType::Vector: return "Vector";
        case HandleType::Aligned: return "Aligned";
        case HandleType::Free: return "Free";
    }
    return "?";
}

void CurvePath::HandlesFor(int i, Float3& l, Float3& r) const {
    const int n = (int)points.size();
    const Float3 p = points[i];
    HandleType type = i < (int)handleType.size() ? handleType[i] : HandleType::Auto;
    bool stored = (int)handleL.size() == n && (int)handleR.size() == n;
    if (stored && (type == HandleType::Aligned || type == HandleType::Free)) { l = handleL[i]; r = handleR[i]; return; }
    bool hasPrev = n > 1 && (cyclic || i > 0), hasNext = n > 1 && (cyclic || i < n - 1);
    Float3 P = hasPrev ? points[(i + n - 1) % n] : p, Nx = hasNext ? points[(i + 1) % n] : p;
    if (type == HandleType::Vector) {
        // straight at the point: each handle a third of the way to its neighbour
        l = hasPrev ? Lerp(p, P, 1.0f / 3) : p;
        r = hasNext ? Lerp(p, Nx, 1.0f / 3) : p;
        if (!hasPrev) l = Sub(p, Sub(r, p));
        if (!hasNext) r = Sub(p, Sub(l, p));
        return;
    }
    // Auto: smooth through the point, each side a third of its own span
    if (hasPrev && hasNext) {
        Float3 dir = Norm(Sub(Nx, P));
        if (Len(dir) == 0) dir = Norm(Sub(Nx, p));
        l = Sub(p, Mul(dir, Len(Sub(p, P)) / 3));
        r = Add(p, Mul(dir, Len(Sub(Nx, p)) / 3));
    } else if (hasNext) {
        r = Lerp(p, Nx, 1.0f / 3); l = Sub(p, Sub(r, p));
    } else if (hasPrev) {
        l = Lerp(p, P, 1.0f / 3); r = Sub(p, Sub(l, p));
    } else {
        l = r = p;
    }
}

void CurvePath::Sync() {
    const size_t n = points.size();
    handleType.resize(n, HandleType::Auto);
    radius.resize(n, 1.0f);
    weight.resize(n, 1.0f);
    std::vector<Float3> L(n), R(n);
    for (size_t i = 0; i < n; ++i) HandlesFor((int)i, L[i], R[i]);
    handleL = std::move(L);
    handleR = std::move(R);
}

Float3 CurvePath::Elem(int e) const {
    int i = e / 3, k = e % 3;
    if (e < 0 || i >= (int)points.size()) return {0, 0, 0};
    if (k == 0 || nurbs) return points[i];   // NURBS points have no handles
    Float3 l, r;
    HandlesFor(i, l, r);
    return k == 1 ? l : r;
}

void CurvePath::SetElem(int e, Float3 p) {
    int i = e / 3, k = e % 3;
    if (e < 0 || i >= (int)points.size()) return;
    if (k == 0) { points[i] = p; return; }
    if (nurbs) return;
    if (handleL.size() != points.size() || handleR.size() != points.size() || handleType.size() != points.size()) Sync();
    HandleType& t = handleType[i];
    if (t == HandleType::Auto) t = HandleType::Aligned;     // what Blender does when a handle is grabbed
    else if (t == HandleType::Vector) t = HandleType::Free;
    Float3& moved = k == 1 ? handleL[i] : handleR[i];
    Float3& other = k == 1 ? handleR[i] : handleL[i];
    moved = p;
    if (t == HandleType::Aligned) {
        Float3 dir = Norm(Sub(p, points[i]));
        if (Len(dir) > 0) other = Sub(points[i], Mul(dir, Len(Sub(other, points[i]))));
    }
}

std::vector<Float3> CurvePath::Evaluate(std::vector<float>* radii) const {
    const int n = (int)points.size();
    if (radii) radii->clear();
    if (n < 2) {
        if (radii) for (int i = 0; i < n; ++i) radii->push_back(RadiusAt(i));
        return points;
    }
    if (nurbs) return EvaluateNurbs(*this, radii);
    std::vector<Float3> L(n), R(n);
    for (int i = 0; i < n; ++i) HandlesFor(i, L[i], R[i]);
    int res = std::clamp(resolution, 1, 64);
    int spans = cyclic ? n : n - 1;
    std::vector<Float3> out;
    out.reserve((size_t)spans * res + 1);
    for (int i = 0; i < spans; ++i) {
        int j = (i + 1) % n;
        const Float3 p0 = points[i], p1 = R[i], p2 = L[j], p3 = points[j];
        for (int s = 0; s < res; ++s) {
            float t = (float)s / res, u = 1 - t;
            float b0 = u * u * u, b1 = 3 * u * u * t, b2 = 3 * u * t * t, b3 = t * t * t;
            out.push_back({b0 * p0.x + b1 * p1.x + b2 * p2.x + b3 * p3.x,
                           b0 * p0.y + b1 * p1.y + b2 * p2.y + b3 * p3.y,
                           b0 * p0.z + b1 * p1.z + b2 * p2.z + b3 * p3.z});
            if (radii) radii->push_back(RadiusAt(i) + (RadiusAt(j) - RadiusAt(i)) * t);
        }
    }
    out.push_back(cyclic ? out.front() : points.back());
    if (radii) radii->push_back(cyclic ? radii->front() : RadiusAt(n - 1));
    return out;
}

std::vector<Float3> CurveFrameNormals(const std::vector<Float3>& path, bool cyclic) {
    std::vector<Float3> T, N;
    Frames(path, cyclic, T, N);
    return N;
}

std::vector<int> CurveElemsInside(const CurvePath& c, const std::function<bool(const Float3&)>& inside) {
    std::vector<int> hits;
    for (int e = 0; e < c.ElemCount(); ++e) {
        if (c.nurbs && e % 3 != 0) continue;
        if (inside(c.Elem(e))) hits.push_back(e);
    }
    return hits;
}

Mesh CurveToMesh(const CurvePath& c, const CurvePath* profile) {
    Mesh m;
    if (c.points.size() < 2) return m;
    // Cross-section offsets in the frame's (normal, binormal) plane.
    std::vector<Float2> cs;
    bool csClosed = true;
    if (profile && profile != &c && profile->points.size() >= 2) {
        std::vector<Float3> pp = profile->Evaluate();
        csClosed = profile->cyclic || (pp.size() > 2 && Len(Sub(pp.front(), pp.back())) < 1e-5f);
        if (csClosed) pp.pop_back();
        for (const Float3& p : pp) cs.push_back({p.x, p.z});   // presets lie in the X/Z plane
    } else {
        if (c.bevelDepth <= 0) return m;
        const int n = 4 + 2 * std::clamp(c.bevelResolution, 0, 32);
        for (int k = 0; k < n; ++k) {
            float a = 2 * kPi * k / n;
            cs.push_back({c.bevelDepth * std::cos(a), c.bevelDepth * std::sin(a)});
        }
    }
    const int sides = (int)cs.size();
    if (sides < 2) return m;

    std::vector<float> rad;
    std::vector<Float3> path = c.Evaluate(&rad);
    const bool closed = c.cyclic || (path.size() > 2 && Len(Sub(path.front(), path.back())) < 1e-5f);
    if (closed) { path.pop_back(); rad.pop_back(); }   // the closing sample repeats the first
    const int rings = (int)path.size();
    if (rings < 2) return m;
    std::vector<Float3> T, N;
    Frames(path, closed, T, N);

    m.positions.reserve((size_t)rings * sides + 2);
    for (int i = 0; i < rings; ++i) {
        Float3 B = Cross(T[i], N[i]);
        for (const Float2& o : cs)
            m.positions.push_back(Add(path[i], Mul(Add(Mul(N[i], o.u), Mul(B, o.v)), rad[i])));
    }
    auto V = [&](int ring, int k) { return uint32_t((ring % rings) * sides + (k % sides)); };
    const int segs = closed ? rings : rings - 1;
    const int across = csClosed ? sides : sides - 1;   // an open profile doesn't wrap round
    for (int i = 0; i < segs; ++i)
        for (int k = 0; k < across; ++k) {
            uint32_t a = V(i, k), b = V(i, k + 1), cc = V(i + 1, k + 1), d = V(i + 1, k);
            float u0 = (float)k / sides, u1 = (float)(k + 1) / sides, v0 = (float)i / segs, v1 = (float)(i + 1) / segs;
            m.tris.push_back({a, b, cc, m.polyCount});
            m.tris.push_back({a, cc, d, m.polyCount});
            m.loopUVs.insert(m.loopUVs.end(), {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v0}, {u1, v1}, {u0, v1}});
            ++m.polyCount;
        }
    if (!closed && csClosed && sides >= 3) {
        // n-gon caps; the start cap faces backwards along the curve, the end cap forwards
        for (int end = 0; end < 2; ++end) {
            int ring = end ? rings - 1 : 0;
            auto uv = [&](int k) { float a = 2 * kPi * k / sides; return Float2{0.5f + 0.5f * std::cos(a), 0.5f + 0.5f * std::sin(a)}; };
            for (int k = 1; k + 1 < sides; ++k) {
                int k1 = end ? k : k + 1, k2 = end ? k + 1 : k;
                m.tris.push_back({V(ring, 0), V(ring, k1), V(ring, k2), m.polyCount});
                m.loopUVs.insert(m.loopUVs.end(), {uv(0), uv(k1), uv(k2)});
            }
            ++m.polyCount;
        }
    }
    m.ComputeBounds();
    m.ComputeNormals();
    return m;
}

void SetNurbsCircle(CurvePath& c, float r) {
    const float h = std::sqrt(0.5f);
    c.nurbs = true;
    c.cyclic = false;   // closed by the repeated end point, which the sweep detects
    c.order = 3;
    c.endpoint = true;
    c.points = {{r, 0, 0}, {r, 0, r}, {0, 0, r}, {-r, 0, r}, {-r, 0, 0}, {-r, 0, -r}, {0, 0, -r}, {r, 0, -r}, {r, 0, 0}};
    c.weight = {1, h, 1, h, 1, h, 1, h, 1};
    c.knots = {0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 4};
    c.handleL.clear(); c.handleR.clear(); c.handleType.clear(); c.radius.clear();
    c.Sync();
}

int ExtrudeCurveEnd(CurvePath& c, int point) {
    const int n = (int)c.points.size();
    if (c.cyclic || n < 1 || (point != 0 && point != n - 1)) return -1;
    c.Sync();
    bool front = point == 0 && n > 1;
    Float3 p = c.points[point];
    Float3 away = n > 1 ? Sub(p, c.points[front ? 1 : n - 2]) : Float3{1, 0, 0};
    Float3 np = Add(p, away);
    int at = front ? 0 : n;
    c.points.insert(c.points.begin() + at, np);
    c.handleL.insert(c.handleL.begin() + at, np);
    c.handleR.insert(c.handleR.begin() + at, np);
    c.handleType.insert(c.handleType.begin() + at, HandleType::Auto);
    c.radius.insert(c.radius.begin() + at, c.radius[front ? 0 : n - 1]);
    c.weight.insert(c.weight.begin() + at, c.weight[front ? 0 : n - 1]);
    c.knots.clear();   // explicit knots no longer fit the point count
    c.Sync();
    return at;
}

int DeleteCurvePoints(CurvePath& c, const std::set<int>& pts) {
    const int n = (int)c.points.size();
    int gone = 0;
    for (int i : pts) if (i >= 0 && i < n) ++gone;
    if (gone == 0 || n - gone < 2) return 0;
    c.Sync();
    CurvePath keep = c;
    keep.points.clear(); keep.handleL.clear(); keep.handleR.clear(); keep.handleType.clear(); keep.radius.clear();
    keep.weight.clear(); keep.knots.clear();
    for (int i = 0; i < n; ++i) {
        if (pts.count(i)) continue;
        keep.points.push_back(c.points[i]);
        keep.handleL.push_back(c.handleL[i]);
        keep.handleR.push_back(c.handleR[i]);
        keep.handleType.push_back(c.handleType[i]);
        keep.radius.push_back(c.radius[i]);
        keep.weight.push_back(c.weight[i]);
    }
    c = std::move(keep);
    c.Sync();
    return gone;
}
