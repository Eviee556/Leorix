// Curve Edit Mode: picking and drawing control points and handles, extrude/delete, the curve
// settings (handle types, cyclic, bevel, taper) and Convert to Mesh. G/R/S run through the shared
// transform operator, which moves curve elements instead of mesh vertices for a curve object.
#include "app/App.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace {
const ImVec4 kBrass(0.79f, 0.54f, 0.29f, 1.0f);
ImU32 HandleColor(HandleType t) {
    switch (t) {
        case HandleType::Auto: return IM_COL32(230, 210, 80, 230);
        case HandleType::Vector: return IM_COL32(110, 210, 110, 230);
        case HandleType::Aligned: return IM_COL32(230, 120, 200, 230);
        case HandleType::Free: return IM_COL32(40, 40, 40, 230);
    }
    return IM_COL32_WHITE;
}
std::set<int> SelectedPoints(const MeshObject& o) {
    std::set<int> pts;
    for (int e : o.curveSel) if (e % 3 == 0 && e / 3 < (int)o.curve.points.size()) pts.insert(e / 3);
    return pts;
}
} // namespace

const CurvePath* App::CurveProfile(const MeshObject& o) const {
    int b = o.curve.bevelObject;
    if (b < 0 || b >= (int)scene.objects.size() || scene.objects[b].get() == &o || !scene.objects[b]->isCurve) return nullptr;
    return &scene.objects[b]->curve;
}

void App::RebuildProfileUsers(const MeshObject& profile) {
    // one level only, so two curves that use each other as profiles can't recurse
    for (auto& up : scene.objects) {
        MeshObject& u = *up;
        if (&u == &profile || !u.isCurve || CurveProfile(u) != &profile.curve) continue;
        u.mesh = CurveToMesh(u.curve, &profile.curve);
        u.mesh.name = u.name;
        u.MarkTopoDirty();
        u.stackDirty = true;
    }
}

void App::RebuildCurveMesh(MeshObject& o) {
    o.curve.Sync();
    if (!o.isCurve) return;
    o.mesh = CurveToMesh(o.curve, CurveProfile(o));
    o.mesh.name = o.name;
    o.selVerts.clear(); o.selEdges.clear(); o.selFaces.clear();
    o.MarkTopoDirty();
    o.stackDirty = true;
    RebuildProfileUsers(o);
    scene.MarkCurveDependentsDirty();
}

void App::PushCurveUndo(const std::string& label, MeshObject* o, const CurvePath& beforeCurve, const Mesh& beforeMesh, bool beforeIsCurve) {
    if (!o) return;
    struct Snap { CurvePath c; Mesh m; bool isCurve; };
    auto before = std::make_shared<Snap>(Snap{beforeCurve, beforeMesh, beforeIsCurve});
    auto after = std::make_shared<Snap>(Snap{o->curve, o->mesh, o->isCurve});
    auto put = [this, o](const Snap& s) {
        if (scene.IndexOf(o) < 0) return;   // the object was deleted since
        o->curve = s.c; o->mesh = s.m; o->isCurve = s.isCurve;
        o->curveSel.clear(); o->selVerts.clear(); o->selEdges.clear(); o->selFaces.clear();
        o->MarkTopoDirty();
        o->stackDirty = true;
        RebuildProfileUsers(*o);
        scene.MarkCurveDependentsDirty();
    };
    size_t bytes = (before->c.points.size() + after->c.points.size()) * sizeof(Float3) * 4 +
                   (before->m.positions.size() + after->m.positions.size()) * sizeof(Float3) +
                   (before->m.tris.size() + after->m.tris.size()) * sizeof(Tri);
    undoStack.Push(label, [put, before] { put(*before); }, [put, after] { put(*after); }, bytes);
}

