#include "app/RigPanel.h"
#include "imgui.h"
#include <algorithm>
#include <cfloat>

static const ImVec4 kBrass(0.79f, 0.54f, 0.29f, 1.0f);
static constexpr float kDeg = 57.2957795f;

std::string RigPresetUI(const Armature& arm, const Mesh& mesh,
                        const std::function<void(const char*, const std::function<void(Armature&)>&)>& edit) {
    static int naming = 0;
    std::string status;
    ImGui::Spacing(); ImGui::TextColored(kBrass, "RIG PRESETS"); ImGui::Separator();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##naming", RigNamingName((RigNaming)naming))) {
        for (int i = 0; i < (int)RigNaming::Count; ++i)
            if (ImGui::Selectable(RigNamingName((RigNaming)i), naming == i)) naming = i;
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Bone naming convention for the humanoid rig and for Rename Bones");
    if (ImGui::Button("Add Humanoid Rig")) {
        Float3 mn{-0.5f, 0, -0.25f}, mx{0.5f, 2, 0.25f};   // a 2 m figure when there is no mesh
        if (!mesh.positions.empty()) {
            mn = mx = mesh.positions[0];
            for (const Float3& p : mesh.positions) {
                mn = {std::min(mn.x, p.x), std::min(mn.y, p.y), std::min(mn.z, p.z)};
                mx = {std::max(mx.x, p.x), std::max(mx.y, p.y), std::max(mx.z, p.z)};
            }
        }
        edit("Add Humanoid Rig", [&](Armature& a) { AddHumanoidRig(a, mn, mx, (RigNaming)naming); });
        status = "Added a 21-bone humanoid rig (parent with automatic weights to bind it)";
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("T-pose skeleton fitted to the mesh height, facing +Z, left on +X");
    ImGui::SameLine();
    ImGui::BeginDisabled(arm.bones.empty());
    if (ImGui::Button("Rename Bones")) {
        int renamed = 0;
        edit("Rename Bones", [&](Armature& a) { renamed = ApplyRigNaming(a, (RigNaming)naming); });
        status = "Renamed " + std::to_string(renamed) + " bones to " + RigNamingName((RigNaming)naming) + " names";
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Renames recognised humanoid bones (from any convention) to the chosen one");
    return status;
}

int BoneConstraintsUI(const Armature& arm, int self, Bone& b, const std::function<void(const char*)>& track) {
    const int n = (int)arm.bones.size();
    int result = 0;
    auto live = [&](bool changed) { if (changed) result = std::max(result, 1); };
    auto boneCombo = [&](const char* label, int& t, const char* noneLabel) {
        bool valid = t >= 0 && t < n && t != self;
        if (ImGui::BeginCombo(label, valid ? arm.bones[t].name.c_str() : noneLabel)) {
            if (ImGui::Selectable(noneLabel, !valid)) { t = -1; result = 2; }
            for (int i = 0; i < n; ++i) {
                if (i == self) continue;
                ImGui::PushID(i);
                if (ImGui::Selectable(arm.bones[i].name.c_str(), t == i)) { t = i; result = 2; }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        return t >= 0 && t < n && t != self;
    };

    ImGui::Spacing(); ImGui::TextColored(kBrass, "CONSTRAINTS"); ImGui::Separator();
    int remove = -1;
    ImGui::PushItemWidth(-90.0f);
    for (int ci = 0; ci < (int)b.constraints.size(); ++ci) {
        BoneConstraint& c = b.constraints[ci];
        ImGui::PushID(ci);
        if (ImGui::Checkbox("##on", &c.enabled)) result = 2;
        ImGui::SameLine(); ImGui::TextUnformatted(ConstraintTypeName(c.type));
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 24.0f);
        if (ImGui::SmallButton("x")) remove = ci;
        switch (c.type) {
            case BoneConstraint::CopyLocation:
            case BoneConstraint::TrackTo:
            case BoneConstraint::IK:
                if (!boneCombo("Target", c.target, "(point)")) {
                    live(ImGui::DragFloat3("Point", &c.point.x, 0.01f, 0, 0, "%.3f m"));
                    track("Move Constraint Target");
                }
                if (c.type == BoneConstraint::IK) {
                    if (b.parent < 0) ImGui::TextDisabled("IK needs a parent bone: it bends this bone and its parent.");
                    if (ImGui::Checkbox("Pole", &c.usePole)) result = 2;
                    if (c.usePole && !boneCombo("Pole bone", c.poleTarget, "(point)")) {
                        live(ImGui::DragFloat3("Pole point", &c.polePoint.x, 0.01f, 0, 0, "%.3f m"));
                        track("Move Pole");
                    }
                }
                break;
            case BoneConstraint::CopyRotation:
                boneCombo("Target", c.target, "(pick a bone)");
                break;
            case BoneConstraint::LimitRotation: {
                const char* axes[3] = {"X", "Y", "Z"};
                float* lo = &c.minRot.x;
                float* hi = &c.maxRot.x;
                for (int a = 0; a < 3; ++a) {
                    ImGui::PushID(a);
                    if (ImGui::Checkbox(axes[a], &c.limit[a])) result = 2;
                    ImGui::SameLine();
                    float deg[2] = {lo[a] * kDeg, hi[a] * kDeg};
                    if (ImGui::DragFloat2("min / max", deg, 0.5f, -180.0f, 180.0f, "%.1f deg")) {
                        lo[a] = deg[0] / kDeg; hi[a] = deg[1] / kDeg;
                        result = std::max(result, 1);
                    }
                    track("Edit Rotation Limit");
                    ImGui::PopID();
                }
                break;
            }
        }
        ImGui::Separator();
        ImGui::PopID();
    }
    ImGui::PopItemWidth();
    if (remove >= 0) { b.constraints.erase(b.constraints.begin() + remove); result = 2; }

    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##addcon", "Add Constraint...")) {
        const BoneConstraint::Type types[] = {BoneConstraint::IK, BoneConstraint::TrackTo, BoneConstraint::CopyLocation,
                                              BoneConstraint::CopyRotation, BoneConstraint::LimitRotation};
        for (BoneConstraint::Type t : types)
            if (ImGui::Selectable(ConstraintTypeName(t))) {
                BoneConstraint c;
                c.type = t;
                c.point = b.tail;   // a target point that starts where the bone already points
                b.constraints.push_back(c);
                result = 2;
            }
        ImGui::EndCombo();
    }
    return result;
}
