#include "anim/Armature.h"
#include <algorithm>
#include <cmath>

Mat4 Mat4::Translate(const Float3& t) {
    Mat4 r;
    r.m[3] = t.x; r.m[7] = t.y; r.m[11] = t.z;
    return r;
}
Mat4 Mat4::Scale(const Float3& s) {
    Mat4 r;
    r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z;
    return r;
}
Mat4 Mat4::RotateXYZ(const Float3& e) {
    float cx = std::cos(e.x), sx = std::sin(e.x);
    float cy = std::cos(e.y), sy = std::sin(e.y);
    float cz = std::cos(e.z), sz = std::sin(e.z);
    Mat4 r;
    r.m[0] = cy * cz;               r.m[1] = -cy * sz;              r.m[2] = sy;
    r.m[4] = sx * sy * cz + cx * sz; r.m[5] = -sx * sy * sz + cx * cz; r.m[6] = -sx * cy;
    r.m[8] = -cx * sy * cz + sx * sz; r.m[9] = cx * sy * sz + sx * cz; r.m[10] = cx * cy;
    return r;
}
Mat4 Mat4::operator*(const Mat4& o) const {
    Mat4 r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            float sum = 0;
            for (int k = 0; k < 4; ++k) sum += m[i * 4 + k] * o.m[k * 4 + j];
            r.m[i * 4 + j] = sum;
        }
    return r;
}
Float3 Mat4::Apply(const Float3& p) const {
    return {m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3],
            m[4] * p.x + m[5] * p.y + m[6] * p.z + m[7],
            m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11]};
}

int Armature::AddBone(const std::string& name, Float3 head, Float3 tail, int parent) {
    Bone b;
    b.name = name; b.head = head; b.tail = tail; b.parent = parent;
    bones.push_back(b);
    return static_cast<int>(bones.size() - 1);
}

void Armature::Remove(int index) {
    if (index < 0 || index >= (int)bones.size()) return;
    bones.erase(bones.begin() + index);
    for (auto& b : bones) {
        if (b.parent == index) b.parent = -1;
        else if (b.parent > index) --b.parent;
    }
    weights.clear();   // binding no longer valid
}

void Armature::Evaluate(std::vector<Mat4>& out) const {
    out.assign(bones.size(), Mat4::Identity());
    for (size_t i = 0; i < bones.size(); ++i) {
        const Bone& b = bones[i];
        // Pose is applied around the bone's head: move to the head, rotate/scale, move back.
        Mat4 local = Mat4::Translate(b.head) *
                     Mat4::Translate(b.poseLocation) *
                     Mat4::RotateXYZ(b.poseRotation) *
                     Mat4::Scale(b.poseScale) *
                     Mat4::Translate({-b.head.x, -b.head.y, -b.head.z});
        out[i] = (b.parent >= 0 && b.parent < (int)i) ? out[b.parent] * local : local;
    }
}

float DistanceToSegment(const Float3& p, const Float3& a, const Float3& b) {
    Float3 ab{b.x - a.x, b.y - a.y, b.z - a.z};
    Float3 ap{p.x - a.x, p.y - a.y, p.z - a.z};
    float len2 = ab.x * ab.x + ab.y * ab.y + ab.z * ab.z;
    float t = len2 > 1e-9f ? std::clamp((ap.x * ab.x + ap.y * ab.y + ap.z * ab.z) / len2, 0.0f, 1.0f) : 0.0f;
    Float3 c{a.x + ab.x * t - p.x, a.y + ab.y * t - p.y, a.z + ab.z * t - p.z};
    return std::sqrt(c.x * c.x + c.y * c.y + c.z * c.z);
}

void Armature::BindAutomaticWeights(const Mesh& mesh) {
    restPositions = mesh.positions;
    weights.assign(mesh.positions.size(), VertexWeights{});
    if (bones.empty()) return;

    for (size_t v = 0; v < mesh.positions.size(); ++v) {
        // keep the four strongest influences
        struct Cand { int bone; float w; };
        Cand best[4] = {{-1, 0}, {-1, 0}, {-1, 0}, {-1, 0}};
        for (size_t b = 0; b < bones.size(); ++b) {
            if (!bones[b].deform) continue;
            float d = DistanceToSegment(mesh.positions[v], bones[b].head, bones[b].tail);
            float w = 1.0f / (d * d / std::max(0.01f, bones[b].envelope) + 1e-4f);
            for (int s = 0; s < 4; ++s) {
                if (w > best[s].w) {
                    for (int k = 3; k > s; --k) best[k] = best[k - 1];
                    best[s] = {static_cast<int>(b), w};
                    break;
                }
            }
        }
        float total = 0;
        for (auto& c : best) total += c.w;
        if (total <= 0) continue;
        for (int s = 0; s < 4; ++s) {
            weights[v].bone[s] = best[s].bone;
            weights[v].weight[s] = best[s].w / total;
        }
    }
}

void Armature::Skin(std::vector<Float3>& out) const {
    if (!Bound()) return;
    std::vector<Mat4> mats;
    Evaluate(mats);
    out.resize(restPositions.size());
    for (size_t v = 0; v < restPositions.size(); ++v) {
        const Float3& rest = restPositions[v];
        Float3 acc{0, 0, 0};
        float applied = 0;
        for (int s = 0; s < 4; ++s) {
            int b = weights[v].bone[s];
            float w = weights[v].weight[s];
            if (b < 0 || b >= (int)mats.size() || w <= 0) continue;
            Float3 p = mats[b].Apply(rest);
            acc.x += p.x * w; acc.y += p.y * w; acc.z += p.z * w;
            applied += w;
        }
        out[v] = applied > 1e-6f ? Float3{acc.x / applied, acc.y / applied, acc.z / applied} : rest;
    }
}
