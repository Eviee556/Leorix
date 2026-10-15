#pragma once
// Modal transform operator (G / R / S), the way Blender runs it: press, move the mouse,
// optionally press X/Y/Z to constrain, left-click or Enter to confirm, Esc or right-click to cancel.
#include "mesh/Scene.h"   // Transform, Float3
#include "imgui.h"        // ImVec2
#include <cstdint>
#include <vector>

struct TransformOp {
    bool   active = false;
    int    mode = 0;              // 0 grab, 1 rotate, 2 scale
    int    axis = -1;             // -1 free, 0 X, 1 Y, 2 Z
    ImVec2 startMouse{0, 0};
    Float3 pivot;                 // world-space
    float  pivotDepth = 1.0f;
    std::vector<Float3> savedVerts;   // edit mode
    std::vector<uint32_t> movedVerts;
    Transform savedTransform;         // object mode
    int    objectIndex = -1;
};
