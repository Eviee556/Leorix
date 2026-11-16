#pragma once
// Weight painting: edits an armature's per-vertex bone weights with brushes and whole-mesh
// operations. Pure data, no UI, so the self-test can run it headless. Weights stay normalized per
// vertex (auto-normalize) unless that is switched off, and locked bones (Bone::weightLocked) never
// change: normalization only rescales the unlocked influences.
#include "anim/Armature.h"
#include "tools/Tools3D.h"
#include <functional>
#include <vector>

enum class WeightBrush { Add, Subtract, Replace, Smooth, Blur, Average, Sharpen, Multiply };

struct WeightDab {
    Float3      center{0, 0, 0};   // object local space, on the displayed (skinned) surface
    float       radius = 0;
    float       strength = 0.5f;
    float       value = 1.0f;      // target for Replace, amount for Add/Subtract, factor for Multiply
    WeightBrush mode = WeightBrush::Add;
    int         bone = -1;
    bool        mirrorX = false;   // also paint the vertex at -x with the .L/.R twin bone
    bool        autoNormalize = true;
    bool        frontOnly = false; // skip vertices whose normal faces away from `viewDir`
    Float3      viewDir{0, 0, -1}; // object space, from the eye into the scene
    // Accumulate off: Add/Subtract/Multiply can't push a vertex past one full dab from where the
    // stroke started, however often the brush passes over it.
    const std::vector<VertexWeights>* strokeStart = nullptr;
};

float BoneWeight(const VertexWeights& vw, int bone);
// Sets one bone's weight and rescales the vertex's other unlocked influences so the total stays 1
// (locked ones keep their value; the new weight is capped by what they leave free). A vertex with
// no other influence keeps weight 1 on its only bone, as Blender's auto-normalize does. With
// `normalize` off only this bone's slot changes.
void  SetBoneWeight(VertexWeights& vw, int bone, float w, const std::vector<Bone>* bones = nullptr, bool normalize = true);
// The bone with the opposite side's name (upper_arm.L <-> upper_arm.R, hand_l <-> hand_r,
// LeftArm <-> RightArm), or `bone` itself when there is none.
int   MirrorBone(const Armature& arm, int bone);
// For each vertex, the vertex at (-x, y, z) within 0.4 mm, or -1. A centre-line vertex maps to itself.
std::vector<int> MirrorTable(const std::vector<Float3>& pos);
// `pos` are the positions the brush is measured on (skinned when posed), one per weight.
// `rest` (optional, same size) are the unposed positions the X mirror twin is looked up on, since
// a posed mesh is rarely symmetric.
size_t ApplyWeightDab(Armature& arm, const std::vector<Float3>& pos, const std::vector<Tri>& tris,
                      const SculptSettings& falloff, const WeightDab& dab, const std::vector<Float3>* rest = nullptr);

// Gradient tool: weights fade from `value` at `a` to 0 at `b` (linear: along a->b; radial: by
// distance from a over |b - a|), shaped by the falloff curve and blended in by `strength`.
// Vertices beyond the end are left alone.
struct WeightGradient {
    Float3 a{0, 0, 0}, b{1, 0, 0};
    bool   radial = false;
    float  value = 1.0f, strength = 1.0f;
    int    bone = -1;
    bool   autoNormalize = true;
};
size_t ApplyWeightGradient(Armature& arm, const std::vector<Float3>& pos, const SculptSettings& falloff, const WeightGradient& g);

// The bone's weight at `point` (object space) on triangle `tri`, interpolated from its corners.
float SampleWeight(const std::vector<VertexWeights>& weights, const std::vector<Float3>& pos, const std::vector<Tri>& tris,
                   uint32_t tri, Float3 point, int bone);

// Whole-mesh operations. `bone` >= 0 works on that bone; -1 works on every unlocked bone. Each
// returns how many vertices changed.
size_t NormalizeAllWeights(Armature& arm);
// Drops influences below `threshold` (keeping each vertex's strongest one) and renormalizes.
size_t CleanWeights(Armature& arm, float threshold = 0.01f);
// Maps each weight through `f` (result clamped to 0..1). Single bone: only vertices that already
// have that bone, unless `includeMissing`.
size_t MapWeights(Armature& arm, int bone, const std::function<float(float)>& f, bool normalize = true, bool includeMissing = false);
size_t InvertWeights(Armature& arm, int bone, bool normalize = true);
size_t LevelsWeights(Armature& arm, int bone, float offset, float gain, bool normalize = true);
size_t ClampWeights(Armature& arm, int bone, float lo, float hi, bool normalize = true);
size_t QuantizeWeights(Armature& arm, int bone, int steps, bool normalize = true);
size_t ClearBoneWeights(Armature& arm, int bone);
// Edge-neighbour smoothing, `iterations` passes of `factor`. `expand` > 0 pulls towards the
// neighbours' maximum (grows the area), < 0 towards their minimum (shrinks it).
size_t SmoothWeights(Armature& arm, const std::vector<Tri>& tris, int bone, int iterations, float factor, float expand, bool normalize = true);
// Keeps at most `maxInfluences` per vertex (locked ones always stay), strongest first, renormalized.
size_t LimitTotalWeights(Armature& arm, int maxInfluences);
// Copies a .L bone's weights onto its .R twin at the mirrored vertices (`rest` positions). With
// bone -1 every left-side bone is copied.
size_t MirrorWeights(Armature& arm, const std::vector<Float3>& rest, int bone);
// Edit Mode's selected vertices get `value` on `bone`.
size_t AssignWeights(Armature& arm, const std::vector<uint32_t>& verts, int bone, float value, bool normalize = true);
// Each destination vertex takes the source mesh's weights at its nearest surface point
// (barycentric blend of that triangle's corners, top four kept). Both meshes use the same bones.
size_t TransferWeights(Armature& dst, const std::vector<Float3>& dstPos,
                       const std::vector<VertexWeights>& srcWeights, const std::vector<Float3>& srcPos, const std::vector<Tri>& srcTris);
// How many vertices each bone influences (weight > 0).
std::vector<int> BoneInfluenceCounts(const Armature& arm, size_t boneCount);
