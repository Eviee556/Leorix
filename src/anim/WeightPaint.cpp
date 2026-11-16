#include "anim/WeightPaint.h"
#include "tools/Sculpt.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <unordered_map>

namespace {
Float3 Sub(Float3 a, Float3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
float  Dot(Float3 a, Float3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Float3 Cross(Float3 a, Float3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float  Len(Float3 a) { return std::sqrt(Dot(a, a)); }

bool Locked(const std::vector<Bone>* bones, int b) { return bones && b >= 0 && b < (int)bones->size() && (*bones)[b].weightLocked; }

// Scales the unlocked influences so the total is 1; locked ones keep their value.
bool NormalizeUnlocked(VertexWeights& vw, const std::vector<Bone>* bones) {
    float locked = 0, free = 0;
    for (int s = 0; s < 4; ++s) {
        if (vw.bone[s] < 0) continue;
        (Locked(bones, vw.bone[s]) ? locked : free) += vw.weight[s];
    }
    if (free <= 1e-6f || std::fabs(locked + free - 1.0f) < 1e-6f) return false;
    const float k = std::max(0.0f, 1.0f - locked) / free;
    for (int s = 0; s < 4; ++s) if (vw.bone[s] >= 0 && !Locked(bones, vw.bone[s])) vw.weight[s] *= k;
    return true;
}

bool IsLeftName(const std::string& n) {
    for (const char* l : {".L", ".l", "_L", "_l", "Left", "left"}) if (n.find(l) != std::string::npos) return true;
    return false;
}

// Barycentric coordinates of `p` projected onto triangle abc.
void Barycentric(Float3 p, Float3 a, Float3 b, Float3 c, float& u, float& v, float& w) {
    Float3 v0 = Sub(b, a), v1 = Sub(c, a), v2 = Sub(p, a);
    float d00 = Dot(v0, v0), d01 = Dot(v0, v1), d11 = Dot(v1, v1), d20 = Dot(v2, v0), d21 = Dot(v2, v1);
    float den = d00 * d11 - d01 * d01;
    if (std::fabs(den) < 1e-20f) { u = 1; v = w = 0; return; }
    v = (d11 * d20 - d01 * d21) / den;
    w = (d00 * d21 - d01 * d20) / den;
    u = 1 - v - w;
}

// Closest point on triangle abc to p (Ericson, Real-Time Collision Detection 5.1.5), as barycentrics.
void ClosestOnTri(Float3 p, Float3 a, Float3 b, Float3 c, float& u, float& v, float& w) {
    Float3 ab = Sub(b, a), ac = Sub(c, a), ap = Sub(p, a);
    float d1 = Dot(ab, ap), d2 = Dot(ac, ap);
    if (d1 <= 0 && d2 <= 0) { u = 1; v = w = 0; return; }
    Float3 bp = Sub(p, b);
    float d3 = Dot(ab, bp), d4 = Dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) { v = 1; u = w = 0; return; }
    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) { float t = d1 / (d1 - d3); u = 1 - t; v = t; w = 0; return; }
    Float3 cp = Sub(p, c);
    float d5 = Dot(ab, cp), d6 = Dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) { w = 1; u = v = 0; return; }
    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) { float t = d2 / (d2 - d6); u = 1 - t; w = t; v = 0; return; }
    float va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) { float t = (d4 - d3) / ((d4 - d3) + (d5 - d6)); v = 1 - t; w = t; u = 0; return; }
    float den = 1.0f / (va + vb + vc);
    v = vb * den; w = vc * den; u = 1 - v - w;
}

