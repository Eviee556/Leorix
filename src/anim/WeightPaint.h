#pragma once
// Weight painting: edits an armature's per-vertex bone weights with a brush. Pure data, no UI, so
// the self-test can run it headless. Weights stay normalized per vertex (auto-normalize).
#include "anim/Armature.h"
#include "tools/Tools3D.h"
#include <vector>

enum class WeightBrush { Add, Subtract, Replace, Smooth };

struct WeightDab {
    Float3      center{0, 0, 0};   // object local space, on the displayed (skinned) surface
    float       radius = 0;
    float       strength = 0.5f;
    float       value = 1.0f;      // target weight for Replace, amount for Add/Subtract
    WeightBrush mode = WeightBrush::Add;
    int         bone = -1;
    bool        mirrorX = false;   // also paint the vertex at -x with the .L/.R twin bone
};

float BoneWeight(const VertexWeights& vw, int bone);
// Sets one bone's weight and scales the vertex's other influences so the total stays 1. A vertex
// with no other influence keeps weight 1 on its only bone, as Blender's auto-normalize does.
void  SetBoneWeight(VertexWeights& vw, int bone, float w);
// The bone with the opposite side's name (upper_arm.L <-> upper_arm.R, hand_l <-> hand_r,
// LeftArm <-> RightArm), or `bone` itself when there is none.
int   MirrorBone(const Armature& arm, int bone);
// `pos` are the positions the brush is measured on (skinned when posed), one per weight.
// `rest` (optional, same size) are the unposed positions the X mirror twin is looked up on, since
// a posed mesh is rarely symmetric.
size_t ApplyWeightDab(Armature& arm, const std::vector<Float3>& pos, const std::vector<Tri>& tris,
                      const SculptSettings& falloff, const WeightDab& dab, const std::vector<Float3>* rest = nullptr);
size_t NormalizeAllWeights(Armature& arm);
// Drops influences below `threshold` (keeping each vertex's strongest one) and renormalizes.
size_t CleanWeights(Armature& arm, float threshold = 0.01f);
