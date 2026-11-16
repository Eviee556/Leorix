// Weight Paint mode: brush strokes, the sample and gradient tools, bone locks, whole-mesh weight
// operations, transfer, and undo. The weight math lives in anim/WeightPaint so the self-test can
// check it without a window.
#include "app/App.h"
#include "anim/ArmatureLink.h"
#include "imgui.h"
#include <algorithm>
#include <cctype>
#include <cmath>

namespace {
// The surface the brush is measured on: skinned when posed, so it matches what is picked.
const std::vector<Float3>& BrushPositions(const MeshObject& o) {
    return o.armature.enabled && o.skinned.size() == o.mesh.positions.size() ? o.skinned : o.mesh.positions;
}

// A linked mesh has no bones of its own: weight functions run on a stand-in armature that borrows
// the bone source's bones (names for mirroring, locks) and this mesh's weights, swapped back after.
struct BorrowedBones {
    MeshObject& o;
    Armature    stand;
    Armature*   arm;
    BorrowedBones(MeshObject& obj, const MeshObject* src) : o(obj), arm(&obj.armature) {
        if (!src || src == &obj) return;
        stand.bones = src->armature.bones;
        stand.weights.swap(o.armature.weights);
        stand.restPositions.swap(o.armature.restPositions);
        arm = &stand;
    }
    ~BorrowedBones() {
        if (arm != &stand) return;
        o.armature.weights.swap(stand.weights);
        o.armature.restPositions.swap(stand.restPositions);
    }
};

bool Contains(const std::string& name, const char* filter) {
    if (!filter[0]) return true;
    std::string a = name, b = filter;
    for (char& c : a) c = (char)std::tolower((unsigned char)c);
    for (char& c : b) c = (char)std::tolower((unsigned char)c);
    return a.find(b) != std::string::npos;
}
}   // namespace

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

// Runs a whole-mesh weight operation on the active object as one undo step.
void App::WeightOp(const char* label, const std::function<size_t(Armature&)>& op) {
    MeshObject* o = scene.Active();
    if (!o || !o->armature.Bound()) return;
    auto before = o->armature.weights;
    size_t n;
    {
        BorrowedBones bb(*o, BoneSource(scene, *o));
        n = op(*bb.arm);
    }
    if (n == 0) { ShowStatus(std::string(label) + ": nothing changed"); return; }
    PushWeightsUndo(label, scene.activeIndex, std::move(before));
    UpdateSkinning(*o);
    ShowStatus(std::string(label) + ": " + std::to_string(n) + " vertices");
}