// Mean, min and max of `bone`'s weight over each vertex's edge neighbours (vertices with no
// neighbour report their own weight).
void NeighbourStats(const std::vector<VertexWeights>& wts, const std::vector<Tri>& tris, int bone,
                    std::vector<float>& mean, std::vector<float>& mn, std::vector<float>& mx) {
    const size_t n = wts.size();
    std::vector<float> sum(n, 0.0f), cnt(n, 0.0f);
    mn.assign(n, 2.0f); mx.assign(n, -1.0f);
    auto edge = [&](uint32_t a, uint32_t b) {
        if (a >= n || b >= n) return;
        const float wa = BoneWeight(wts[a], bone), wb = BoneWeight(wts[b], bone);
        sum[a] += wb; cnt[a] += 1; mn[a] = std::min(mn[a], wb); mx[a] = std::max(mx[a], wb);
        sum[b] += wa; cnt[b] += 1; mn[b] = std::min(mn[b], wa); mx[b] = std::max(mx[b], wa);
    };
    for (const Tri& t : tris) { edge(t.a, t.b); edge(t.b, t.c); edge(t.c, t.a); }
    mean.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const float own = BoneWeight(wts[i], bone);
        mean[i] = cnt[i] > 0 ? sum[i] / cnt[i] : own;
        if (cnt[i] == 0) mn[i] = mx[i] = own;
    }
}
}   // namespace

float BoneWeight(const VertexWeights& vw, int bone) {
    for (int s = 0; s < 4; ++s) if (vw.bone[s] == bone) return vw.weight[s];
    return 0.0f;
}

void SetBoneWeight(VertexWeights& vw, int bone, float w, const std::vector<Bone>* bones, bool normalize) {
    if (Locked(bones, bone)) return;
    w = std::clamp(w, 0.0f, 1.0f);
    int slot = -1;
    for (int s = 0; s < 4; ++s) if (vw.bone[s] == bone) slot = s;
    if (slot < 0) {
        if (w <= 0) return;
        for (int s = 0; s < 4; ++s) {   // an empty slot, else the weakest unlocked influence makes room
            if (vw.bone[s] < 0 || vw.weight[s] <= 0) { slot = s; break; }
            if (Locked(bones, vw.bone[s])) continue;
            if (slot < 0 || vw.weight[s] < vw.weight[slot]) slot = s;
        }
        if (slot < 0) return;   // all four slots hold locked bones
        vw.bone[slot] = bone;
        vw.weight[slot] = 0;
    }
    if (!normalize) { vw.weight[slot] = w; return; }
    float locked = 0, others = 0;
    for (int s = 0; s < 4; ++s) {
        if (s == slot || vw.bone[s] < 0) continue;
        (Locked(bones, vw.bone[s]) ? locked : others) += vw.weight[s];
    }
    const float room = std::max(0.0f, 1.0f - locked);
    if (others <= 1e-6f) {
        // The other unlocked bones are at zero: they share what this bone gives up. With none at
        // all, the only influence stays at full weight.
        int freeSlots = 0;
        for (int s = 0; s < 4; ++s) freeSlots += s != slot && vw.bone[s] >= 0 && !Locked(bones, vw.bone[s]);
        if (freeSlots == 0) { vw.weight[slot] = locked <= 1e-6f ? 1.0f : room; return; }
        w = std::min(w, room);
        vw.weight[slot] = w;
        for (int s = 0; s < 4; ++s)
            if (s != slot && vw.bone[s] >= 0 && !Locked(bones, vw.bone[s])) vw.weight[s] = (room - w) / freeSlots;
        return;
    }
    w = std::min(w, room);
    vw.weight[slot] = w;
    const float k = (room - w) / others;
    for (int s = 0; s < 4; ++s) if (s != slot && vw.bone[s] >= 0 && !Locked(bones, vw.bone[s])) vw.weight[s] *= k;
}

int MirrorBone(const Armature& arm, int bone) {
    if (bone < 0 || bone >= (int)arm.bones.size()) return bone;
    static const char* pairs[][2] = {{".L", ".R"}, {".l", ".r"}, {"_L", "_R"}, {"_l", "_r"}, {"Left", "Right"}, {"left", "right"}};
    const std::string& name = arm.bones[bone].name;
    for (auto& p : pairs)
        for (int dir = 0; dir < 2; ++dir) {
            const std::string from = p[dir], to = p[1 - dir];
            size_t at = name.rfind(from);
            if (at == std::string::npos) continue;
            std::string twin = name.substr(0, at) + to + name.substr(at + from.size());
            for (int i = 0; i < (int)arm.bones.size(); ++i) if (arm.bones[i].name == twin) return i;
        }
    return bone;
}