void App::CurveEditOverlay(ImDrawList* dl, MeshObject& c) {
    const CurvePath& cv = c.curve;
    if (cv.nurbs) {
        // the control polygon: NURBS points sit off the curve, so show what they pull on
        const int n = (int)cv.points.size();
        for (int i = 0; i + 1 < n || (cv.cyclic && i < n && n > 2); ++i) {
            float z; ImVec2 a, b;
            if (viewport.ProjectPoint(c.transform.Apply(cv.points[i]), z, a) &&
                viewport.ProjectPoint(c.transform.Apply(cv.points[(i + 1) % n]), z, b))
                dl->AddLine(a, b, IM_COL32(150, 150, 150, 160), 1.0f);
        }
    }
    for (int i = 0; i < (int)cv.points.size(); ++i) {
        float z; ImVec2 sp, sl, sr;
        if (!viewport.ProjectPoint(c.transform.Apply(cv.points[i]), z, sp)) continue;
        HandleType t = i < (int)cv.handleType.size() ? cv.handleType[i] : HandleType::Auto;
        for (int k = 1; k <= 2 && !cv.nurbs; ++k) {
            ImVec2& hs = k == 1 ? sl : sr;
            if (!viewport.ProjectPoint(c.transform.Apply(cv.Elem(i * 3 + k)), z, hs)) continue;
            dl->AddLine(sp, hs, HandleColor(t), 1.2f);
            bool sel = c.curveSel.count(i * 3 + k) != 0;
            dl->AddCircleFilled(hs, 3.5f, sel ? IM_COL32(255, 170, 60, 255) : HandleColor(t));
        }
        bool sel = c.curveSel.count(i * 3) != 0;
        dl->AddRectFilled(ImVec2(sp.x - 4, sp.y - 4), ImVec2(sp.x + 4, sp.y + 4), sel ? IM_COL32(255, 170, 60, 255) : IM_COL32(20, 20, 20, 240));
        dl->AddRect(ImVec2(sp.x - 4, sp.y - 4), ImVec2(sp.x + 4, sp.y + 4), IM_COL32(240, 240, 240, 230));
    }
}

