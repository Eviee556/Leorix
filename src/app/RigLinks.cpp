// Armature panel section for meshes deformed by another object's bones and for control rigs, plus
// the viewport drawing and picking of control widgets. The logic lives in anim/ArmatureLink.
#include "app/App.h"
#include "anim/ArmatureLink.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>

namespace {
const ImVec4 kBrass(0.79f, 0.54f, 0.29f, 1.0f);
struct LinkState { MeshObject* obj; Armature arm; uint32_t link; };
float SegmentDistance(ImVec2 p, ImVec2 a, ImVec2 b) {
    float dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
    float t = l2 > 1e-6f ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / l2, 0.0f, 1.0f) : 0.0f;
    float ex = a.x + dx * t - p.x, ey = a.y + dy * t - p.y;
    return std::sqrt(ex * ex + ey * ey);
}
}  // namespace

void App::ArmatureLinkUI(MeshObject& o) {
    // One undo step for a set of objects whose binding changed.
    auto pushLinks = [this](const char* label, std::vector<LinkState> before) {
        std::vector<LinkState> after;
        for (const LinkState& s : before) after.push_back({s.obj, s.obj->armature, s.obj->armatureObject});
        auto apply = [this](const std::vector<LinkState>& states) {
            for (const LinkState& s : states) {
                if (scene.IndexOf(s.obj) < 0) continue;
                s.obj->armature = s.arm;
                s.obj->armatureObject = s.link;
                UpdateSkinning(*s.obj);
            }
        };
        auto b = std::make_shared<std::vector<LinkState>>(std::move(before));
        auto a = std::make_shared<std::vector<LinkState>>(std::move(after));
        size_t bytes = 0;
        for (const LinkState& s : *a) bytes += s.arm.weights.size() * (sizeof(VertexWeights) + sizeof(Float3)) * 2;
        undoStack.Push(label, [apply, b] { apply(*b); }, [apply, a] { apply(*a); }, bytes);
    };

    ImGui::Spacing(); ImGui::TextColored(kBrass, "LINKED MESHES"); ImGui::Separator();
    if (!o.armature.bones.empty()) {
        int linked = 0, candidates = 0;
        for (const auto& up : scene.objects) {
            if (up.get() == &o) continue;
            if (o.id && up->armatureObject == o.id) ++linked;
            if (up->selected && !up->isControl && !up->isCurve && !up->mesh.positions.empty()) ++candidates;
        }
        ImGui::TextDisabled("%d other mesh%s deformed by these bones", linked, linked == 1 ? "" : "es");
        ImGui::BeginDisabled(candidates == 0);
        if (ImGui::Button("Bind Selected Meshes", ImVec2(-1, 0))) {
            std::vector<LinkState> before;
            for (const auto& up : scene.objects)
                if (up.get() != &o && up->selected && !up->isControl && !up->isCurve && !up->mesh.positions.empty())
                    before.push_back({up.get(), up->armature, up->armatureObject});
            int bound = 0;
            for (const LinkState& s : before) bound += BindToArmatureObject(scene, *s.obj, o) ? 1 : 0;
            pushLinks("Bind Meshes to Armature", std::move(before));
            ShowStatus("Bound " + std::to_string(bound) + " mesh" + (bound == 1 ? "" : "es") + " to " + o.name + " with automatic weights");
        }
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Select other meshes (Shift+click), make this armature active, then bind: each mesh gets its own automatic weights");

        if (ImGui::Button("Add Control Rig", ImVec2(-1, 0))) {
            Armature before = o.armature;
            std::vector<MeshObject*> made = AddControlRig(scene, o);
            if (made.empty()) {
                ShowStatus("Control rigs need a humanoid rig without controls (Add Humanoid Rig first)");
            } else {
                struct Step { std::vector<MeshObject*> objs; std::vector<std::unique_ptr<MeshObject>> held; };
                auto st = std::make_shared<Step>();
                st->objs = made;
                auto bArm = std::make_shared<Armature>(std::move(before));
                auto aArm = std::make_shared<Armature>(o.armature);
                MeshObject* ao = &o;
                undoStack.Push("Add Control Rig",
                    [this, st, bArm, ao] {
                        for (MeshObject* c : st->objs) { int i = scene.IndexOf(c); if (i >= 0) st->held.push_back(scene.Take(i)); }
                        if (scene.IndexOf(ao) >= 0) { ao->armature = *bArm; UpdateSkinning(*ao); }
                    },
                    [this, st, aArm, ao] {
                        for (auto& c : st->held) scene.Insert((int)scene.objects.size(), std::move(c));
                        st->held.clear();
                        if (scene.IndexOf(ao) >= 0) { ao->armature = *aArm; UpdateSkinning(*ao); }
                    },
                    made.size() * 64 * sizeof(Float3));
                ShowStatus("Added " + std::to_string(made.size()) + " controls: move them in Object Mode to pose the rig");
            }
        }
        ImGui::SetItemTooltip("Hand and foot IK controls with elbow and knee poles, plus hip and chest controls");
        return;
    }

    // A mesh without bones of its own: pick the armature object that deforms it.
    const MeshObject* src = BoneSource(scene, o);
    if (ImGui::BeginCombo("Deformed by", src ? src->name.c_str() : "(none)")) {
        if (ImGui::Selectable("(none)", !src) && o.armatureObject) {
            std::vector<LinkState> before{{&o, o.armature, o.armatureObject}};
            o.armatureObject = 0;
            o.armature.weights.clear(); o.armature.restPositions.clear();
            UpdateSkinning(o);
            pushLinks("Unbind from Armature", std::move(before));
        }
        for (const auto& up : scene.objects) {
            if (up.get() == &o || up->armature.bones.empty()) continue;
            ImGui::PushID(up.get());
            if (ImGui::Selectable(up->name.c_str(), src == up.get())) {
                std::vector<LinkState> before{{&o, o.armature, o.armatureObject}};
                if (BindToArmatureObject(scene, o, *up)) {
                    pushLinks("Bind Mesh to Armature", std::move(before));
                    ShowStatus("Bound " + o.name + " to " + up->name + " with automatic weights");
                }
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Another object's bones deform this mesh; it keeps its own weights (Weight Paint edits them)");
}

void App::DrawControls(ImDrawList* dl, ImVec2 mouse, bool hovered, int& pickedObject) {
    int best = -1;
    float bestDist = 7.0f;   // pixels: wires are thin, so picking is by distance to a segment
    for (int i = 0; i < (int)scene.objects.size(); ++i) {
        const MeshObject& c = *scene.objects[i];
        if (!c.isControl || !c.visible) continue;
        ImU32 col = c.selected ? IM_COL32(255, 170, 60, 255) : IM_COL32(90, 220, 255, 230);
        for (size_t k = 0; k + 1 < c.mesh.positions.size(); k += 2) {
            float za, zb; ImVec2 a, b;
            if (!viewport.ProjectPoint(c.transform.Apply(c.mesh.positions[k]), za, a) ||
                !viewport.ProjectPoint(c.transform.Apply(c.mesh.positions[k + 1]), zb, b)) continue;
            dl->AddLine(a, b, col, c.selected ? 2.5f : 1.6f);
            if (!hovered) continue;
            float d = SegmentDistance(mouse, a, b);
            if (d < bestDist) { bestDist = d; best = i; }
        }
    }
    // Controls sit on top of the body, so a click near a wire selects the control, as in Blender.
    if (best >= 0 && mode3D == Mode3D::Object) pickedObject = best;
}
