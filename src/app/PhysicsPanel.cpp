// Physics panel: the active object's rigid body settings, and baking the simulation for the
// whole scene. The solver lives in physics/Physics.*; this only edits settings and records undo.
#include "app/App.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace {
const ImVec4 kBrass(0.79f, 0.54f, 0.29f, 1.0f);

// Every object's rigid body and transform, so a bake, a free or a settings edit undoes as one step.
using PhysSnap = std::vector<std::pair<MeshObject*, std::pair<RigidBody, Transform>>>;
PhysSnap Snapshot(Scene& scene) {
    PhysSnap s;
    for (auto& o : scene.objects) s.push_back({o.get(), {o->rigidBody, o->transform}});
    return s;
}
size_t SnapBytes(const PhysSnap& s) {
    size_t b = 0;
    for (const auto& e : s) b += sizeof(e) + e.second.first.cache.size() * sizeof(RigidTransform);
    return b;
}
}  // namespace

void App::PhysicsPanel() {
    if (!ImGui::Begin("Physics", &showPhysics)) { ImGui::End(); return; }
    auto restore = [this](const PhysSnap& s) {
        for (const auto& e : s) {
            if (scene.IndexOf(e.first) < 0) continue;   // the object was deleted since
            e.first->rigidBody = e.second.first;
            e.first->transform = e.second.second;
        }
    };
    auto record = [&](const char* label, PhysSnap before) {
        auto a = std::make_shared<PhysSnap>(std::move(before));
        auto b = std::make_shared<PhysSnap>(Snapshot(scene));
        undoStack.Push(label, [restore, a] { restore(*a); }, [restore, b] { restore(*b); }, SnapBytes(*a) + SnapBytes(*b));
    };

    MeshObject* o = scene.Active();
    ImGui::TextColored(kBrass, "RIGID BODY"); ImGui::Separator();
    if (!o) {
        ImGui::TextDisabled("Select an object to give it physics.");
    } else {
        RigidBody& rb = o->rigidBody;
        // Drags record one undo step when released; clicks and combos record at once.
        static PhysSnap dragBefore;
        auto track = [&](const char* label) {
            if (ImGui::IsItemActivated()) dragBefore = Snapshot(scene);
            if (ImGui::IsItemDeactivatedAfterEdit()) record(label, std::move(dragBefore));
        };
        bool en = rb.enabled;
        if (ImGui::Checkbox("Rigid Body", &en)) { PhysSnap b = Snapshot(scene); rb.enabled = en; record(en ? "Add Rigid Body" : "Remove Rigid Body", std::move(b)); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Let this object fall, collide and bounce when you bake");
        if (rb.enabled) {
            int type = rb.passive ? 1 : 0;
            const char* types[] = {"Active (falls and moves)", "Passive (floor or wall)"};
            ImGui::SetNextItemWidth(-1);
            if (ImGui::Combo("##rbtype", &type, types, 2)) { PhysSnap b = Snapshot(scene); rb.passive = type == 1; record("Rigid Body Type", std::move(b)); }
            int shape = (int)rb.shape;
            const char* shapes[] = {RigidShapeName(RigidShape::Box), RigidShapeName(RigidShape::Sphere), RigidShapeName(RigidShape::ConvexHull)};
            if (ImGui::Combo("Shape", &shape, shapes, 3)) { PhysSnap b = Snapshot(scene); rb.shape = (RigidShape)shape; record("Rigid Body Shape", std::move(b)); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Box and Sphere fit the object's bounds; Convex Hull wraps its outer points");
            ImGui::BeginDisabled(rb.passive);
            ImGui::DragFloat("Mass (kg)", &rb.mass, 0.05f, 0.001f, 1e6f, "%.3f", ImGuiSliderFlags_AlwaysClamp); track("Rigid Body Mass");
            ImGui::EndDisabled();
            ImGui::SliderFloat("Friction", &rb.friction, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp); track("Rigid Body Friction");
            ImGui::SliderFloat("Bounciness", &rb.bounciness, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp); track("Rigid Body Bounciness");
            if (rb.Baked())
                ImGui::TextDisabled("Baked: frames %d - %d", rb.cacheStart, rb.cacheStart + (int)rb.cache.size() - 1);
        }
    }

    ImGui::Spacing(); ImGui::TextColored(kBrass, "SIMULATION (whole scene)"); ImGui::Separator();
    ImGui::DragIntRange2("Frames", &physics.frameStart, &physics.frameEnd, 1, 1, kMaxBakeFrames);
    physics.frameEnd = std::max(physics.frameEnd, physics.frameStart);
    ImGui::DragFloat3("Gravity", &physics.gravity.x, 0.05f, -100, 100, "%.2f");
    physics.fps = playback.fps;
    if (ImGui::Button("Bake")) {
        PhysSnap before = Snapshot(scene);
        int n = BakeRigidBodies(scene, physics);
        if (n > 0) {
            ApplyRigidBodyCache(scene, playback.frame);
            record("Bake Physics", std::move(before));
            ShowStatus("Baked " + std::to_string(n) + (n == 1 ? " moving object" : " moving objects") + ", frames " +
                       std::to_string(physics.frameStart) + "-" + std::to_string(physics.frameEnd));
        } else {
            restore(before);
            ShowStatus("Nothing to bake: give at least one object an Active rigid body");
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Simulate every rigid body; play or scrub the timeline to watch it");
    ImGui::SameLine();
    if (ImGui::Button("Free Bake")) {
        PhysSnap before = Snapshot(scene);
        FreeRigidBodyBake(scene);
        record("Free Physics Bake", std::move(before));
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Forget the simulation and put objects back where they started");
    if (ImGui::Button("Apply Transform at Current Frame")) {
        PhysSnap before = Snapshot(scene);
        int n = 0;
        for (auto& ob : scene.objects)
            if (ob->selected && ob->rigidBody.Baked()) { ob->rigidBody.cache.clear(); ++n; }   // keeps where it is now
        if (n) record("Apply Physics Transform", std::move(before));
        ShowStatus(n ? "Kept the simulated pose of " + std::to_string(n) + " object(s)" : "Select a baked object first");
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Keep the selected objects where the simulation has them now, and drop their bake");
    ImGui::End();
}