void App::CurveEditClick() {
    MeshObject* o = scene.Active();
    if (!o || !o->isCurve || !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;
    ImVec2 m = ImGui::GetIO().MousePos;
    int best = -1;
    float bestD = 10.0f;   // pixels
    for (int e = 0; e < o->curve.ElemCount(); ++e) {
        if (o->curve.nurbs && e % 3 != 0) continue;   // NURBS points have no handles
        float z; ImVec2 sp;
        if (!viewport.ProjectPoint(o->transform.Apply(o->curve.Elem(e)), z, sp)) continue;
        float d = std::hypot(sp.x - m.x, sp.y - m.y) - (e % 3 == 0 ? 1.5f : 0.0f);   // points win ties
        if (d < bestD) { bestD = d; best = e; }
    }
    bool shift = ImGui::GetIO().KeyShift;
    if (!shift) o->curveSel.clear();
    if (best >= 0) {
        if (shift && o->curveSel.count(best)) o->curveSel.erase(best); else o->curveSel.insert(best);
    } else {
        boxArmed = true;                 // a click on empty space may become a box select
        boxStart = m;
    }
}

void App::CurveEditOp(char op) {
    MeshObject* o = scene.Active();
    if (!o || !o->isCurve) return;
    CurvePath bc = o->curve; Mesh bm = o->mesh;
    std::set<int> pts = SelectedPoints(*o);
    if (op == 'E') {
        if (pts.size() != 1) { ShowStatus("Select one end point of the curve to extrude"); return; }
        int at = ExtrudeCurveEnd(o->curve, *pts.begin());
        if (at < 0) { ShowStatus(o->curve.cyclic ? "A closed curve has no end to extrude" : "Only an end point can be extruded"); return; }
        o->curveSel = {at * 3};
        RebuildCurveMesh(*o);
        PushCurveUndo("Extrude Curve", o, bc, bm, true);
        BeginTransform(0);
    } else if (op == 'X') {
        if (pts.empty()) { ShowStatus("Select curve points to delete"); return; }
        if (!DeleteCurvePoints(o->curve, pts)) { ShowStatus("A curve needs at least two points"); return; }
        o->curveSel.clear();
        RebuildCurveMesh(*o);
        PushCurveUndo("Delete Curve Points", o, bc, bm, true);
        ShowStatus("Deleted " + std::to_string(pts.size()) + " curve point(s)");
    }
}

void App::CurveSettingsUI() {
    MeshObject* o = scene.Active();
    if (!o || !o->isCurve) return;
    o->curve.Sync();
    ImGui::Spacing(); ImGui::TextColored(kBrass, "CURVE"); ImGui::Separator();
    // One undo step per widget drag: remember the state when it starts, push when it ends.
    static CurvePath startCurve; static Mesh startMesh;
    auto track = [&](const char* label, bool changed) {
        if (ImGui::IsItemActivated()) { startCurve = o->curve; startMesh = o->mesh; }
        if (changed) RebuildCurveMesh(*o);
        if (ImGui::IsItemDeactivatedAfterEdit()) PushCurveUndo(label, o, startCurve, startMesh, true);
    };
    bool cyc = o->curve.cyclic;
    if (ImGui::Checkbox("Cyclic", &cyc)) {
        CurvePath bc = o->curve; Mesh bm = o->mesh;
        o->curve.cyclic = cyc;
        RebuildCurveMesh(*o);
        PushCurveUndo(cyc ? "Close Curve" : "Open Curve", o, bc, bm, true);
    }
    track("Curve Resolution", ImGui::SliderInt("Resolution", &o->curve.resolution, 1, 64));
    track("Bevel Depth", ImGui::DragFloat("Bevel Depth", &o->curve.bevelDepth, 0.005f, 0.0f, 10.0f, "%.3f"));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Radius of the tube swept along the curve; 0 shows only the path");
    track("Bevel Resolution", ImGui::SliderInt("Bevel Resolution", &o->curve.bevelResolution, 0, 16));
    {
        // Bevel profile: another curve's shape (its X/Z plane) swept along this one
        const CurvePath* prof = CurveProfile(*o);
        const char* cur = prof ? scene.objects[o->curve.bevelObject]->name.c_str() : "Round";
        if (ImGui::BeginCombo("Bevel Profile", cur)) {
            int pick = -2;
            if (ImGui::Selectable("Round", !prof)) pick = -1;
            for (int i = 0; i < (int)scene.objects.size(); ++i) {
                const MeshObject& c = *scene.objects[i];
                if (&c == o || !c.isCurve) continue;
                ImGui::PushID(i);
                if (ImGui::Selectable(c.name.c_str(), prof == &c.curve)) pick = i;
                ImGui::PopID();
            }
            ImGui::EndCombo();
            if (pick != -2) {
                CurvePath bc = o->curve; Mesh bm = o->mesh;
                o->curve.bevelObject = pick;
                RebuildCurveMesh(*o);
                PushCurveUndo("Bevel Profile", o, bc, bm, true);
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sweep another curve's shape (seen from above) instead of a round tube");
    }

    std::set<int> pts = SelectedPoints(*o);
    {
        int type = o->curve.nurbs ? 1 : 0;
        if (ImGui::Combo("Type", &type, "Bezier\0NURBS\0")) {
            CurvePath bc = o->curve; Mesh bm = o->mesh;
            o->curve.nurbs = type == 1;
            o->curveSel.clear();
            RebuildCurveMesh(*o);
            PushCurveUndo(type ? "Make NURBS" : "Make Bezier", o, bc, bm, true);
        }
    }
    if (o->curve.nurbs) {
        track("NURBS Order", ImGui::SliderInt("Order", &o->curve.order, 2, 6));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("2 = straight lines between points; higher = smoother and further from the points");
        bool ep = o->curve.endpoint;
        if (ImGui::Checkbox("Endpoint", &ep)) {
            CurvePath bc = o->curve; Mesh bm = o->mesh;
            o->curve.endpoint = ep;
            o->curve.knots.clear();
            RebuildCurveMesh(*o);
            PushCurveUndo("NURBS Endpoint", o, bc, bm, true);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Make an open curve start and end exactly on its first and last points");
        if (!pts.empty()) {
            float w = o->curve.WeightAt(*pts.begin());
            bool changed = ImGui::DragFloat("Weight", &w, 0.01f, 0.01f, 100.0f, "%.3f");
            if (changed) for (int i : pts) o->curve.weight[i] = std::max(w, 0.01f);
            track("NURBS Weight", changed);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("How strongly the selected points pull the curve towards them");
        }
    }
    if (!o->curve.nurbs) ImGui::TextDisabled(pts.empty() ? "Handles (all points)" : "Handles (%d selected point%s)", (int)pts.size(), pts.size() == 1 ? "" : "s");
    for (int t = 0; t < 4 && !o->curve.nurbs; ++t) {
        if (t) ImGui::SameLine();
        if (ImGui::Button(HandleTypeName((HandleType)t))) {
            CurvePath bc = o->curve; Mesh bm = o->mesh;
            for (int i = 0; i < (int)o->curve.points.size(); ++i)
                if (pts.empty() || pts.count(i)) o->curve.handleType[i] = (HandleType)t;
            RebuildCurveMesh(*o);
            PushCurveUndo("Set Handle Type", o, bc, bm, true);
        }
    }
    if (!pts.empty()) {
        float r = o->curve.RadiusAt(*pts.begin());
        bool changed = ImGui::DragFloat("Radius (taper)", &r, 0.01f, 0.0f, 20.0f, "%.2f");
        if (changed) for (int i : pts) o->curve.radius[i] = r;
        track("Curve Radius", changed);
    }
    ImGui::TextDisabled("Edit Mode: click or drag a box over points, G move, E extrude an end, X delete");
    if (ImGui::Button("Convert to Mesh")) {
        if (o->mesh.positions.empty()) ShowStatus("Set a Bevel Depth or Bevel Profile first - the tube is what becomes the mesh");
        else {
            CurvePath bc = o->curve; Mesh bm = o->mesh;
            o->isCurve = false;
            o->curveSel.clear();
            o->MarkTopoDirty();
            o->stackDirty = true;
            PushCurveUndo("Convert to Mesh", o, bc, bm, true);
            ShowStatus(o->name + " is now a mesh");
        }
    }
}
