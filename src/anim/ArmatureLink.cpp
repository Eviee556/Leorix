#include "anim/ArmatureLink.h"
#include <algorithm>
#include <cmath>

namespace {
// R_arm^-1 * R_target: the target object's rotation seen from the armature object's space.
Mat4 RelativeRotation(const Transform& arm, const Transform& target) {
    Transform ra{{0, 0, 0}, arm.rotation, {1, 1, 1}}, rt{{0, 0, 0}, target.rotation, {1, 1, 1}};
    Mat4 m;
    const Float3 axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (int i = 0; i < 3; ++i) {
        Float3 v = ra.InverseApply(rt.Apply(axes[i]));
        m.m[i] = v.x; m.m[4 + i] = v.y; m.m[8 + i] = v.z;
    }
    return m;
}
bool SetIfChanged(Float3& dst, const Float3& v) {
    bool ch = std::fabs(dst.x - v.x) + std::fabs(dst.y - v.y) + std::fabs(dst.z - v.z) > 1e-6f;
    dst = v;
    return ch;
}
bool SetIfChanged(Mat4& dst, const Mat4& v) {
    bool ch = false;
    for (int i = 0; i < 16; ++i) ch |= std::fabs(dst.m[i] - v.m[i]) > 1e-6f;
    dst = v;
    return ch;
}
// What a linked skin depends on besides the pose: both transforms and the enable switches.
std::vector<float> LinkKey(const MeshObject* a, const MeshObject& o) {
    if (!a || a == &o) return {0};
    std::vector<float> k{1, a->armature.enabled ? 1.0f : 0.0f, o.armature.enabled ? 1.0f : 0.0f};
    for (const Transform* t : {&a->transform, &o.transform})
        for (const Float3* v : {&t->position, &t->rotation, &t->scale}) k.insert(k.end(), {v->x, v->y, v->z});
    return k;
}
}  // namespace

const MeshObject* BoneSource(const Scene& scene, const MeshObject& o) {
    if (o.armatureObject) {
        const MeshObject* a = scene.FindById(o.armatureObject);
        if (a && a != &o && !a->armature.bones.empty()) return a;
    }
    return o.armature.bones.empty() ? nullptr : &o;
}

bool ResolveObjectTargets(const Scene& scene, MeshObject& armObj) {
    bool changed = false;
    for (Bone& b : armObj.armature.bones)
        for (BoneConstraint& c : b.constraints) {
            if (const MeshObject* t = scene.FindById(c.targetObject); t && t != &armObj) {
                changed |= SetIfChanged(c.point, armObj.transform.InverseApply(t->transform.position));
                changed |= SetIfChanged(c.objectRot, RelativeRotation(armObj.transform, t->transform));
            }
            if (const MeshObject* p = scene.FindById(c.poleObject); p && p != &armObj)
                changed |= SetIfChanged(c.polePoint, armObj.transform.InverseApply(p->transform.position));
        }
    return changed;
}

void SkinObject(const Scene& scene, MeshObject& o) {
    if (!o.armature.bones.empty()) ResolveObjectTargets(scene, o);
    const MeshObject* src = BoneSource(scene, o);
    o.rigKey = LinkKey(src, o);
    if (!src || !o.armature.enabled || !o.armature.Bound() || !src->armature.enabled) { o.skinned.clear(); return; }
    if (src == &o) { o.armature.Skin(o.skinned); return; }
    // Linked: take each rest vertex into the armature object's space, skin it there, and bring it back.
    std::vector<Mat4> mats;
    src->armature.Evaluate(mats);
    const Armature& w = o.armature;
    o.skinned.resize(w.restPositions.size());
    for (size_t v = 0; v < w.restPositions.size(); ++v) {
        Float3 q = src->transform.InverseApply(o.transform.Apply(w.restPositions[v]));
        o.skinned[v] = o.transform.InverseApply(src->transform.Apply(SkinPoint(mats, w.weights[v], q)));
    }
}