std::vector<int> MirrorTable(const std::vector<Float3>& pos) {
    const float q = 1e-4f;   // vertices hashed on a 0.1 mm grid, probed at (-x, y, z)
    auto key = [q](Float3 p) {
        long long x = std::llround(p.x / q), y = std::llround(p.y / q), z = std::llround(p.z / q);
        return (x * 73856093LL) ^ (y * 19349663LL) ^ (z * 83492791LL);
    };
    std::unordered_map<long long, uint32_t> grid;
    grid.reserve(pos.size());
    for (size_t i = 0; i < pos.size(); ++i) grid.emplace(key(pos[i]), uint32_t(i));
    std::vector<int> out(pos.size(), -1);
    for (size_t i = 0; i < pos.size(); ++i) {
        auto it = grid.find(key({-pos[i].x, pos[i].y, pos[i].z}));
        if (it == grid.end()) continue;
        const Float3& m = pos[it->second];
        if (std::fabs(m.x + pos[i].x) + std::fabs(m.y - pos[i].y) + std::fabs(m.z - pos[i].z) > 4 * q) continue;   // hash collision
        out[i] = int(it->second);
    }
    return out;
}

size_t ApplyWeightDab(Armature& arm, const std::vector<Float3>& pos, const std::vector<Tri>& tris,
                      const SculptSettings& falloff, const WeightDab& dab, const std::vector<Float3>* rest) {
    if (!arm.Bound() || pos.size() != arm.weights.size() || dab.bone < 0 || dab.bone >= (int)arm.bones.size() ||
        !(dab.radius > 0) || arm.bones[dab.bone].weightLocked)
        return 0;
    const size_t n = pos.size();
    const std::vector<Bone>* bones = &arm.bones;

    std::vector<Float3> nrm;   // front-faces-only needs vertex normals of the surface being painted
    if (dab.frontOnly) {
        nrm.assign(n, Float3{0, 0, 0});
        for (const Tri& t : tris) {
            if (t.a >= n || t.b >= n || t.c >= n) continue;
            Float3 fn = Cross(Sub(pos[t.b], pos[t.a]), Sub(pos[t.c], pos[t.a]));
            for (uint32_t v : {t.a, t.b, t.c}) { nrm[v].x += fn.x; nrm[v].y += fn.y; nrm[v].z += fn.z; }
        }
    }
    std::vector<uint32_t> idx;
    std::vector<float> fall;
    for (size_t i = 0; i < n; ++i) {
        float dist = Len(Sub(pos[i], dab.center));
        if (dist > dab.radius) continue;
        if (dab.frontOnly && Dot(nrm[i], dab.viewDir) >= 0) continue;
        idx.push_back(uint32_t(i));
        fall.push_back(SculptFalloff(dist / dab.radius, falloff));
    }
    if (idx.empty()) return 0;

    // Every target below is read from the weights before this dab, so vertex order doesn't matter.
    std::vector<float> target(idx.size(), 0.0f);
    if (dab.mode == WeightBrush::Smooth || dab.mode == WeightBrush::Sharpen) {
        std::vector<float> mean, mn, mx;
        NeighbourStats(arm.weights, tris, dab.bone, mean, mn, mx);
        for (size_t k = 0; k < idx.size(); ++k) target[k] = mean[idx[k]];
    } else if (dab.mode == WeightBrush::Average) {
        float s = 0, ws = 0;   // the falloff-weighted mean of the whole brush area
        for (size_t k = 0; k < idx.size(); ++k) { s += fall[k] * BoneWeight(arm.weights[idx[k]], dab.bone); ws += fall[k]; }
        std::fill(target.begin(), target.end(), ws > 0 ? s / ws : 0.0f);
    } else if (dab.mode == WeightBrush::Blur) {
        // Each vertex takes the distance-weighted mean of the brush vertices within one brush radius,
        // found through a grid of cells that size.
        // shortcut: O(brush vertices^2) on very dense meshes; cap the samples per vertex if Blur stutters.
        const float r = dab.radius;
        auto cell = [r](Float3 p) { return std::array<long long, 3>{(long long)std::floor(p.x / r), (long long)std::floor(p.y / r), (long long)std::floor(p.z / r)}; };
        auto hash = [](long long x, long long y, long long z) { return (x * 73856093LL) ^ (y * 19349663LL) ^ (z * 83492791LL); };
        std::unordered_multimap<long long, size_t> grid;
        for (size_t k = 0; k < idx.size(); ++k) { auto c = cell(pos[idx[k]]); grid.emplace(hash(c[0], c[1], c[2]), k); }
        for (size_t k = 0; k < idx.size(); ++k) {
            auto c = cell(pos[idx[k]]);
            float s = 0, ws = 0;
            for (long long dx = -1; dx <= 1; ++dx) for (long long dy = -1; dy <= 1; ++dy) for (long long dz = -1; dz <= 1; ++dz) {
                auto range = grid.equal_range(hash(c[0] + dx, c[1] + dy, c[2] + dz));
                for (auto it = range.first; it != range.second; ++it) {
                    float d = Len(Sub(pos[idx[it->second]], pos[idx[k]]));
                    if (d > r) continue;
                    float wgt = 1.0f - d / r;
                    s += wgt * BoneWeight(arm.weights[idx[it->second]], dab.bone); ws += wgt;
                }
            }
            target[k] = ws > 0 ? s / ws : BoneWeight(arm.weights[idx[k]], dab.bone);
        }
    }

    const int mbone = dab.mirrorX ? MirrorBone(arm, dab.bone) : dab.bone;
    std::vector<int> mirror;
    if (dab.mirrorX) mirror = MirrorTable(rest && rest->size() == n ? *rest : pos);

    std::vector<float> next(idx.size());
    for (size_t k = 0; k < idx.size(); ++k) {
        const uint32_t v = idx[k];
        const float cur = BoneWeight(arm.weights[v], dab.bone), amt = std::clamp(dab.strength, 0.0f, 1.0f) * fall[k];
        float nw = cur;
        switch (dab.mode) {
            case WeightBrush::Add:      nw = cur + amt * dab.value; break;
            case WeightBrush::Subtract: nw = cur - amt * dab.value; break;
            case WeightBrush::Replace:  nw = cur + (dab.value - cur) * amt; break;
            case WeightBrush::Multiply: nw = cur * (1.0f + (dab.value - 1.0f) * amt); break;
            case WeightBrush::Sharpen:  nw = cur + (cur - target[k]) * amt; break;
            case WeightBrush::Smooth: case WeightBrush::Blur: case WeightBrush::Average:
                nw = cur + (target[k] - cur) * amt; break;
        }
        if (dab.strokeStart && v < dab.strokeStart->size()) {   // accumulate off: at most one full dab per stroke
            const float s = BoneWeight((*dab.strokeStart)[v], dab.bone);
            if (dab.mode == WeightBrush::Add) nw = std::min(nw, std::max(cur, s + amt * dab.value));
            else if (dab.mode == WeightBrush::Subtract) nw = std::max(nw, std::min(cur, s - amt * dab.value));
            else if (dab.mode == WeightBrush::Multiply) {
                const float lim = s * (1.0f + (dab.value - 1.0f) * amt);
                nw = dab.value < 1 ? std::max(nw, std::min(cur, lim)) : std::min(nw, std::max(cur, lim));
            }
        }
        next[k] = nw;
    }
    for (size_t k = 0; k < idx.size(); ++k) {
        const uint32_t v = idx[k];
        SetBoneWeight(arm.weights[v], dab.bone, next[k], bones, dab.autoNormalize);
        if (!dab.mirrorX) continue;
        const int m = mirror[v];
        if (m < 0 || m == int(v)) continue;   // a centre-line vertex keeps the painted bone
        SetBoneWeight(arm.weights[m], mbone, next[k], bones, dab.autoNormalize);
    }
    return idx.size();
}

