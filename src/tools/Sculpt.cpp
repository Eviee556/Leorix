#include "tools/Sculpt.h"
#include "tools/BrushImage.h"
#include <algorithm>
#include <cmath>

namespace {
Float3 Add(Float3 a, Float3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Float3 Sub(Float3 a, Float3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Float3 Mul(Float3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
Float3 Hadamard(Float3 a, Float3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
float  Dot(Float3 a, Float3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Float3 Cross(Float3 a, Float3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
Float3 Normalize(Float3 a) { float l = std::sqrt(Dot(a, a)); return l > 1e-8f ? Mul(a, 1.0f / l) : a; }

// t: 0 at the brush center, 1 at the rim. Curve order matches the panel's Falloff combo.
float Falloff(float t, const SculptSettings& s, bool sharp) {
    float h = std::clamp(s.hardness, 0.0f, 0.99f);
    t = t <= h ? 0.0f : (t - h) / (1.0f - h);
    float x = 1.0f - std::clamp(t, 0.0f, 1.0f);
    if (sharp) return x * x * x;
    switch (s.falloffCurve) {
        case 1: return x * x * x * (x * (x * 6.0f - 15.0f) + 10.0f);  // Smoother
        case 2: return std::sqrt(std::max(0.0f, 1.0f - t * t));      // Sphere
        case 3: return std::sqrt(x);                                  // Root
        case 4: return x * x;                                         // Sharp
        case 5: return x;                                             // Linear
        case 6: return x * x * x;                                     // Sharper
        case 7: return 1.0f - t * t;                                  // Inverse Square
        case 8: return 1.0f;                                          // Constant
        default: return x * x * (3.0f - 2.0f * x);                    // Smooth
    }
}

Float3 Pos(const Mesh& m, const std::vector<Float3>* disp, size_t i) {
    return disp ? Add(m.positions[i], (*disp)[i]) : m.positions[i];
}

// Height of the stamp under local offset `d`, projected onto the plane perpendicular to `n`.
float StampHeight(const BrushImage& img, const SculptSettings& s, Float3 n, Float3 d, float radius) {
    Float3 ref = std::fabs(n.y) < 0.9f ? Float3{0, 1, 0} : Float3{1, 0, 0};
    Float3 u = Normalize(Cross(ref, n)), v = Cross(n, u);
    float scale = radius * std::max(0.01f, s.imageScale);
    float a = s.imageAngle * 3.14159265f / 180.0f, ca = std::cos(a), sa = std::sin(a);
    float su = Dot(d, u) / scale, sv = Dot(d, v) / scale;
    float ru = su * ca - sv * sa, rv = su * sa + sv * ca;
    float fu = ru * 0.5f + 0.5f, fv = 0.5f - rv * 0.5f;
    if (fu < 0 || fu >= 1 || fv < 0 || fv >= 1) return 0.0f;
    float h = img.heights[size_t(int(fv * img.height)) * img.width + int(fu * img.width)];
    return s.imageInvert ? 1.0f - h : h;
}

size_t DabPass(Mesh& m, std::vector<Float3>* disp, const SculptSettings& s, Icon brush,
               const SculptDab& dab, const BrushImage* stamp) {
    const size_t n = m.positions.size();
    const float r2 = dab.radius * dab.radius;
    const float mag = std::fabs(dab.strength), sign = dab.strength < 0 ? -1.0f : 1.0f;

    std::vector<uint32_t> idx;
    std::vector<float> fall;
    Float3 avgN{}, avgP{};
    for (size_t i = 0; i < n; ++i) {
        Float3 d = Sub(Pos(m, disp, i), dab.center);
        float d2 = Dot(d, d);
        if (d2 > r2) continue;
        idx.push_back(uint32_t(i));
        fall.push_back(Falloff(std::sqrt(d2) / dab.radius, s, brush == Icon::SculptDrawSharp));
        avgN = Add(avgN, m.normals[i]);
        avgP = Add(avgP, Pos(m, disp, i));
    }
    if (idx.empty()) return 0;
    avgN = Normalize(avgN);
    avgP = Mul(avgP, 1.0f / idx.size());

    // Laplacian neighbour average, only when a smoothing term is needed.
    const bool smooth = dab.smooth || brush == Icon::SculptSmooth;
    std::vector<Float3> nbr;
    if (smooth || s.autoSmooth > 0) {
        // shortcut: O(verts + tris) scratch per dab, fine to a few million tris; cache adjacency if it shows in profiles.
        std::vector<int> slot(n, -1);
        for (size_t k = 0; k < idx.size(); ++k) slot[idx[k]] = int(k);
        nbr.assign(idx.size(), Float3{});
        std::vector<int> cnt(idx.size(), 0);
        auto edge = [&](uint32_t a, uint32_t b) {
            if (slot[a] >= 0) { nbr[slot[a]] = Add(nbr[slot[a]], Pos(m, disp, b)); ++cnt[slot[a]]; }
        };
        for (const Tri& t : m.tris) { edge(t.a, t.b); edge(t.a, t.c); edge(t.b, t.a); edge(t.b, t.c); edge(t.c, t.a); edge(t.c, t.b); }
        for (size_t k = 0; k < idx.size(); ++k)
            nbr[k] = cnt[k] ? Mul(nbr[k], 1.0f / cnt[k]) : Pos(m, disp, idx[k]);
    }

    const Float3 plane = Add(avgP, Mul(avgN, s.planeOffset * dab.radius));
    const Float3 tangentDrag = Sub(dab.drag, Mul(avgN, Dot(dab.drag, avgN)));
    std::vector<Float3> delta(idx.size());
    for (size_t k = 0; k < idx.size(); ++k) {
        const uint32_t i = idx[k];
        const float f = fall[k];
        const Float3 p = Pos(m, disp, i);
        const float amount = f * mag * dab.radius * 0.25f * sign;
        Float3 d{};
        if (smooth) {
            d = Mul(Sub(nbr[k], p), std::min(1.0f, f * mag));
        } else switch (brush) {
            case Icon::SculptInflate: case Icon::SculptBlob:
                d = Mul(m.normals[i], amount); break;
            case Icon::SculptFlatten: case Icon::SculptFill: case Icon::SculptScrape:
            case Icon::SculptMultiScrape: case Icon::SculptPolish: case Icon::SculptPlanarTrim: {
                float to = Dot(Sub(plane, p), avgN);                      // > 0: point is below the plane
                bool below = to > 0;
                if (brush == Icon::SculptFill && !below) break;
                if ((brush == Icon::SculptScrape || brush == Icon::SculptMultiScrape || brush == Icon::SculptPlanarTrim) && below) break;
                d = Mul(avgN, to * std::min(1.0f, f * mag));
                break;
            }
            case Icon::SculptPinch:
                d = Mul(Sub(dab.center, p), f * mag * 0.5f * sign); break;
            case Icon::SculptGrab: case Icon::SculptElastic: case Icon::SculptSnakeHook:
                d = Mul(dab.drag, f); break;
            case Icon::SculptThumb: case Icon::SculptNudge:
                d = Mul(tangentDrag, f * mag); break;
            case Icon::SculptCrease:
                d = Add(Mul(avgN, amount), Mul(Sub(dab.center, p), f * mag * 0.25f)); break;
            case Icon::SculptCustomImage:
                d = Mul(avgN, amount * (stamp ? StampHeight(*stamp, s, avgN, Sub(p, dab.center), dab.radius) : 1.0f)); break;
            default:   // Draw, Draw Sharp, Clay family, Layer: along the averaged normal
                d = Mul(avgN, amount); break;
        }
        if (s.autoSmooth > 0 && !smooth) d = Add(d, Mul(Sub(nbr[k], p), f * s.autoSmooth * 0.5f));
        if (s.lockX) d.x = 0;
        if (s.lockY) d.y = 0;
        if (s.lockZ) d.z = 0;
        delta[k] = d;
    }
    size_t moved = 0;
    for (size_t k = 0; k < idx.size(); ++k) {
        Float3& dst = disp ? (*disp)[idx[k]] : m.positions[idx[k]];
        dst = Add(dst, delta[k]);
        if (Dot(delta[k], delta[k]) > 0) ++moved;
    }
    return moved;
}
}  // namespace

bool SculptBrushSupported(Icon b) {
    switch (b) {
        case Icon::SculptDraw: case Icon::SculptDrawSharp: case Icon::SculptClay: case Icon::SculptClayStrips:
        case Icon::SculptClayThumb: case Icon::SculptLayer: case Icon::SculptInflate: case Icon::SculptBlob:
        case Icon::SculptCrease: case Icon::SculptSmooth: case Icon::SculptFlatten: case Icon::SculptFill:
        case Icon::SculptScrape: case Icon::SculptMultiScrape: case Icon::SculptPinch: case Icon::SculptGrab:
        case Icon::SculptElastic: case Icon::SculptSnakeHook: case Icon::SculptThumb: case Icon::SculptNudge:
        case Icon::SculptPolish: case Icon::SculptPlanarTrim: case Icon::SculptCustomImage:
            return true;
        default:
            return false;
    }
}

size_t ApplySculptDab(Mesh& target, std::vector<Float3>* disp, const SculptSettings& s, Icon brush,
                      const SculptDab& dab, const BrushImage* stamp) {
    if (target.positions.empty() || dab.radius <= 1e-6f) return 0;
    if (!dab.smooth && !SculptBrushSupported(brush)) return 0;
    if (target.normals.size() != target.positions.size()) target.ComputeNormals();
    if (disp && disp->size() != target.positions.size()) disp->assign(target.positions.size(), Float3{});
    if (stamp && stamp->heights.empty()) stamp = nullptr;

    // Symmetry mirrors the dab across the object's local planes; a dab sitting on the
    // mirror plane is not applied twice.
    size_t moved = 0;
    std::vector<Float3> done;
    for (int mx = 0; mx <= (s.symX ? 1 : 0); ++mx)
    for (int my = 0; my <= (s.symY ? 1 : 0); ++my)
    for (int mz = 0; mz <= (s.symZ ? 1 : 0); ++mz) {
        Float3 m{mx ? -1.0f : 1.0f, my ? -1.0f : 1.0f, mz ? -1.0f : 1.0f};
        SculptDab d = dab;
        d.center = Hadamard(dab.center, m);
        d.drag = Hadamard(dab.drag, m);
        if (std::any_of(done.begin(), done.end(), [&](Float3 c) { return Dot(Sub(c, d.center), Sub(c, d.center)) < 1e-12f; })) continue;
        done.push_back(d.center);
        moved += DabPass(target, disp, s, brush, d, stamp);
    }
    return moved;
}