void SkinWithLinks(const Scene& scene, MeshObject& o) {
    SkinObject(scene, o);
    if (o.id == 0 || o.armature.bones.empty()) return;
    for (const auto& up : scene.objects)
        if (up.get() != &o && up->armatureObject == o.id) SkinObject(scene, *up);
}

bool BindToArmatureObject(const Scene& scene, MeshObject& mesh, MeshObject& armObj) {
    if (&mesh == &armObj || armObj.id == 0 || armObj.armature.bones.empty() || mesh.mesh.positions.empty()) return false;
    Armature tmp;
    tmp.bones = armObj.armature.bones;
    Mesh inArm = mesh.mesh;   // weights are measured where the bones are: in the armature's space
    for (Float3& p : inArm.positions) p = armObj.transform.InverseApply(mesh.transform.Apply(p));
    tmp.BindAutomaticWeights(inArm);
    mesh.armature.weights = std::move(tmp.weights);
    mesh.armature.restPositions = mesh.mesh.positions;
    mesh.armatureObject = armObj.id;
    SkinObject(scene, mesh);
    return true;
}

int SyncRigs(Scene& scene) {
    int n = 0;
    for (auto& o : scene.objects)
        if (!o->armature.bones.empty() && ResolveObjectTargets(scene, *o)) { SkinWithLinks(scene, *o); ++n; }
    for (auto& o : scene.objects) {
        if (!o->armatureObject) continue;
        const MeshObject* src = BoneSource(scene, *o);
        bool live = src && src != o.get() && o->armature.enabled && o->armature.Bound() && src->armature.enabled;
        if (LinkKey(src, *o) != o->rigKey || (live && o->skinned.size() != o->armature.restPositions.size())) {
            SkinObject(scene, *o);
            ++n;
        }
    }
    return n;
}

bool LinkedRigForExport(const Scene& scene, const MeshObject& child, Mesh& meshOut, Armature& armOut,
                        const Action** actionOut) {
    const MeshObject* a = BoneSource(scene, child);
    if (!a || a == &child || !child.armature.Bound()) return false;
    auto toArm = [&](const Float3& p) { return a->transform.InverseApply(child.transform.Apply(p)); };
    meshOut = child.mesh;
    for (Float3& p : meshOut.positions) p = toArm(p);
    meshOut.ComputeNormals();
    armOut = a->armature;
    armOut.weights = child.armature.weights;
    armOut.restPositions.resize(child.armature.restPositions.size());
    for (size_t i = 0; i < armOut.restPositions.size(); ++i) armOut.restPositions[i] = toArm(child.armature.restPositions[i]);
    if (actionOut) *actionOut = &a->action;
    return true;
}

Mesh ControlWire(ControlShape shape, float size) {
    Mesh m;
    auto seg = [&](Float3 a, Float3 b) { m.positions.push_back(a); m.positions.push_back(b); };
    const float r = size;
    if (shape == ControlShape::Circle) {
        const int n = 24;
        for (int i = 0; i < n; ++i) {
            float a0 = 6.2831853f * i / n, a1 = 6.2831853f * (i + 1) / n;
            seg({r * std::cos(a0), 0, r * std::sin(a0)}, {r * std::cos(a1), 0, r * std::sin(a1)});
        }
    } else if (shape == ControlShape::Box) {
        for (int i = 0; i < 4; ++i) {   // four edges along each axis
            float u = (i & 1) ? r : -r, v = (i & 2) ? r : -r;
            seg({-r, u, v}, {r, u, v});
            seg({u, -r, v}, {u, r, v});
            seg({u, v, -r}, {u, v, r});
        }
    } else {
        const Float3 tips[6] = {{r, 0, 0}, {-r, 0, 0}, {0, r, 0}, {0, -r, 0}, {0, 0, r}, {0, 0, -r}};
        for (int a = 0; a < 2; ++a)
            for (int b = 2; b < 6; ++b) seg(tips[a], tips[b]);
        for (int b = 2; b < 4; ++b)
            for (int c = 4; c < 6; ++c) seg(tips[b], tips[c]);
    }
    m.normals.assign(m.positions.size(), Float3{0, 1, 0});
    return m;
}