size_t ApplyWeightGradient(Armature& arm, const std::vector<Float3>& pos, const SculptSettings& falloff, const WeightGradient& g) {
    if (!arm.Bound() || pos.size() != arm.weights.size() || g.bone < 0 || g.bone >= (int)arm.bones.size()) return 0;
    const Float3 ab = Sub(g.b, g.a);
    const float len2 = Dot(ab, ab);
    if (len2 < 1e-12f) return 0;
    size_t changed = 0;
    for (size_t i = 0; i < pos.size(); ++i) {
        const Float3 ap = Sub(pos[i], g.a);
        float t = g.radial ? Len(ap) / std::sqrt(len2) : Dot(ap, ab) / len2;
        if (t > 1) continue;
        t = std::max(t, 0.0f);
        const float cur = BoneWeight(arm.weights[i], g.bone);
        const float want = g.value * SculptFalloff(t, falloff);
        const float nw = cur + (want - cur) * std::clamp(g.strength, 0.0f, 1.0f);
        if (std::fabs(nw - cur) < 1e-7f) continue;
        SetBoneWeight(arm.weights[i], g.bone, nw, &arm.bones, g.autoNormalize);
        ++changed;
    }
    return changed;
}

float SampleWeight(const std::vector<VertexWeights>& weights, const std::vector<Float3>& pos, const std::vector<Tri>& tris,
                   uint32_t tri, Float3 point, int bone) {
    if (tri >= tris.size()) return 0;
    const Tri& t = tris[tri];
    if (t.a >= pos.size() || t.b >= pos.size() || t.c >= pos.size() || t.a >= weights.size() || t.b >= weights.size() || t.c >= weights.size()) return 0;
    float u, v, w;
    Barycentric(point, pos[t.a], pos[t.b], pos[t.c], u, v, w);
    u = std::max(u, 0.0f); v = std::max(v, 0.0f); w = std::max(w, 0.0f);
    const float s = u + v + w;
    if (s <= 0) return BoneWeight(weights[t.a], bone);
    return (u * BoneWeight(weights[t.a], bone) + v * BoneWeight(weights[t.b], bone) + w * BoneWeight(weights[t.c], bone)) / s;
}

