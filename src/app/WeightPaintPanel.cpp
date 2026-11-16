// Weight Paint mode: brush strokes on the active bone's weights, the tool settings, and undo.
// The brush math lives in anim/WeightPaint so the self-test can check it without a window.
#include "app/App.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>

namespace {
// The surface the brush is measured on: skinned when posed, so it matches what is picked.
const std::vector<Float3>& BrushPositions(const MeshObject& o) {
    return o.armature.enabled && o.skinned.size() == o.mesh.positions.size() ? o.skinned : o.mesh.positions;
}
}

void App::PushWeightsUndo(const std::string& label, int objIndex, std::vector<VertexWeights> before) {
    if (objIndex < 0 || objIndex >= (int)scene.objects.size()) return;
    MeshObject* o = scene.objects[objIndex].get();
    auto b = std::make_shared<std::vector<VertexWeights>>(std::move(before));
    auto a = std::make_shared<std::vector<VertexWeights>>(o->armature.weights);
    if (b->size() != a->size()) return;
    auto apply = [this, o](const std::vector<VertexWeights>& w) {
        if (scene.IndexOf(o) < 0 || o->armature.weights.size() != w.size()) return;
        o->armature.weights = w;
        UpdateSkinning(*o);
    };
    undoStack.Push(label, [apply, b] { apply(*b); }, [apply, a] { apply(*a); }, (a->size() + b->size()) * sizeof(VertexWeights));
}

void App::WeightStroke(bool begin) {
    MeshObject* o = scene.Active();
    if (!o || !lastPick.hit || lastPickObject != scene.activeIndex) return;
    Armature& arm = o->armature;
    if (!arm.Bound() || arm.weights.size() != o->mesh.positions.size()) {
        if (begin) ShowStatus("Weight Paint needs a mesh parented to an armature (Armature panel > Parent)");
        return;
    }
    if (activeBone < 0 || activeBone >= (int)arm.bones.size()) {
        if (begin) ShowStatus("Pick a bone to paint in the Weight Paint settings");
        return;
    }
    if (begin || !weightPainting) {
        weightPainting = true;
        weightUndoIndex = scene.activeIndex;
        weightBefore = arm.weights;
    }
    // Radius is in screen pixels: convert to object units at the hit depth, as sculpting does.
    Vec3 eye = viewport.CameraEye(), fwd = viewport.CameraForward();
    Float3 hp = lastPick.point;
    float depth = std::max(0.2f, (hp.x - eye.x) * fwd.x + (hp.y - eye.y) * fwd.y + (hp.z - eye.z) * fwd.z);
    const Float3& sc = o->transform.scale;
    float maxScale = std::max({std::fabs(sc.x), std::fabs(sc.y), std::fabs(sc.z), 1e-6f});

    WeightDab dab;
    dab.center = o->transform.InverseApply(hp);
    dab.radius = weightPaint.brush.radiusPx * viewport.WorldPerPixel(depth) / maxScale;
    dab.strength = weightPaint.brush.strength;
    dab.value = weightPaint.value;
    dab.mode = ImGui::GetIO().KeyShift ? WeightBrush::Smooth
             : ImGui::GetIO().KeyCtrl && weightPaint.mode == 0 ? WeightBrush::Subtract
             : (WeightBrush)weightPaint.mode;
    dab.bone = activeBone;
    dab.mirrorX = weightPaint.mirrorX;
    if (ApplyWeightDab(arm, BrushPositions(*o), o->mesh.tris, weightPaint.brush, dab, &o->mesh.positions)) UpdateSkinning(*o);
}

void App::WeightStrokeEnd() {
    weightPainting = false;
    if (weightBefore.empty()) return;
    PushWeightsUndo("Weight Paint", weightUndoIndex, std::move(weightBefore));
    weightBefore.clear();
}

void App::WeightSettingsUI() {
    MeshObject* o = scene.Active();
    if (!o || !o->armature.Bound()) {
        ImGui::TextWrapped("Weight Paint edits how strongly each bone moves the mesh. Add an armature and parent the mesh "
                           "to it (Armature panel) first; automatic weights are the starting point.");
        return;
    }
    Armature& arm = o->armature;
    const char* boneName = activeBone >= 0 && activeBone < (int)arm.bones.size() ? arm.bones[activeBone].name.c_str() : "(pick a bone)";
    if (ImGui::BeginCombo("Bone", boneName)) {
        for (int i = 0; i < (int)arm.bones.size(); ++i) {
            ImGui::PushID(i);
            if (ImGui::Selectable(arm.bones[i].name.c_str(), activeBone == i)) activeBone = i;
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    static const char* kModes[] = {"Add", "Subtract", "Replace", "Smooth"};
    ImGui::Combo("Brush", &weightPaint.mode, kModes, 4);
    ImGui::SliderFloat("Weight", &weightPaint.value, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Radius", &weightPaint.brush.radiusPx, 2.0f, 400.0f, "%.0f px");
    ImGui::SliderFloat("Strength", &weightPaint.brush.strength, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Hardness", &weightPaint.brush.hardness, 0.0f, 0.99f, "%.2f");
    static const char* kCurves[] = {"Smooth", "Smoother", "Sphere", "Root", "Sharp", "Linear", "Sharper", "Inverse Square", "Constant"};
    ImGui::Combo("Falloff", &weightPaint.brush.falloffCurve, kCurves, 9);
    ImGui::Checkbox("X Mirror", &weightPaint.mirrorX);
    ImGui::SetItemTooltip("Also paints the other side, on the matching .L/.R bone");
    if (ImGui::Button("Normalize All")) {
        auto before = arm.weights;
        size_t n = NormalizeAllWeights(arm);
        PushWeightsUndo("Normalize All Weights", scene.activeIndex, std::move(before));
        UpdateSkinning(*o);
        ShowStatus("Normalized " + std::to_string(n) + " vertices");
    }
    ImGui::SameLine();
    if (ImGui::Button("Clean")) {
        auto before = arm.weights;
        size_t n = CleanWeights(arm, 0.01f);
        PushWeightsUndo("Clean Weights", scene.activeIndex, std::move(before));
        UpdateSkinning(*o);
        ShowStatus("Removed " + std::to_string(n) + " weights under 0.01");
    }
    ImGui::TextDisabled("Blue = 0, red = 1. Shift smooths, Ctrl subtracts.");
}