std::vector<MeshObject*> AddControlRig(Scene& scene, MeshObject& armObj) {
    Armature& arm = armObj.armature;
    int role[21];
    std::fill(std::begin(role), std::end(role), -1);
    for (int i = 0; i < (int)arm.bones.size(); ++i) {
        int r = HumanoidRole(arm.bones[i].name);
        if (r >= 0 && role[r] < 0) role[r] = i;
        for (const BoneConstraint& c : arm.bones[i].constraints)
            if (c.targetObject || c.poleObject) return {};   // already has controls
    }
    // hips, chest, then per side: forearm (7/15) and shin (10/18), each needing its parent
    const int need[] = {0, 2, 6, 7, 9, 10, 14, 15, 17, 18};
    for (int r : need) if (role[r] < 0) return {};

    float lo = 1e30f, hi = -1e30f;
    for (const Bone& b : arm.bones) { lo = std::min({lo, b.head.y, b.tail.y}); hi = std::max({hi, b.head.y, b.tail.y}); }
    const float h = std::max(hi - lo, 1e-3f);
    const int armIndex = scene.IndexOf(&armObj);

    std::vector<MeshObject*> made;
    auto control = [&](const std::string& name, ControlShape shape, float size, const Float3& local) {
        MeshObject* c = scene.AddMesh(ControlWire(shape, size * h), "CTRL_" + name);
        c->isControl = true;
        c->transform.position = armObj.transform.Apply(local);
        c->transform.rotation = armObj.transform.rotation;   // so Copy Rotation starts at no change
        made.push_back(c);
        return c->id;
    };
    auto add = [&](int bone, BoneConstraint c) { arm.bones[bone].constraints.push_back(c); };

    const Bone& hips = arm.bones[role[0]];
    uint32_t hipCtrl = control("hips", ControlShape::Circle, 0.14f, hips.head);
    BoneConstraint loc; loc.type = BoneConstraint::CopyLocation; loc.targetObject = hipCtrl; loc.target = -1;
    BoneConstraint rot; rot.type = BoneConstraint::CopyRotation; rot.targetObject = hipCtrl; rot.target = -1;
    add(role[0], loc);
    add(role[0], rot);
    uint32_t chestCtrl = control("chest", ControlShape::Circle, 0.11f, arm.bones[role[2]].head);
    rot.targetObject = chestCtrl;
    add(role[2], rot);

    for (int side = 0; side < 2; ++side) {
        const char* sfx = side == 0 ? ".L" : ".R";
        // Arm: IK on the forearm (upper arm + forearm), hand control at the wrist, pole behind the elbow.
        const Bone& fore = arm.bones[role[7 + side * 8]];
        uint32_t hand = control(std::string("hand") + sfx, ControlShape::Box, 0.035f, fore.tail);
        uint32_t elbow = control(std::string("elbow") + sfx, ControlShape::Diamond, 0.02f,
                                 Float3{fore.head.x, fore.head.y, fore.head.z - 0.15f * h});
        // Leg: IK on the shin (thigh + shin), foot control at the ankle, pole in front of the knee.
        const Bone& shin = arm.bones[role[10 + side * 8]];
        uint32_t foot = control(std::string("foot") + sfx, ControlShape::Box, 0.04f, shin.tail);
        uint32_t knee = control(std::string("knee") + sfx, ControlShape::Diamond, 0.02f,
                                Float3{shin.head.x, shin.head.y, shin.head.z + 0.15f * h});
        BoneConstraint ik; ik.type = BoneConstraint::IK; ik.target = -1; ik.usePole = true; ik.poleTarget = -1;
        ik.targetObject = hand; ik.poleObject = elbow;
        add(role[7 + side * 8], ik);
        ik.targetObject = foot; ik.poleObject = knee;
        add(role[10 + side * 8], ik);
    }
    if (armIndex >= 0) scene.SelectOnly(scene.IndexOf(&armObj));
    SkinWithLinks(scene, armObj);
    return made;
}
