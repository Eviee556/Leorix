#include "anim/Armature.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace {
Float3 Sub(Float3 a, Float3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Float3 Add(Float3 a, Float3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Float3 Mul(Float3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float  Dot(Float3 a, Float3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Float3 Cross(Float3 a, Float3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float  Len(Float3 a) { return std::sqrt(Dot(a, a)); }
Float3 Norm(Float3 a) { float l = Len(a); return l > 1e-12f ? Mul(a, 1.0f / l) : Float3{0, 0, 0}; }
Float3 AnyPerpendicular(Float3 u) {
    return Norm(std::fabs(u.x) < 0.9f ? Cross(u, {1, 0, 0}) : Cross(u, {0, 1, 0}));
}
// Shortest rotation taking direction a onto direction b (Rodrigues).
Mat4 RotationBetween(Float3 a, Float3 b) {
    a = Norm(a); b = Norm(b);
    Mat4 r;
    if (Len(a) == 0 || Len(b) == 0) return r;
    Float3 v = Cross(a, b);
    float c = Dot(a, b), s2 = Dot(v, v);
    if (s2 < 1e-14f) {
        if (c > 0) return r;
        Float3 k = AnyPerpendicular(a);   // 180 degrees about any perpendicular axis
        const float kk[3] = {k.x, k.y, k.z};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) r.m[i * 4 + j] = 2 * kk[i] * kk[j] - (i == j ? 1.0f : 0.0f);
        return r;
    }
    float f = (1 - c) / s2;
    const float K[9] = {0, -v.z, v.y, v.z, 0, -v.x, -v.y, v.x, 0};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            float k2 = 0;
            for (int t = 0; t < 3; ++t) k2 += K[i * 3 + t] * K[t * 3 + j];
            r.m[i * 4 + j] = (i == j ? 1.0f : 0.0f) + K[i * 3 + j] + k2 * f;
        }
    return r;
}
Mat4 RotateAbout(Float3 pivot, Float3 from, Float3 to) {
    return Mat4::Translate(pivot) * RotationBetween(from, to) * Mat4::Translate(Mul(pivot, -1));
}
}  // namespace

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
    for (auto& b : bones)
        for (auto& c : b.constraints)
            for (int* t : {&c.target, &c.poleTarget}) {
                if (*t == index) *t = -1;
                else if (*t > index) --*t;
            }
    weights.clear();   // binding no longer valid
}

const char* ConstraintTypeName(BoneConstraint::Type t) {
    switch (t) {
        case BoneConstraint::CopyLocation:  return "Copy Location";
        case BoneConstraint::CopyRotation:  return "Copy Rotation";
        case BoneConstraint::TrackTo:       return "Track To";
        case BoneConstraint::LimitRotation: return "Limit Rotation";
        case BoneConstraint::IK:            return "IK";
    }
    return "?";
}

void SolveTwoBoneIK(const Float3& root, const Float3& joint, const Float3& end, const Float3& target,
                    const Float3* pole, Float3& newJoint, Float3& newEnd) {
    newJoint = joint; newEnd = end;
    float l1 = Len(Sub(joint, root)), l2 = Len(Sub(end, joint));
    if (l1 < 1e-6f || l2 < 1e-6f) return;
    Float3 toT = Sub(target, root);
    float d = Len(toT);
    Float3 u = d > 1e-6f ? Mul(toT, 1.0f / d) : Norm(Sub(end, root));
    if (Len(u) == 0) return;
    d = std::clamp(d, std::max(std::fabs(l1 - l2), 1e-4f), l1 + l2);
    // bend plane: the pole (or the current joint) with its component along the chain removed
    Float3 w{0, 0, 0};
    for (Float3 h : {pole ? Sub(*pole, root) : Sub(joint, root), Sub(joint, root)}) {
        w = Sub(h, Mul(u, Dot(h, u)));
        if (Len(w) > 1e-5f) break;
    }
    w = Len(w) > 1e-5f ? Norm(w) : AnyPerpendicular(u);
    float cosA = std::clamp((l1 * l1 + d * d - l2 * l2) / (2 * l1 * d), -1.0f, 1.0f);
    float sinA = std::sqrt(std::max(0.0f, 1 - cosA * cosA));
    newJoint = Add(root, Add(Mul(u, l1 * cosA), Mul(w, l1 * sinA)));
    newEnd = Add(root, Mul(u, d));
}

static Mat4 LocalPose(const Bone& b) {
    Float3 rot = b.poseRotation;
    for (const BoneConstraint& c : b.constraints) {
        if (!c.enabled || c.type != BoneConstraint::LimitRotation) continue;
        float* r = &rot.x;
        const float* lo = &c.minRot.x;
        const float* hi = &c.maxRot.x;
        for (int a = 0; a < 3; ++a)
            if (c.limit[a]) r[a] = std::clamp(r[a], std::min(lo[a], hi[a]), std::max(lo[a], hi[a]));
    }
    // Pose is applied around the bone's head: move to the head, rotate/scale, move back.
    return Mat4::Translate(b.head) * Mat4::Translate(b.poseLocation) * Mat4::RotateXYZ(rot) *
           Mat4::Scale(b.poseScale) * Mat4::Translate({-b.head.x, -b.head.y, -b.head.z});
}

void Armature::Evaluate(std::vector<Mat4>& out) const {
    const int n = (int)bones.size();
    auto parentOf = [&](int i) { int p = bones[i].parent; return (p >= 0 && p < i) ? p : -1; };
    // Pass 1: forward kinematics.
    std::vector<Mat4> fk(n);
    bool constrained = false;
    for (int i = 0; i < n; ++i) {
        Mat4 local = LocalPose(bones[i]);
        fk[i] = parentOf(i) >= 0 ? fk[parentOf(i)] * local : local;
        for (const BoneConstraint& c : bones[i].constraints)
            constrained |= c.enabled && c.type != BoneConstraint::LimitRotation;
    }
    if (!constrained) { out = std::move(fk); return; }

    // Target position: an earlier bone's final pose, a later bone's FK pose, or the point.
    auto targetPos = [&](int t, const Float3& point, int self, const std::vector<Mat4>& cur) {
        if (t < 0 || t >= n || t == self) return point;
        return (t < self ? cur : fk)[t].Apply(bones[t].head);
    };

    // IK corrections for a bone, solved when its parent's final pose is known (see the end of pass 2).
    std::vector<Mat4> corr(n);

    // Pass 2: FK with the IK corrections, then the remaining constraints in stack order.
    out.assign(n, Mat4::Identity());
    for (int i = 0; i < n; ++i) {
        const Bone& b = bones[i];
        Mat4 local = LocalPose(b);
        Mat4 m = corr[i] * (parentOf(i) >= 0 ? out[parentOf(i)] * local : local);
        for (const BoneConstraint& c : b.constraints) {
            if (!c.enabled) continue;
            Float3 h = m.Apply(b.head);
            switch (c.type) {
                case BoneConstraint::CopyLocation:
                    m = Mat4::Translate(Sub(targetPos(c.target, c.point, i, out), h)) * m;
                    break;
                case BoneConstraint::TrackTo:
                    m = RotateAbout(h, Sub(m.Apply(b.tail), h), Sub(targetPos(c.target, c.point, i, out), h)) * m;
                    break;
                case BoneConstraint::CopyRotation: {
                    if (c.target < 0 || c.target >= n || c.target == i) break;
                    // the target's pose rotation (scale stripped); this bone's head stays where it is
                    Mat4 r = (c.target < i ? out : fk)[c.target];
                    for (int col = 0; col < 3; ++col) {
                        Float3 v = Norm({r.m[col], r.m[4 + col], r.m[8 + col]});
                        r.m[col] = v.x; r.m[4 + col] = v.y; r.m[8 + col] = v.z;
                    }
                    r.m[3] = r.m[7] = r.m[11] = 0;
                    m = Mat4::Translate(h) * r * Mat4::Translate(Mul(b.head, -1));
                    break;
                }
                default: break;   // IK and Limit Rotation are already applied
            }
        }
        out[i] = m;
        // Two-bone IK on a child of i: rotate i about its head, then the child about the new joint. Solving here,
        // against i's constrained pose, keeps the chain attached when i or its parents were moved by constraints.
        bool solved = false;   // one IK per parent; a second one would undo the first
        for (int k = i + 1; k < n && !solved; ++k) {
            if (parentOf(k) != i) continue;
            for (const BoneConstraint& c : bones[k].constraints) {
                if (!c.enabled || c.type != BoneConstraint::IK) continue;
                Mat4 mk = out[i] * LocalPose(bones[k]);
                Float3 A = out[i].Apply(b.head), J = mk.Apply(bones[k].head), E = mk.Apply(bones[k].tail);
                auto at = [&](int t, const Float3& point) {   // bones up to i are final, later ones FK
                    if (t < 0 || t >= n || t == k) return point;
                    return (t <= i ? out : fk)[t].Apply(bones[t].head);
                };
                Float3 T = at(c.target, c.point), poleP = at(c.poleTarget, c.polePoint);
                Float3 J2, E2;
                SolveTwoBoneIK(A, J, E, T, c.usePole ? &poleP : nullptr, J2, E2);
                Mat4 cp = RotateAbout(A, Sub(J, A), Sub(J2, A));
                out[i] = cp * out[i];
                corr[k] = RotateAbout(J2, Sub(cp.Apply(E), J2), Sub(E2, J2));
                solved = true;
                break;
            }
        }
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

// ---------------------------------------------------------------- humanoid rigs and naming
namespace {
constexpr int kCenter = 5, kSide = 8, kHumanoid = kCenter + 2 * kSide;
// Per convention: the five centre bones, the eight per-side bones, and the left/right affixes.
struct NamingSet { const char* center[kCenter]; const char* side[kSide]; const char* pre[2]; const char* suf[2]; };
const NamingSet kNaming[(int)RigNaming::Count] = {
    {{"hips", "spine", "chest", "neck", "head"},
     {"shoulder", "upper_arm", "forearm", "hand", "thigh", "shin", "foot", "toe"}, {"", ""}, {".L", ".R"}},
    {{"pelvis", "spine_01", "spine_02", "neck_01", "head"},
     {"clavicle", "upperarm", "lowerarm", "hand", "thigh", "calf", "foot", "ball"}, {"", ""}, {"_l", "_r"}},
    {{"Hips", "Spine", "Chest", "Neck", "Head"},
     {"Shoulder", "UpperArm", "LowerArm", "Hand", "UpperLeg", "LowerLeg", "Foot", "Toes"}, {"Left", "Right"}, {"", ""}},
    {{"mixamorig:Hips", "mixamorig:Spine", "mixamorig:Spine1", "mixamorig:Neck", "mixamorig:Head"},
     {"Shoulder", "Arm", "ForeArm", "Hand", "UpLeg", "Leg", "Foot", "ToeBase"}, {"mixamorig:Left", "mixamorig:Right"}, {"", ""}},
    {{"Hips", "Spine", "Chest", "Neck", "Head"},
     {"Shoulder", "UpperArm", "LowerArm", "Hand", "UpperLeg", "LowerLeg", "Foot", "Toes"}, {"Left", "Right"}, {"", ""}},
};
// Canonical bone k: 0..4 centre, then the left side 5..12 and the right side 13..20.
std::string HumanoidName(RigNaming n, int k) {
    const NamingSet& s = kNaming[(int)n];
    if (k < kCenter) return s.center[k];
    int side = (k - kCenter) / kSide, j = (k - kCenter) % kSide;
    return std::string(s.pre[side]) + s.side[j] + s.suf[side];
}
}  // namespace

const char* RigNamingName(RigNaming n) {
    static const char* names[] = {"Plain", "Unreal Engine 5", "Unity (Mecanim)", "Mixamo", "Godot"};
    return (int)n >= 0 && n < RigNaming::Count ? names[(int)n] : "?";
}

int ApplyRigNaming(Armature& arm, RigNaming n) {
    if ((int)n < 0 || n >= RigNaming::Count) return 0;
    std::unordered_map<std::string, int> known;
    for (int c = 0; c < (int)RigNaming::Count; ++c)
        for (int k = 0; k < kHumanoid; ++k) known.emplace(HumanoidName((RigNaming)c, k), k);
    int renamed = 0;
    for (Bone& b : arm.bones) {
        auto it = known.find(b.name);
        if (it == known.end()) continue;
        std::string name = HumanoidName(n, it->second);
        if (name != b.name) { b.name = name; ++renamed; }
    }
    return renamed;
}

int AddHumanoidRig(Armature& arm, const Float3& bmin, const Float3& bmax, RigNaming n) {
    const float h = std::max(bmax.y - bmin.y, 1e-3f);
    const float cx = (bmin.x + bmax.x) * 0.5f, cz = (bmin.z + bmax.z) * 0.5f;
    auto P = [&](float x, float y, float z) { return Float3{cx + x * h, bmin.y + y * h, cz + z * h}; };
    const int base = (int)arm.bones.size();
    struct B { float hx, hy, hz, tx, ty, tz; int parent; };   // fractions of the height
    // centre chain; parent is a canonical index, -1 for the root
    const B center[kCenter] = {
        {0, 0.50f, 0, 0, 0.58f, 0, -1}, {0, 0.58f, 0, 0, 0.68f, 0, 0}, {0, 0.68f, 0, 0, 0.80f, 0, 1},
        {0, 0.80f, 0, 0, 0.87f, 0, 2},  {0, 0.87f, 0, 0, 1.00f, 0, 3}};
    // left side in a T-pose; parent is a side index, -1 for the chest, -2 for the hips
    const B side[kSide] = {
        {0.02f, 0.80f, 0, 0.12f, 0.80f, 0, -1}, {0.12f, 0.80f, 0, 0.29f, 0.80f, 0, 0},
        {0.29f, 0.80f, 0, 0.44f, 0.80f, 0, 1},  {0.44f, 0.80f, 0, 0.52f, 0.80f, 0, 2},
        {0.06f, 0.50f, 0, 0.06f, 0.27f, 0, -2}, {0.06f, 0.27f, 0, 0.06f, 0.05f, 0, 4},
        {0.06f, 0.05f, 0, 0.06f, 0.01f, 0.08f, 5}, {0.06f, 0.01f, 0.08f, 0.06f, 0.01f, 0.13f, 6}};
    for (int k = 0; k < kCenter; ++k) {
        const B& b = center[k];
        arm.AddBone(HumanoidName(n, k), P(b.hx, b.hy, b.hz), P(b.tx, b.ty, b.tz), b.parent < 0 ? -1 : base + b.parent);
    }
    for (int s = 0; s < 2; ++s) {
        const float sx = s == 0 ? 1.0f : -1.0f;
        for (int j = 0; j < kSide; ++j) {
            const B& b = side[j];
            int parent = b.parent == -1 ? base + 2 : b.parent == -2 ? base : base + kCenter + s * kSide + b.parent;
            arm.AddBone(HumanoidName(n, kCenter + s * kSide + j), P(b.hx * sx, b.hy, b.hz), P(b.tx * sx, b.ty, b.tz), parent);
        }
    }
    return base;
}