void App::WeightStroke(bool begin) {
    MeshObject* o = scene.Active();
    if (!o || !lastPick.hit || lastPickObject != scene.activeIndex) return;
    Armature& arm = o->armature;
    const MeshObject* src = BoneSource(scene, *o);   // own bones, or the armature object it is bound to
    const int boneCount = src ? (int)src->armature.bones.size() : 0;
    if (!arm.Bound() || arm.weights.size() != o->mesh.positions.size()) {
        if (begin) ShowStatus("Weight Paint needs a mesh parented to an armature (Armature panel > Parent)");
        return;
    }
    if (activeBone < 0 || activeBone >= boneCount) {
        if (begin) ShowStatus("Pick a bone to paint in the Weight Paint settings");
        return;
    }
    if (src->armature.bones[activeBone].weightLocked) {
        if (begin) ShowStatus(src->armature.bones[activeBone].name + " is locked; unlock it in the bone list to paint it");
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
    dab.mode = (WeightBrush)weightPaint.mode;
    if (ImGui::GetIO().KeyShift) dab.mode = WeightBrush::Smooth;
    else if (ImGui::GetIO().KeyCtrl && dab.mode == WeightBrush::Add) dab.mode = WeightBrush::Subtract;   // Ctrl inverts
    else if (ImGui::GetIO().KeyCtrl && dab.mode == WeightBrush::Subtract) dab.mode = WeightBrush::Add;
    dab.bone = activeBone;
    dab.mirrorX = weightPaint.mirrorX;
    dab.autoNormalize = weightPaint.autoNormalize;
    dab.frontOnly = weightPaint.frontOnly;
    const Float3 eyeLocal = o->transform.InverseApply({eye.x, eye.y, eye.z});
    dab.viewDir = {dab.center.x - eyeLocal.x, dab.center.y - eyeLocal.y, dab.center.z - eyeLocal.z};
    dab.strokeStart = weightPaint.accumulate ? nullptr : &weightBefore;
    bool changed;
    {
        BorrowedBones bb(*o, src);
        changed = ApplyWeightDab(*bb.arm, BrushPositions(*o), o->mesh.tris, weightPaint.brush, dab, &o->mesh.positions);
    }
    if (changed) UpdateSkinning(*o);   // re-skins per dab, so a posed rig deforms live while painting
}

void App::WeightStrokeEnd() {
    weightPainting = false;
    if (weightBefore.empty()) return;
    PushWeightsUndo("Weight Paint", weightUndoIndex, std::move(weightBefore));
    weightBefore.clear();
}

// Viewport input in Weight Paint mode: brush strokes, F / Shift+F radius and strength, Ctrl+click
// a bone, the sample and gradient tools, and the weight readout under the cursor.
void App::WeightViewportInput(ImDrawList* dl, ImVec2 mouse) {
    ImGuiIO& io = ImGui::GetIO();
    MeshObject* o = scene.Active();
    const MeshObject* src = o ? BoneSource(scene, *o) : nullptr;
    const bool onActive = o && lastPick.hit && lastPickObject == scene.activeIndex;

    // F / Shift+F: drag sideways to size the brush or set its strength; click or Enter keeps it,
    // right-click or Esc cancels.
    if (!weightAdjust && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F, false)) {
        weightAdjust = io.KeyShift ? 2 : 1;
        weightAdjustStart = weightAdjust == 1 ? weightPaint.brush.radiusPx : weightPaint.brush.strength;
        weightAdjustMouse = mouse.x;
    }
    if (weightAdjust) {
        const float dx = mouse.x - weightAdjustMouse;
        if (weightAdjust == 1) weightPaint.brush.radiusPx = std::clamp(weightAdjustStart + dx, 2.0f, 400.0f);
        else weightPaint.brush.strength = std::clamp(weightAdjustStart + dx / 300.0f, 0.0f, 1.0f);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            if (weightAdjust == 1) weightPaint.brush.radiusPx = weightAdjustStart; else weightPaint.brush.strength = weightAdjustStart;
            weightAdjust = 0;
        } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            weightAdjust = 0;
        }
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
        dl->AddCircle(mouse, weightPaint.brush.radiusPx, IM_COL32(230, 180, 90, 230), 64, 1.6f);
        char t[48];
        std::snprintf(t, sizeof t, weightAdjust == 1 ? "Radius %.0f px" : "Strength %.2f", weightAdjust == 1 ? weightPaint.brush.radiusPx : weightPaint.brush.strength);
        dl->AddText(ImVec2(mouse.x + 12, mouse.y - 22), IM_COL32(240, 240, 240, 255), t);
        return;
    }

    // Ctrl+click near a bone makes it the active one (otherwise Ctrl+click paints inverted).
    if (src && io.KeyCtrl && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        int b = PickArmatureBone(viewport, *src, mouse, 8.0f);
        if (b >= 0) { activeBone = b; ShowStatus("Painting " + src->armature.bones[b].name); return; }
    }
    // Sample Weight: the tool, or Ctrl+right-click with any tool.
    const bool sampleClick = (weightPaint.tool == 1 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) ||
                             (io.KeyCtrl && ImGui::IsMouseClicked(ImGuiMouseButton_Right));
    if (sampleClick && onActive && activeBone >= 0 && o->armature.Bound()) {
        weightPaint.value = SampleWeight(o->armature.weights, BrushPositions(*o), o->mesh.tris, lastPick.tri,
                                         o->transform.InverseApply(lastPick.point), activeBone);
        char t[48];
        std::snprintf(t, sizeof t, "Sampled weight %.3f", weightPaint.value);
        ShowStatus(t);
        return;
    }

    if (weightPaint.tool == 2) {   // Gradient: drag from where the weight is full to where it fades out
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && onActive) {
            weightGradientDrag = true;
            weightGradientA = o->transform.InverseApply(lastPick.point);
            weightGradientScreen = mouse;
        }
        if (weightGradientDrag) {
            dl->AddLine(weightGradientScreen, mouse, IM_COL32(240, 200, 90, 230), 2.0f);
            dl->AddCircleFilled(weightGradientScreen, 4.0f, IM_COL32(240, 80, 60, 255));
            if (weightPaint.gradientRadial) {
                const float r = std::hypot(mouse.x - weightGradientScreen.x, mouse.y - weightGradientScreen.y);
                dl->AddCircle(weightGradientScreen, r, IM_COL32(240, 200, 90, 160), 64, 1.2f);
            }
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                weightGradientDrag = false;
                if (onActive && activeBone >= 0 && o->armature.Bound()) {
                    WeightGradient g;
                    g.a = weightGradientA;
                    g.b = o->transform.InverseApply(lastPick.point);
                    g.radial = weightPaint.gradientRadial;
                    g.value = weightPaint.value;
                    g.strength = weightPaint.brush.strength;
                    g.bone = activeBone;
                    g.autoNormalize = weightPaint.autoNormalize;
                    const std::vector<Float3> pos = BrushPositions(*o);
                    WeightOp("Weight Gradient", [&](Armature& a) { return ApplyWeightGradient(a, pos, weightPaint.brush, g); });
                } else {
                    ShowStatus("End the gradient on the mesh");
                }
            }
        }
    } else if (weightPaint.tool == 0) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && lastPick.hit) WeightStroke(true);
        else if (weightPainting && ImGui::IsMouseDown(ImGuiMouseButton_Left)) WeightStroke(false);
    }

    ImGui::SetMouseCursor(weightPaint.tool == 0 ? ImGuiMouseCursor_None : ImGuiMouseCursor_Hand);
    if (weightPaint.tool == 0)
        dl->AddCircle(mouse, weightPaint.brush.radiusPx, lastPick.hit ? IM_COL32(230, 180, 90, 230) : IM_COL32(150, 158, 175, 120), 64, 1.6f);

    // Weight under the cursor, in the status bar (only when it changes, so other messages still show).
    if (onActive && src && activeBone >= 0 && activeBone < (int)src->armature.bones.size() && o->armature.Bound()) {
        float w = SampleWeight(o->armature.weights, BrushPositions(*o), o->mesh.tris, lastPick.tri,
                               o->transform.InverseApply(lastPick.point), activeBone);
        if (std::fabs(w - weightReadout) > 0.005f) {
            weightReadout = w;
            char t[160];
            std::snprintf(t, sizeof t, "%s weight %.3f", src->armature.bones[activeBone].name.c_str(), w);
            ShowStatus(t);
        }
    }
}