size_t NormalizeAllWeights(Armature& arm) {
    size_t changed = 0;
    for (VertexWeights& vw : arm.weights) changed += NormalizeUnlocked(vw, &arm.bones) ? 1 : 0;
    return changed;
}

size_t CleanWeights(Armature& arm, float threshold) {
    size_t removed = 0;
    for (VertexWeights& vw : arm.weights) {
        int strongest = -1;
        for (int s = 0; s < 4; ++s)
            if (vw.bone[s] >= 0 && (strongest < 0 || vw.weight[s] > vw.weight[strongest])) strongest = s;
        for (int s = 0; s < 4; ++s)
            if (s != strongest && vw.bone[s] >= 0 && !Locked(&arm.bones, vw.bone[s]) && vw.weight[s] < threshold) {
                vw.bone[s] = -1; vw.weight[s] = 0; ++removed;
            }
        NormalizeUnlocked(vw, &arm.bones);
    }
    return removed;
}

size_t MapWeights(Armature& arm, int bone, const std::function<float(float)>& f, bool normalize, bool includeMissing) {
    size_t changed = 0;
    for (VertexWeights& vw : arm.weights) {
        if (bone >= 0) {
            bool has = false;
            for (int s = 0; s < 4; ++s) has |= vw.bone[s] == bone;
            if (!has && !includeMissing) continue;
            const float cur = BoneWeight(vw, bone), nw = std::clamp(f(cur), 0.0f, 1.0f);
            if (std::fabs(nw - cur) < 1e-7f) continue;
            SetBoneWeight(vw, bone, nw, &arm.bones, normalize);
            ++changed;
        } else {
            bool any = false;
            for (int s = 0; s < 4; ++s) {
                if (vw.bone[s] < 0 || Locked(&arm.bones, vw.bone[s])) continue;
                const float nw = std::clamp(f(vw.weight[s]), 0.0f, 1.0f);
                if (std::fabs(nw - vw.weight[s]) < 1e-7f) continue;
                vw.weight[s] = nw; any = true;
            }
            if (any && normalize) NormalizeUnlocked(vw, &arm.bones);
            changed += any ? 1 : 0;
        }
    }
    return changed;
}

size_t InvertWeights(Armature& arm, int bone, bool normalize) { return MapWeights(arm, bone, [](float w) { return 1.0f - w; }, normalize); }
size_t LevelsWeights(Armature& arm, int bone, float offset, float gain, bool normalize) {
    return MapWeights(arm, bone, [=](float w) { return w * gain + offset; }, normalize);
}
size_t ClampWeights(Armature& arm, int bone, float lo, float hi, bool normalize) {
    return MapWeights(arm, bone, [=](float w) { return std::clamp(w, std::min(lo, hi), std::max(lo, hi)); }, normalize);
}
size_t QuantizeWeights(Armature& arm, int bone, int steps, bool normalize) {
    steps = std::max(1, steps);
    return MapWeights(arm, bone, [steps](float w) { return std::round(w * steps) / steps; }, normalize);
}

