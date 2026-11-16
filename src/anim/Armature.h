#pragma once
// Bones, posing and skinning. Modeled on Blender's armature: bones live in the object's space
// with a head and a tail, each bone can have a parent, and posing a bone deforms the mesh
// through vertex weights.
#include "mesh/Mesh.h"
#include <string>
#include <vector>

// Row-major 4x4, only what skinning needs.
struct Mat4 {
    float m[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    static Mat4 Identity() { return Mat4{}; }
    static Mat4 Translate(const Float3& t);
    static Mat4 RotateXYZ(const Float3& euler);   // radians
    static Mat4 Scale(const Float3& s);
    Mat4 operator*(const Mat4& o) const;
    Float3 Apply(const Float3& p) const;
};

// A bone constraint, evaluated by Armature::Evaluate after the bone's own pose, like Blender's
// constraint stack. Targets are bones of the same armature; with no target bone, `point`
// (object space) is used instead.
struct BoneConstraint {
    enum Type { CopyLocation, CopyRotation, TrackTo, LimitRotation, IK };
    Type   type = IK;
    bool   enabled = true;
    int    target = -1;               // bone index (its posed head is the target), or -1 for `point`
    Float3 point{0, 1, 0};
    // IK: solves this bone and its parent (two-bone chain). The pole sets the bend direction:
    // poleTarget >= 0 uses that bone's head, otherwise polePoint; without a pole the chain keeps
    // bending the way it already bends.
    bool   usePole = false;
    int    poleTarget = -1;
    Float3 polePoint{0, 0, 1};
    // Limit Rotation: per-axis limits (radians) on the bone's own pose rotation.
    bool   limit[3] = {true, true, true};
    Float3 minRot{-0.7854f, -0.7854f, -0.7854f}, maxRot{0.7854f, 0.7854f, 0.7854f};
    // Object targets (scene object ids, 0 = none), used when no target bone is set. The scene
    // resolves them before Evaluate: `point` / `polePoint` become the object's position and
    // `objectRot` its rotation, both in this armature's object space.
    uint32_t targetObject = 0, poleObject = 0;
    Mat4     objectRot;   // runtime only, for Copy Rotation from an object
};
const char* ConstraintTypeName(BoneConstraint::Type t);

struct Bone {
    std::string name = "Bone";
    int    parent = -1;
    Float3 head{0, 0, 0}, tail{0, 1, 0};   // rest pose, object space
    // pose, relative to the rest bone
    Float3 poseRotation{0, 0, 0};          // euler radians
    Float3 poseLocation{0, 0, 0};
    Float3 poseScale{1, 1, 1};
    bool   selected = false;
    float  envelope = 1.0f;                // weighting radius multiplier
    bool   deform = true;
    bool   weightLocked = false;           // Weight Paint: this bone's weights never change
    std::vector<BoneConstraint> constraints;
};

// Up to four influences per vertex, like every real-time skinning pipeline.
struct VertexWeights { int bone[4] = {-1, -1, -1, -1}; float weight[4] = {0, 0, 0, 0}; };

struct Armature {
    std::vector<Bone>          bones;
    std::vector<VertexWeights> weights;      // one per mesh vertex, empty until parented
    std::vector<Float3>        restPositions;// mesh positions when the weights were bound
    bool                       enabled = true;

    int  AddBone(const std::string& name, Float3 head, Float3 tail, int parent = -1);
    void Remove(int index);

    // World (object-space) matrix of each bone in its current pose, constraints included.
    // Order: forward kinematics (with Limit Rotation), then IK solved on that pose, then the other
    // constraints bone by bone, so children follow their constrained parents.
    void Evaluate(std::vector<Mat4>& out) const;

    // "Automatic weights": distance falloff to each bone segment, normalized over the closest
    // four. This is Blender's envelope approach, not its heat-diffusion solver - it binds
    // instantly and behaves predictably, but it ignores topology, so a limb close to the body
    // can pick up the wrong bone.
    void BindAutomaticWeights(const Mesh& mesh);

    // Linear blend skinning of the bound rest positions into `out`.
    void Skin(std::vector<Float3>& out) const;
    bool Bound() const { return !weights.empty() && weights.size() == restPositions.size(); }
};

// Distance from a point to a bone's segment, used by the weighting and by bone picking.
float DistanceToSegment(const Float3& p, const Float3& a, const Float3& b);

// Analytic two-bone IK. root/joint/end are the chain's current positions; the result keeps both
// bone lengths, puts the end on the target (or as far toward it as the chain reaches) and bends
// toward `pole` (or the current bend when pole is null).
void SolveTwoBoneIK(const Float3& root, const Float3& joint, const Float3& end, const Float3& target,
                    const Float3* pole, Float3& newJoint, Float3& newEnd);

// Humanoid rig naming. The same 21-bone humanoid set is known under each convention.
enum class RigNaming { Plain, UE5, Unity, Mixamo, Godot, Count };
const char* RigNamingName(RigNaming n);
// Renames every bone whose name is a known humanoid bone (in any convention). Returns how many.
int ApplyRigNaming(Armature& arm, RigNaming n);
// Appends a T-pose humanoid (hips, spine, chest, neck, head, arms, legs) fitted to the bounds,
// facing +Z with the character's left on +X. Returns the index of its root (hips).
int AddHumanoidRig(Armature& arm, const Float3& bmin, const Float3& bmax, RigNaming n);
// Canonical humanoid role of a bone name in any convention: 0..4 hips, spine, chest, neck, head;
// 5..12 left shoulder, upper arm, forearm, hand, thigh, shin, foot, toe; 13..20 the same on the
// right. -1 when the name is not a known humanoid bone.
int HumanoidRole(const std::string& name);
// One vertex skinned by linear blend: the weighted bone matrices applied to `rest`.
Float3 SkinPoint(const std::vector<Mat4>& mats, const VertexWeights& w, const Float3& rest);
