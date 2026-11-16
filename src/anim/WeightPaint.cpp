#include "anim/WeightPaint.h"
#include "tools/Sculpt.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>

float BoneWeight(const VertexWeights& vw, int bone) {
    for (int s = 0; s < 4; ++s) if (vw.bone[s] == bone) return vw.weight[s];
    return 0.0f;
}

void SetBoneWeight(VertexWeights& vw, int bone, float w) {
    w = std::clamp(w, 0.0f, 1.0f);
    int slot = -1;
    for (int s = 0; s < 4; ++s) if (vw.bone[s] == bone) slot = s;
    if (slot < 0) {
        if (w <= 0) return;
        slot = 0;   // an empty slot, else the weakest influence makes room
        for (int s = 0; s < 4; ++s) {
            if (vw.bone[s] < 0 || vw.weight[s] <= 0) { slot = s; break; }
            if (vw.weight[s] < vw.weight[slot]) slot = s;
        }
        vw.bone[slot] = bone;
    }
    float others = 0;
    for (int s = 0; s < 4; ++s) if (s != slot && vw.bone[s] >= 0) others += vw.weight[s];
    if (others <= 1e-6f) { vw.weight[slot] = 1.0f; return; }
    vw.weight[slot] = w;
    const float k = (1.0f - w) / others;
    for (int s = 0; s < 4; ++s) if (s != slot && vw.bone[s] >= 0) vw.weight[s] *= k;
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

size_t ApplyWeightDab(Armature& arm, const std::vector<Float3>& pos, const std::vector<Tri>& tris,
                      const SculptSettings& falloff, const WeightDab& dab, const std::vector<Float3>* rest) {
    if (!arm.Bound() || pos.size() != arm.weights.size() || dab.bone < 0 || dab.bone >= (int)arm.bones.size() ||
        !(dab.radius > 0))
        return 0;
    const size_t n = pos.size();
    std::vector<uint32_t> idx;
    std::vector<float> fall;
    for (size_t i = 0; i < n; ++i) {
        Float3 d{pos[i].x - dab.center.x, pos[i].y - dab.center.y, pos[i].z - dab.center.z};
        float dist = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        if (dist > dab.radius) continue;
        idx.push_back(uint32_t(i));
        fall.push_back(SculptFalloff(dist / dab.radius, falloff));
    }
    if (idx.empty()) return 0;

    // Smooth: the active bone's mean weight over each vertex's edge neighbours, from the weights
    // before this dab so the result does not depend on vertex order.
    std::vector<float> avg;
    if (dab.mode == WeightBrush::Smooth) {
        std::vector<int> slot(n, -1);
        for (size_t k = 0; k < idx.size(); ++k) slot[idx[k]] = int(k);
        std::vector<float> sum(idx.size(), 0.0f), cnt(idx.size(), 0.0f);
        auto edge = [&](uint32_t a, uint32_t b) {
            if (a >= n || b >= n) return;
            if (slot[a] >= 0) { sum[slot[a]] += BoneWeight(arm.weights[b], dab.bone); cnt[slot[a]] += 1; }
            if (slot[b] >= 0) { sum[slot[b]] += BoneWeight(arm.weights[a], dab.bone); cnt[slot[b]] += 1; }
        };
        for (const Tri& t : tris) { edge(t.a, t.b); edge(t.b, t.c); edge(t.c, t.a); }
        avg.resize(idx.size());
        for (size_t k = 0; k < idx.size(); ++k)
            avg[k] = cnt[k] > 0 ? sum[k] / cnt[k] : BoneWeight(arm.weights[idx[k]], dab.bone);
    }

    // Mirror lookup: vertices hashed on a fine grid, probed at (-x, y, z).
    const int mbone = dab.mirrorX ? MirrorBone(arm, dab.bone) : dab.bone;
    std::unordered_map<long long, uint32_t> grid;
    const float q = 1e-4f;
    auto key = [q](Float3 p) {
        long long x = std::llround(p.x / q), y = std::llround(p.y / q), z = std::llround(p.z / q);
        return (x * 73856093LL) ^ (y * 19349663LL) ^ (z * 83492791LL);
    };
    const std::vector<Float3>& mp = rest && rest->size() == n ? *rest : pos;
    if (dab.mirrorX) for (size_t i = 0; i < n; ++i) grid.emplace(key(mp[i]), uint32_t(i));

    std::vector<float> next(idx.size());
    for (size_t k = 0; k < idx.size(); ++k) {
        const float cur = BoneWeight(arm.weights[idx[k]], dab.bone), amt = std::clamp(dab.strength, 0.0f, 1.0f) * fall[k];
        switch (dab.mode) {
            case WeightBrush::Add:      next[k] = cur + amt * dab.value; break;
            case WeightBrush::Subtract: next[k] = cur - amt * dab.value; break;
            case WeightBrush::Replace:  next[k] = cur + (dab.value - cur) * amt; break;
            case WeightBrush::Smooth:   next[k] = cur + (avg[k] - cur) * amt; break;
        }
    }
    for (size_t k = 0; k < idx.size(); ++k) {
        const uint32_t v = idx[k];
        SetBoneWeight(arm.weights[v], dab.bone, next[k]);
        if (!dab.mirrorX) continue;
        auto it = grid.find(key({-mp[v].x, mp[v].y, mp[v].z}));
        if (it == grid.end() || it->second == v) continue;   // a centre-line vertex keeps the painted bone
        const Float3& m = mp[it->second];
        if (std::fabs(m.x + mp[v].x) + std::fabs(m.y - mp[v].y) + std::fabs(m.z - mp[v].z) > 4 * q) continue;   // hash collision
        SetBoneWeight(arm.weights[it->second], mbone, next[k]);
    }
    return idx.size();
}

namespace {
bool Normalize(VertexWeights& vw) {
    float sum = 0;
    for (int s = 0; s < 4; ++s) if (vw.bone[s] >= 0) sum += vw.weight[s];
    if (sum <= 1e-6f || std::fabs(sum - 1.0f) < 1e-6f) return false;
    for (int s = 0; s < 4; ++s) if (vw.bone[s] >= 0) vw.weight[s] /= sum;
    return true;
}
}

size_t NormalizeAllWeights(Armature& arm) {
    size_t changed = 0;
    for (VertexWeights& vw : arm.weights) changed += Normalize(vw) ? 1 : 0;
    return changed;
}

size_t CleanWeights(Armature& arm, float threshold) {
    size_t removed = 0;
    for (VertexWeights& vw : arm.weights) {
        int strongest = -1;
        for (int s = 0; s < 4; ++s)
            if (vw.bone[s] >= 0 && (strongest < 0 || vw.weight[s] > vw.weight[strongest])) strongest = s;
        for (int s = 0; s < 4; ++s)
            if (s != strongest && vw.bone[s] >= 0 && vw.weight[s] < threshold) { vw.bone[s] = -1; vw.weight[s] = 0; ++removed; }
        Normalize(vw);
    }
    return removed;
}