size_t ClearBoneWeights(Armature& arm, int bone) {
    size_t changed = 0;
    for (VertexWeights& vw : arm.weights) {
        bool any = false;
        for (int s = 0; s < 4; ++s) {
            if (vw.bone[s] < 0 || Locked(&arm.bones, vw.bone[s]) || (bone >= 0 && vw.bone[s] != bone)) continue;
            vw.bone[s] = -1; vw.weight[s] = 0; any = true;
        }
        if (any) { NormalizeUnlocked(vw, &arm.bones); ++changed; }
    }
    return changed;
}

size_t SmoothWeights(Armature& arm, const std::vector<Tri>& tris, int bone, int iterations, float factor, float expand, bool normalize) {
    std::vector<int> targets;
    if (bone >= 0) { if (bone < (int)arm.bones.size() && !arm.bones[bone].weightLocked) targets.push_back(bone); }
    else for (int b = 0; b < (int)arm.bones.size(); ++b) if (!arm.bones[b].weightLocked) targets.push_back(b);
    factor = std::clamp(factor, 0.0f, 1.0f);
    expand = std::clamp(expand, -1.0f, 1.0f);
    std::vector<char> touched(arm.weights.size(), 0);
    for (int it = 0; it < std::max(1, iterations); ++it)
        for (int b : targets) {
            std::vector<float> mean, mn, mx;
            NeighbourStats(arm.weights, tris, b, mean, mn, mx);
            for (size_t v = 0; v < arm.weights.size(); ++v) {
                const float cur = BoneWeight(arm.weights[v], b);
                const float want = expand >= 0 ? mean[v] + expand * (mx[v] - mean[v]) : mean[v] + expand * (mean[v] - mn[v]);
                const float nw = cur + (want - cur) * factor;
                if (std::fabs(nw - cur) < 1e-7f) continue;
                SetBoneWeight(arm.weights[v], b, nw, &arm.bones, normalize);
                touched[v] = 1;
            }
        }
    return (size_t)std::count(touched.begin(), touched.end(), 1);
}

size_t LimitTotalWeights(Armature& arm, int maxInfluences) {
    maxInfluences = std::clamp(maxInfluences, 1, 4);
    size_t changed = 0;
    for (VertexWeights& vw : arm.weights) {
        int order[4] = {0, 1, 2, 3}, lockedCount = 0;
        for (int s = 0; s < 4; ++s) lockedCount += vw.bone[s] >= 0 && Locked(&arm.bones, vw.bone[s]);
        std::sort(order, order + 4, [&](int a, int b) { return vw.weight[a] > vw.weight[b]; });
        int keep = std::max(0, maxInfluences - lockedCount);
        bool any = false;
        for (int k = 0; k < 4; ++k) {
            const int s = order[k];
            if (vw.bone[s] < 0 || Locked(&arm.bones, vw.bone[s])) continue;
            if (keep > 0 && vw.weight[s] > 0) { --keep; continue; }
            vw.bone[s] = -1; vw.weight[s] = 0; any = true;
        }
        if (any) { NormalizeUnlocked(vw, &arm.bones); ++changed; }
    }
    return changed;
}

size_t MirrorWeights(Armature& arm, const std::vector<Float3>& rest, int bone) {
    if (rest.size() != arm.weights.size()) return 0;
    std::vector<std::pair<int, int>> pairs;   // (left bone, right twin)
    for (int b = 0; b < (int)arm.bones.size(); ++b) {
        if (bone >= 0 && b != bone) continue;
        const int twin = MirrorBone(arm, b);
        if (twin == b || arm.bones[twin].weightLocked || (bone < 0 && !IsLeftName(arm.bones[b].name))) continue;
        pairs.push_back({b, twin});
    }
    if (pairs.empty()) return 0;
    const std::vector<int> mirror = MirrorTable(rest);
    const std::vector<VertexWeights> src = arm.weights;
    std::vector<char> touched(arm.weights.size(), 0);
    for (auto [b, twin] : pairs)
        for (size_t v = 0; v < src.size(); ++v) {
            const int m = mirror[v];
            if (m < 0 || m == int(v) || rest[v].x <= 0) continue;   // read the left (+x) side only
            const float want = BoneWeight(src[v], b), cur = BoneWeight(arm.weights[m], twin);
            if (std::fabs(want - cur) < 1e-7f) continue;
            SetBoneWeight(arm.weights[m], twin, want, &arm.bones, true);
            touched[m] = 1;
        }
    return (size_t)std::count(touched.begin(), touched.end(), 1);
}

