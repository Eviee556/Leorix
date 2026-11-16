#pragma once
// Non-destructive modifier stack. The base mesh is never touched: the stack is evaluated top to
// bottom into a display mesh every time something changes, and Apply bakes one entry down.
#include "mesh/Mesh.h"
#include <string>
#include <vector>

#include "mesh/Curve.h"   // CurvePath

enum class ModifierType { Array, Mirror, Solidify, Subdivide, Smooth, Weld, Shrinkwrap, Screw, Remesh,
                          Displace, Triangulate, Decimate, CurveDeform, Count };
const char* ModifierName(ModifierType t);
const char* ModifierHint(ModifierType t);

struct Modifier {
    ModifierType type = ModifierType::Array;
    bool enabled = true;
    bool showInEditMode = true;

    // Array
    int   arrayCount = 2;
    float arrayOffset[3] = {1.0f, 0.0f, 0.0f};   // relative to the mesh bounds, like Blender
    // Mirror
    bool  mirrorX = true, mirrorY = false, mirrorZ = false, mirrorMerge = true;
    float mirrorThreshold = 0.001f;
    // Solidify
    float solidifyThickness = 0.05f;
    bool  solidifyRim = true;
    // Subdivide
    int   subdivLevels = 1;
    bool  subdivSmooth = true;
    // Smooth
    float smoothFactor = 0.5f;
    int   smoothIterations = 2;
    // Weld
    float weldDistance = 0.001f;
    // Shrinkwrap
    int   shrinkTarget = -1;      // target object's id (Scene::IndexOfId); -1 none
    float shrinkOffset = 0.0f;
    // Screw (Blender's Screw modifier, i.e. a spin/revolve)
    int   screwSteps = 16;
    float screwAngle = 360.0f;
    float screwOffset = 0.0f;
    int   screwAxis = 1;          // 0 X, 1 Y, 2 Z
    // Remesh
    float remeshVoxel = 0.1f;
    int   remeshSmooth = 2;
    // Displace
    float displaceStrength = 0.1f, displaceMidLevel = 0.5f, displaceNoiseScale = 2.0f;
    bool  displaceUseNoise = true;
    // Decimate
    float decimateRatio = 0.5f;
    // Curve Deform
    int   curveTarget = -1;        // curve object's id (Scene::IndexOfId); -1 none
    int   curveAxis = 0;           // which mesh axis runs along the curve: 0 X, 1 Y, 2 Z
    float curveStretch = 1.0f;
};

// Evaluates the whole stack. `targets` supplies meshes that Shrinkwrap can aim at.
Mesh EvaluateModifiers(const Mesh& base, const std::vector<Modifier>& stack,
                       const std::vector<const Mesh*>& targets,
                       const std::vector<const CurvePath*>& curves = {});
// One modifier on its own, used by Apply and by the stack evaluator.
Mesh ApplyModifier(const Mesh& in, const Modifier& m, const std::vector<const Mesh*>& targets,
                   const std::vector<const CurvePath*>& curves = {});
