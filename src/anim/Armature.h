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

    // World (object-space) matrix of each bone in its current pose.
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
