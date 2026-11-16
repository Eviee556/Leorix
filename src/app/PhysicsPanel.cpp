// Physics panel: the active object's rigid body and cloth settings, and baking the simulation for
// the whole scene. The solver lives in physics/Physics.*; this only edits settings and records undo.
#include "app/App.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace {
const ImVec4 kBrass(0.79f, 0.54f, 0.29f, 1.0f);

// Every object's physics and transform (plus the mesh of cloth objects, which a cloth bake moves),
// so a bake, a free or a settings edit undoes as one step.
struct PhysEntry {
    MeshObject* obj;
    RigidBody rigidBody;
    Cloth cloth;
    Transform transform;
    std::vector<Float3> positions;   // cloth objects only
};
using PhysSnap = std::vector<PhysEntry>;
PhysSnap Snapshot(Scene& scene) {
    PhysSnap s;
    for (auto& o : scene.objects) {
        bool cloth = o->cloth.enabled || o->cloth.Baked();
        s.push_back({o.get(), o->rigidBody, o->cloth, o->transform, cloth ? o->mesh.positions : std::vector<Float3>{}});
    }
    return s;
}
size_t SnapBytes(const PhysSnap& s) {
    size_t b = 0;
    for (const auto& e : s) {
        b += sizeof(e) + e.rigidBody.cache.size() * sizeof(RigidTransform) + (e.positions.size() + e.cloth.rest.size()) * sizeof(Float3);
        for (const auto& f : e.cloth.cache) b += f.size() * sizeof(Float3);
    }
    return b;
}
}  // namespace

void App::PhysicsPanel() {
    if (!ImGui::Begin("Physics", &showPhysics)) { ImGui::End(); return; }
    auto restore = [this](const PhysSnap& s) {
        for (const auto& e : s) {
            if (scene.IndexOf(e.obj) < 0) continue;   // the object was deleted since
            e.obj->rigidBody = e.rigidBody;
            e.obj->cloth = e.cloth;
            e.obj->transform = e.transform;
            if (!e.positions.empty() && e.positions.size() == e.obj->mesh.positions.size()) {
                e.obj->mesh.positions = e.positions;
                e.obj->mesh.ComputeNormals();
                e.obj->MarkTopoDirty();
                e.obj->stackDirty = true;
            }
        }
    };
    auto record = [&](const char* label, PhysSnap before) {
        auto a = std::make_shared<PhysSnap>(std::move(before));
        auto b = std::make_shared<PhysSnap>(Snapshot(scene));
        undoStack.Push(label, [restore, a] { restore(*a); }, [restore, b] { restore(*b); }, SnapBytes(*a) + SnapBytes(*b));
    };

    MeshObject* o = scene.Active();
    // Drags record one undo step when released; clicks and combos record at once.
    static PhysSnap dragBefore;
    auto track = [&](const char* label) {
        if (ImGui::IsItemActivated()) dragBefore = Snapshot(scene);
        if (ImGui::IsItemDeactivatedAfterEdit()) record(label, std::move(dragBefore));
    };
    ImGui::TextColored(kBrass, "RIGID BODY"); ImGui::Separator();
    if (!o) {
        ImGui::TextDisabled("Select an object to give it physics.");
    } else {
        RigidBody& rb = o->rigidBody;
        bool en = rb.enabled;
        if (ImGui::Checkbox("Rigid Body", &en)) { PhysSnap b = Snapshot(scene); rb.enabled = en; record(en ? "Add Rigid Body" : "Remove Rigid Body", std::move(b)); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Let this object fall, collide and bounce when you bake. Cloth also collides with it.");
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

        ImGui::Spacing(); ImGui::TextColored(kBrass, "CLOTH"); ImGui::Separator();
        Cloth& cl = o->cloth;
        bool ce = cl.enabled;
        if (ImGui::Checkbox("Cloth", &ce)) { PhysSnap b = Snapshot(scene); cl.enabled = ce; record(ce ? "Add Cloth" : "Remove Cloth", std::move(b)); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Make this mesh hang, drape and fold like fabric when you bake");
        if (cl.enabled) {
            ImGui::DragFloat("Cloth Mass (kg)", &cl.mass, 0.01f, 0.001f, 1e6f, "%.3f", ImGuiSliderFlags_AlwaysClamp); track("Cloth Mass");
            ImGui::SliderFloat("Stretch Stiffness", &cl.stretch, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp); track("Cloth Stretch");
            ImGui::SliderFloat("Bend Stiffness", &cl.bend, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp); track("Cloth Bend");
            ImGui::SliderFloat("Damping", &cl.damping, 0.0f, 0.99f, "%.2f", ImGuiSliderFlags_AlwaysClamp); track("Cloth Damping");
            ImGui::SliderFloat("Cloth Friction", &cl.friction, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp); track("Cloth Friction");
            ImGui::DragFloat("Thickness (m)", &cl.thickness, 0.001f, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp); track("Cloth Thickness");
            ImGui::Text("Pinned vertices: %d", (int)cl.pins.size());
            if (ImGui::Button("Pin Top Edge")) { PhysSnap b = Snapshot(scene); int n = PinClothTop(*o); record("Pin Cloth", std::move(b)); ShowStatus("Pinned " + std::to_string(n) + " vertices"); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hold the highest vertices in place, like a curtain on its rail");
            ImGui::SameLine();
            if (ImGui::Button("Pin Selected")) {
                PhysSnap b = Snapshot(scene);
                cl.pins.assign(o->selVerts.begin(), o->selVerts.end());
                std::sort(cl.pins.begin(), cl.pins.end());
                record("Pin Cloth", std::move(b));
                ShowStatus(cl.pins.empty() ? "Select vertices in Edit Mode first" : "Pinned " + std::to_string(cl.pins.size()) + " vertices");
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hold the vertices selected in Edit Mode in place");
            ImGui::SameLine();
            if (ImGui::Button("Unpin")) { PhysSnap b = Snapshot(scene); cl.pins.clear(); record("Unpin Cloth", std::move(b)); }
            if (cl.Baked()) {
                ImGui::TextDisabled("Baked: frames %d - %d", cl.cacheStart, cl.cacheStart + (int)cl.cache.size() - 1);
                if (ImGui::Button("Apply as Shape")) {
                    PhysSnap b = Snapshot(scene);
                    DropClothBake(*o, true);
                    record("Apply Cloth Shape", std::move(b));
                    ShowStatus("Kept the cloth's current shape");
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Keep the mesh as it looks on this frame and drop the cloth bake");
            }
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
        int c = BakeCloth(scene, physics);   // after the rigid bodies, so cloth follows their paths
        if (n + c > 0) {
            ApplyRigidBodyCache(scene, playback.frame);
            record("Bake Physics", std::move(before));
            std::string what = std::to_string(n) + (n == 1 ? " moving object" : " moving objects");
            if (c) what += " and " + std::to_string(c) + (c == 1 ? " cloth" : " cloths");
            ShowStatus("Baked " + what + ", frames " + std::to_string(physics.frameStart) + "-" + std::to_string(physics.frameEnd));
        } else {
            restore(before);
            ShowStatus("Nothing to bake: give at least one object an Active rigid body or Cloth");
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Simulate every rigid body and cloth; play or scrub the timeline to watch it");
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