void App::WeightSettingsUI() {
    MeshObject* o = scene.Active();
    if (!o || !o->armature.Bound()) {
        ImGui::TextWrapped("Weight Paint edits how strongly each bone moves the mesh. Add an armature and parent the mesh "
                           "to it (Armature panel) first; automatic weights are the starting point.");
        return;
    }
    Armature& arm = o->armature;
    // Locks live on the bones, which a linked mesh borrows from its armature object.
    const MeshObject* srcc = BoneSource(scene, *o);
    MeshObject* src = srcc ? scene.FindById(srcc->id) : nullptr;
    if (!src) src = o;
    std::vector<Bone>& bones = src->armature.bones;

    static const char* kTools[] = {"Brush", "Sample Weight", "Gradient"};
    ImGui::Combo("Tool", &weightPaint.tool, kTools, 3);
    if (weightPaint.tool == 0) {
        static const char* kModes[] = {"Add", "Subtract", "Replace", "Smooth", "Blur", "Average", "Sharpen", "Multiply"};
        ImGui::Combo("Brush", &weightPaint.mode, kModes, 8);
    } else if (weightPaint.tool == 2) {
        ImGui::Checkbox("Radial", &weightPaint.gradientRadial);
        ImGui::SetItemTooltip("Drag from where the weight is full to where it fades to zero");
    }
    ImGui::SliderFloat(weightPaint.mode == 7 && weightPaint.tool == 0 ? "Factor" : "Weight", &weightPaint.value, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Radius", &weightPaint.brush.radiusPx, 2.0f, 400.0f, "%.0f px");
    ImGui::SliderFloat("Strength", &weightPaint.brush.strength, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Hardness", &weightPaint.brush.hardness, 0.0f, 0.99f, "%.2f");
    static const char* kCurves[] = {"Smooth", "Smoother", "Sphere", "Root", "Sharp", "Linear", "Sharper", "Inverse Square", "Constant"};
    ImGui::Combo("Falloff", &weightPaint.brush.falloffCurve, kCurves, 9);
    ImGui::Checkbox("X Mirror", &weightPaint.mirrorX);
    ImGui::SetItemTooltip("Also paints the other side, on the matching .L/.R bone");
    ImGui::SameLine();
    ImGui::Checkbox("Accumulate", &weightPaint.accumulate);
    ImGui::SetItemTooltip("Off: one stroke can't push a vertex further than a single pass of the brush");
    ImGui::Checkbox("Front Faces Only", &weightPaint.frontOnly);
    ImGui::SameLine();
    ImGui::Checkbox("Auto Normalize", &weightPaint.autoNormalize);
    ImGui::SetItemTooltip("Keep every vertex's weights adding up to 1 (locked bones stay as they are)");
    ImGui::Checkbox("Zero Weights Black", &weightPaint.zeroBlack);
    ImGui::SameLine();
    ImGui::Checkbox("Contours", &weightPaint.contours);

    if (ImGui::CollapsingHeader("Bones", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::InputTextWithHint("##bonefilter", "Search bones", weightPaint.boneFilter, sizeof weightPaint.boneFilter);
        const std::vector<int> counts = BoneInfluenceCounts(arm, bones.size());
        if (ImGui::BeginChild("##wbones", ImVec2(0, std::min(220.0f, 24.0f * (float)bones.size() + 8)), ImGuiChildFlags_Borders)) {
            for (int i = 0; i < (int)bones.size(); ++i) {
                if (!Contains(bones[i].name, weightPaint.boneFilter)) continue;
                ImGui::PushID(i);
                bool locked = bones[i].weightLocked;
                if (ImGui::Checkbox("##lock", &locked)) {
                    const uint32_t id = src->id;
                    const bool was = !locked;
                    undoStack.Push(locked ? "Lock Bone Weights" : "Unlock Bone Weights",
                        [this, id, i, was] { if (MeshObject* s = scene.FindById(id); s && i < (int)s->armature.bones.size()) s->armature.bones[i].weightLocked = was; },
                        [this, id, i, locked] { if (MeshObject* s = scene.FindById(id); s && i < (int)s->armature.bones.size()) s->armature.bones[i].weightLocked = locked; }, 16);
                    bones[i].weightLocked = locked;
                }
                ImGui::SetItemTooltip("Lock: this bone's weights never change");
                ImGui::SameLine();
                char label[160];
                std::snprintf(label, sizeof label, "%s%s  (%d)", bones[i].name.c_str(), bones[i].weightLocked ? " [locked]" : "",
                              i < (int)counts.size() ? counts[i] : 0);
                if (ImGui::Selectable(label, activeBone == i)) activeBone = i;
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
        ImGui::TextDisabled("Ctrl+click a bone in the viewport to pick it.");
    }

    if (ImGui::CollapsingHeader("Weight Tools")) {
        ImGui::Checkbox("All Bones", &weightPaint.allBones);
        ImGui::SetItemTooltip("Off: the operations below work on the active bone only");
        const bool all = weightPaint.allBones;
        const int bone = all ? -1 : activeBone;
        const bool norm = weightPaint.autoNormalize;
        const bool ready = all || (activeBone >= 0 && activeBone < (int)bones.size());
        ImGui::BeginDisabled(!ready);
        if (ImGui::Button("Invert")) WeightOp("Invert Weights", [&](Armature& a) { return InvertWeights(a, bone, norm); });
        ImGui::SameLine();
        if (ImGui::Button("Clear")) WeightOp("Clear Bone Weights", [&](Armature& a) { return ClearBoneWeights(a, bone); });
        ImGui::SameLine();
        if (ImGui::Button("Mirror L > R")) WeightOp("Mirror Weights", [&](Armature& a) { return MirrorWeights(a, o->mesh.positions, bone); });

        ImGui::DragFloat("Offset", &weightPaint.levelsOffset, 0.01f, -1.0f, 1.0f, "%.2f");
        ImGui::DragFloat("Gain", &weightPaint.levelsGain, 0.01f, 0.0f, 10.0f, "%.2f");
        if (ImGui::Button("Levels"))
            WeightOp("Weight Levels", [&](Armature& a) { return LevelsWeights(a, bone, weightPaint.levelsOffset, weightPaint.levelsGain, norm); });

        ImGui::DragFloatRange2("Range", &weightPaint.clampLo, &weightPaint.clampHi, 0.01f, 0.0f, 1.0f, "%.2f");
        if (ImGui::Button("Clamp"))
            WeightOp("Clamp Weights", [&](Armature& a) { return ClampWeights(a, bone, weightPaint.clampLo, weightPaint.clampHi, norm); });

        ImGui::SliderInt("Iterations", &weightPaint.smoothIter, 1, 20);
        ImGui::SliderFloat("Factor##smooth", &weightPaint.smoothFactor, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Expand", &weightPaint.smoothExpand, -1.0f, 1.0f, "%.2f");
        if (ImGui::Button("Smooth"))
            WeightOp("Smooth Weights", [&](Armature& a) {
                return SmoothWeights(a, o->mesh.tris, bone, weightPaint.smoothIter, weightPaint.smoothFactor, weightPaint.smoothExpand, norm);
            });

        ImGui::SliderInt("Steps", &weightPaint.quantizeSteps, 1, 20);
        if (ImGui::Button("Quantize"))
            WeightOp("Quantize Weights", [&](Armature& a) { return QuantizeWeights(a, bone, weightPaint.quantizeSteps, norm); });
        ImGui::EndDisabled();

        ImGui::SliderInt("Max Influences", &weightPaint.limitTotal, 1, 4);
        if (ImGui::Button("Limit Total")) WeightOp("Limit Total Weights", [&](Armature& a) { return LimitTotalWeights(a, weightPaint.limitTotal); });
        ImGui::SameLine();
        if (ImGui::Button("Normalize All")) WeightOp("Normalize All Weights", [](Armature& a) { return NormalizeAllWeights(a); });
        ImGui::SameLine();
        if (ImGui::Button("Clean")) WeightOp("Clean Weights", [](Armature& a) { return CleanWeights(a, 0.01f); });

        ImGui::SliderFloat("Assign Value", &weightPaint.assignValue, 0.0f, 1.0f, "%.2f");
        ImGui::BeginDisabled(activeBone < 0 || o->selVerts.empty());
        if (ImGui::Button("Assign to Selected")) {
            std::vector<uint32_t> verts(o->selVerts.begin(), o->selVerts.end());
            WeightOp("Assign Weights", [&](Armature& a) { return AssignWeights(a, verts, activeBone, weightPaint.assignValue, norm); });
        }
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Vertices selected in Edit Mode get this weight on the active bone");
    }

    if (ImGui::CollapsingHeader("Transfer Weights")) {
        // Sources: other meshes deformed by the same bones.
        MeshObject* chosen = weightPaint.transferSource > 0 ? scene.FindById((uint32_t)weightPaint.transferSource) : nullptr;
        if (chosen && (chosen == o || !chosen->armature.Bound() || BoneSource(scene, *chosen) != srcc)) chosen = nullptr;
        if (ImGui::BeginCombo("From", chosen ? chosen->name.c_str() : "(pick a mesh)")) {
            for (auto& so : scene.objects) {
                if (so.get() == o || !so->armature.Bound() || so->isControl || BoneSource(scene, *so) != srcc) continue;
                if (ImGui::Selectable(so->name.c_str(), so.get() == chosen)) weightPaint.transferSource = (int)so->id;
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip("Copies the weights of the nearest point on another mesh bound to the same armature");
        ImGui::BeginDisabled(!chosen);
        if (ImGui::Button("Transfer") && chosen) {
            // Both meshes in world space, so objects placed apart still line up as they look.
            std::vector<Float3> dp(o->mesh.positions.size()), sp(chosen->mesh.positions.size());
            for (size_t i = 0; i < dp.size(); ++i) dp[i] = o->transform.Apply(o->mesh.positions[i]);
            for (size_t i = 0; i < sp.size(); ++i) sp[i] = chosen->transform.Apply(chosen->mesh.positions[i]);
            const std::vector<VertexWeights> sw = chosen->armature.weights;
            const std::vector<Tri> st = chosen->mesh.tris;
            WeightOp("Transfer Weights", [&](Armature& a) { return TransferWeights(a, dp, sw, sp, st); });
        }
        ImGui::EndDisabled();
    }
    ImGui::TextDisabled("Blue = 0, red = 1. Shift smooths, Ctrl inverts, F / Shift+F size and strength, Ctrl+right-click samples.");
}