size_t AssignWeights(Armature& arm, const std::vector<uint32_t>& verts, int bone, float value, bool normalize) {
    if (bone < 0 || bone >= (int)arm.bones.size()) return 0;
    size_t changed = 0;
    for (uint32_t v : verts) {
        if (v >= arm.weights.size()) continue;
        SetBoneWeight(arm.weights[v], bone, value, &arm.bones, normalize);
        ++changed;
    }
    return changed;
}

size_t TransferWeights(Armature& dst, const std::vector<Float3>& dstPos,
                       const std::vector<VertexWeights>& srcWeights, const std::vector<Float3>& srcPos, const std::vector<Tri>& srcTris) {
    if (dstPos.size() != dst.weights.size() || srcWeights.size() != srcPos.size() || srcTris.empty()) return 0;
    const size_t sn = srcPos.size();
    size_t changed = 0;
    // shortcut: brute-force nearest triangle (O(dst verts x src tris)); add a BVH if transfers between
    // dense meshes get slow.
    for (size_t i = 0; i < dstPos.size(); ++i) {
        float best = 1e30f, bu = 1, bv = 0, bw = 0;
        const Tri* bt = nullptr;
        for (const Tri& t : srcTris) {
            if (t.a >= sn || t.b >= sn || t.c >= sn) continue;
            float u, v, w;
            ClosestOnTri(dstPos[i], srcPos[t.a], srcPos[t.b], srcPos[t.c], u, v, w);
            Float3 q{u * srcPos[t.a].x + v * srcPos[t.b].x + w * srcPos[t.c].x,
                     u * srcPos[t.a].y + v * srcPos[t.b].y + w * srcPos[t.c].y,
                     u * srcPos[t.a].z + v * srcPos[t.b].z + w * srcPos[t.c].z};
            const Float3 d = Sub(q, dstPos[i]);
            const float d2 = Dot(d, d);
            if (d2 < best) { best = d2; bt = &t; bu = u; bv = v; bw = w; }
        }
        if (!bt) continue;
        std::unordered_map<int, float> acc;
        const uint32_t corner[3] = {bt->a, bt->b, bt->c};
        const float bc[3] = {bu, bv, bw};
        for (int c = 0; c < 3; ++c)
            for (int s = 0; s < 4; ++s)
                if (srcWeights[corner[c]].bone[s] >= 0) acc[srcWeights[corner[c]].bone[s]] += bc[c] * srcWeights[corner[c]].weight[s];
        std::vector<std::pair<int, float>> list(acc.begin(), acc.end());
        std::sort(list.begin(), list.end(), [](auto& a, auto& b) { return a.second != b.second ? a.second > b.second : a.first < b.first; });
        VertexWeights vw;
        float sum = 0;
        for (int s = 0; s < 4 && s < (int)list.size(); ++s) { if (list[s].second <= 1e-6f) break; vw.bone[s] = list[s].first; vw.weight[s] = list[s].second; sum += list[s].second; }
        if (sum > 0) for (int s = 0; s < 4; ++s) vw.weight[s] /= sum;
        dst.weights[i] = vw;
        ++changed;
    }
    return changed;
}

std::vector<int> BoneInfluenceCounts(const Armature& arm, size_t boneCount) {
    std::vector<int> counts(boneCount, 0);
    for (const VertexWeights& vw : arm.weights)
        for (int s = 0; s < 4; ++s)
            if (vw.bone[s] >= 0 && vw.bone[s] < (int)boneCount && vw.weight[s] > 0) ++counts[vw.bone[s]];
    return counts;
}
