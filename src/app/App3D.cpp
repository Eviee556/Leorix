// 3D modes: Object, Sculpt, Edit, Retopology. Tool shelves inside the viewport, header widgets,
// the 3D Tool Settings panel and the custom image sculpt brush import.
#include "tools/Snap.h"
#include "app/App.h"
#include "app/Icons.h"
#include "app/RigPanel.h"
#include "anim/ArmatureLink.h"
#include "imgui.h"
#include "mesh/IO.h"
#include "mesh/Primitives.h"
#include "mesh/Unwrap.h"
#include "mesh/UVEdit.h"
#include "mesh/EditOps.h"
#include "mesh/Axes.h"
#include "tools/Script.h"
#include "tools/Sculpt.h"
#include <SDL3/SDL.h>
#include <cfloat>
#include <functional>
#include <unordered_set>
#include <set>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

static const ImVec4 kBrass(0.79f, 0.54f, 0.29f, 1.0f);

static void Section(const char* title) { ImGui::Spacing(); ImGui::TextColored(kBrass, "%s", title); ImGui::Separator(); }
static void Hint(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}
static bool Toggle(const char* label, bool* v, float width = 0) { // small pill toggle like Blender's X/Y/Z buttons
    if (*v) { ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.54f, 0.37f, 0.22f, 1)); ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kBrass); }
    bool pressed = ImGui::Button(label, ImVec2(width, 0));
    if (*v) ImGui::PopStyleColor(2);
    if (pressed) *v = !*v;
    return pressed;
}

const char* App::Mode3DName(Mode3D m) const {
    switch (m) {
        case Mode3D::Object: return "Object";
        case Mode3D::Sculpt: return "Sculpt";
        case Mode3D::Edit: return "Edit";
        case Mode3D::Retopology: return "Retopology";
        case Mode3D::Pose: return "Pose";
        case Mode3D::Weight: return "Weight Paint";
    }
    return "";
}

static Icon ModeIcon(Mode3D m) {
    switch (m) {
        case Mode3D::Object: return Icon::ModeObject;
        case Mode3D::Sculpt: return Icon::ModeSculpt;
        case Mode3D::Edit: return Icon::ModeEdit;
        case Mode3D::Retopology: return Icon::ModeRetopo;
        case Mode3D::Pose: return Icon::SculptPose;
        case Mode3D::Weight: return Icon::SculptPaint;
    }
    return Icon::None;
}

void App::SetMode3D(Mode3D m) {
    // Editing a baked cloth's vertices would be overwritten by playback, so keep its shape and drop the bake.
    if (m == Mode3D::Edit || m == Mode3D::Sculpt || m == Mode3D::Retopology)
        if (MeshObject* o = scene.Active(); o && o->cloth.Baked()) { DropClothBake(*o, true); ShowStatus("Kept the cloth's current shape and dropped its bake"); }
    mode3D = m;
    viewport.modeLabel = Mode3DName(m);
}

// ---------------------------------------------------------------- header row
void App::ViewportHeader3D() {
    // Mode menu with icons, like Blender's interaction mode dropdown
    ImGui::SetNextItemWidth(160);
    const float iconSz = 16.0f;
    ImVec2 comboPos = ImGui::GetCursorScreenPos();
    char preview[32]; std::snprintf(preview, sizeof(preview), "      %s", Mode3DName(mode3D));
    if (ImGui::BeginCombo("##mode3d", preview)) {
        for (Mode3D m : {Mode3D::Object, Mode3D::Sculpt, Mode3D::Edit, Mode3D::Retopology, Mode3D::Pose, Mode3D::Weight}) {
            ImGui::PushID(static_cast<int>(m));
            ImVec2 p = ImGui::GetCursorScreenPos();
            char label[40]; std::snprintf(label, sizeof(label), "      %s Mode", Mode3DName(m));
            if (ImGui::Selectable(label, mode3D == m)) SetMode3D(m);
            DrawIcon(ImGui::GetWindowDrawList(), ModeIcon(m), ImVec2(p.x + 10, p.y + ImGui::GetTextLineHeight() * 0.5f), iconSz, ImGui::GetColorU32(kBrass));
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    DrawIcon(ImGui::GetWindowDrawList(), ModeIcon(mode3D), ImVec2(comboPos.x + 14, comboPos.y + ImGui::GetFrameHeight() * 0.5f), iconSz, ImGui::GetColorU32(kBrass));

    if (mode3D == Mode3D::Edit) {
        ImGui::SameLine();
        const float b = ImGui::GetFrameHeight();
        struct { SelectMode m; Icon icon; const char* tip; } modes[] = {
            {SelectMode::Vertex, Icon::SelVertex, "Vertex select (1)  -  Shift+click to combine"},
            {SelectMode::Edge,   Icon::SelEdge,   "Edge select (2)  -  Shift+click to combine"},
            {SelectMode::Face,   Icon::SelFace,   "Face select (3)  -  Shift+click to combine"},
        };
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(1, 0));
        for (auto& sm : modes) {
            ImGui::PushID(static_cast<int>(sm.m));
            bool on = (edit.selectMode & static_cast<int>(sm.m)) != 0;
            if (IconButton("##sel", sm.icon, on, sm.tip, b)) {
                if (ImGui::GetIO().KeyShift) { edit.selectMode ^= static_cast<int>(sm.m); if (!edit.selectMode) edit.selectMode = static_cast<int>(sm.m); }
                else edit.selectMode = static_cast<int>(sm.m);
            }
            ImGui::SameLine();
            ImGui::PopID();
        }
        ImGui::PopStyleVar();
        ImGui::Dummy(ImVec2(4, 0)); ImGui::SameLine();
        IconButton("##xray", Icon::XRay, edit.xray, "Toggle X-ray (Alt+Z)", b);
        if (ImGui::IsItemClicked()) edit.xray = !edit.xray;
    } else if (mode3D == Mode3D::Sculpt) {
        ImGui::SameLine(); ImGui::SetNextItemWidth(110); ImGui::SliderFloat("##radius", &sculpt.radiusPx, 1, 500, "Radius %.0f px");
        ImGui::SameLine(); ImGui::SetNextItemWidth(110); ImGui::SliderFloat("##strength", &sculpt.strength, 0, 1, "Strength %.2f");
        ImGui::SameLine(); Toggle("X", &sculpt.symX); ImGui::SameLine(); Toggle("Y", &sculpt.symY); ImGui::SameLine(); Toggle("Z", &sculpt.symZ);
    } else if (mode3D == Mode3D::Retopology) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Source: none");
        ImGui::SameLine(); Toggle("Snap to Source", &retopo.snapToSource);
        ImGui::SameLine(); Toggle("X", &retopo.symX);
    }
}

// ---------------------------------------------------------------- tool shelf inside the viewport
void App::MeshStatsOverlay(ImVec2 origin, ImVec2 size) {
    if (!scene.HasMesh()) return;
    const Mesh& m = scene.Active()->mesh;
    char stats[256];
    std::snprintf(stats, sizeof(stats), "%s\nVerts %zu\nTris %zu\nPolys %u",
                  m.name.c_str(), m.VertexCount(), m.TriCount(), m.polyCount);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 ts = ImGui::CalcTextSize(stats);
    ImVec2 p(origin.x + size.x - ts.x - 20, origin.y + 12);
    dl->AddRectFilled(ImVec2(p.x - 8, p.y - 5), ImVec2(p.x + ts.x + 8, p.y + ts.y + 5), IM_COL32(15, 18, 22, 190), 5.0f);
    dl->AddText(p, IM_COL32(232, 227, 216, 255), stats);
}

// Draw the actual mesh and, in edit mode, run picking + selection. Called from the overlay
// so it renders inside the viewport clip rect with the current camera.
void App::EditModePick(bool viewportHovered) {
    if (scene.objects.empty()) return;
    SyncRigs(scene);   // controls moved, or a linked mesh / its armature object moved
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 mouse = ImGui::GetIO().MousePos;
    bool editMode = mode3D == Mode3D::Edit;
    bool retopoMode = mode3D == Mode3D::Retopology;
    meshPrefs.wireframe = editMode || retopoMode;
    ImVec2 o = viewport.ViewportOrigin(), sz = viewport.ViewportSize();
    dl->PushClipRect(o, ImVec2(o.x + sz.x, o.y + sz.y), true);

    // Rendered mode grounds objects with a projected shadow before anything is shaded.
    if (shading == ShadingMode::Rendered) {
        ViewLighting lt = BuildLighting();
        for (auto& obj : scene.objects) DrawGroundShadow(dl, viewport, *obj, lt);
    }

    // Every visible object is drawn. Edit/sculpt overlays only apply to the active one.
    lastPick = PickResult{};
    lastPickObject = -1;
    int pickedObject = -1;
    float bestDepth = FLT_MAX;
    for (int i = 0; i < (int)scene.objects.size(); ++i) {
        MeshObject& obj = *scene.objects[i];
        if (!obj.visible || obj.isControl) continue;   // controls are wire overlays, drawn below
        bool isActive = (i == scene.activeIndex);
        bool objEdit = editMode && isActive && !obj.isCurve;   // a curve edits its points, not its tube
        bool objRetopo = retopoMode && isActive;
        meshPrefs.weightBone = (mode3D == Mode3D::Weight && isActive) ? activeBone : -1;
        PickResult p = DrawAndPickMesh(dl, viewport, obj, edit.selectMode, objEdit, objRetopo, meshPrefs,
                                       mouse, viewportHovered, shading, BuildLighting());
        if (p.hit && p.depth < bestDepth) { bestDepth = p.depth; pickedObject = i; if (isActive || !editMode) { lastPick = p; lastPickObject = i; } }
    }
    DrawCameraGizmo();
    DrawControls(dl, mouse, viewportHovered, pickedObject);

    // curve objects: the evaluated path plus its control points
    for (int i = 0; i < (int)scene.objects.size(); ++i) {
        MeshObject& c = *scene.objects[i];
        if (!c.isCurve || !c.visible || c.curve.points.empty()) continue;
        std::vector<Float3> path = c.curve.Evaluate();
        ImU32 col = c.selected ? IM_COL32(255, 170, 60, 240) : IM_COL32(150, 200, 255, 220);
        ImVec2 prev; bool havePrev = false;
        for (const Float3& p : path) {
            float z; ImVec2 sp;
            if (!viewport.ProjectPoint(c.transform.Apply(p), z, sp)) { havePrev = false; continue; }
            if (havePrev) dl->AddLine(prev, sp, col, 2.0f);
            prev = sp; havePrev = true;
        }
        if (editMode && i == scene.activeIndex) { CurveEditOverlay(dl, c); continue; }
        for (const Float3& p : c.curve.points) {
            float z; ImVec2 sp;
            if (viewport.ProjectPoint(c.transform.Apply(p), z, sp)) dl->AddCircleFilled(sp, 3.5f, col);
        }
    }

    // bones on top of the surface, like Blender draws them in pose mode
    for (int i = 0; i < (int)scene.objects.size(); ++i)
        if (scene.objects[i]->visible && !scene.objects[i]->armature.bones.empty())
            DrawArmature(dl, viewport, *scene.objects[i], i == scene.activeIndex ? activeBone : -1);
    dl->PopClipRect();

    if (boxArmed || boxSelecting) {
        ImVec2 cur = ImGui::GetIO().MousePos;
        float moved = std::fabs(cur.x - boxStart.x) + std::fabs(cur.y - boxStart.y);
        if (boxArmed && moved > 5.0f) { boxSelecting = true; boxArmed = false; }
        if (boxSelecting) {
            ImVec2 a(std::min(boxStart.x, cur.x), std::min(boxStart.y, cur.y));
            ImVec2 b(std::max(boxStart.x, cur.x), std::max(boxStart.y, cur.y));
            ImGui::GetForegroundDrawList()->AddRectFilled(a, b, IM_COL32(230, 180, 90, 28));
            ImGui::GetForegroundDrawList()->AddRect(a, b, IM_COL32(230, 180, 90, 220), 0, 0, 1.2f);
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (boxSelecting)
                FinishBoxSelect(boxStart, cur, ImGui::GetIO().KeyShift, ImGui::GetIO().KeyCtrl);
            else if (boxArmed && mode3D == Mode3D::Object && !ImGui::GetIO().KeyShift) {
                for (auto& obj : scene.objects) obj->selected = false;   // a plain click on empty space clears
                selectedCamera = -1;
            }
            boxSelecting = false;
            boxArmed = false;
        }
    }

    // Before the hover check: a stroke released outside the viewport must still become an undo step.
    if (sculpting && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) { sculpting = false; SculptStrokeEnd(); }
    if (paintingModel && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) PaintStrokeEnd();
    if (weightPainting && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) WeightStrokeEnd();
    if (!viewportHovered || xform.active) return;
    // Alt+LMB, the gizmo and the pan/zoom handles steer the view; they must not also select.
    if (viewport.navigating || viewport.overNavWidget || ImGui::GetIO().KeyAlt) return;

    if (mode3D == Mode3D::Object) {
        ImGui::SetMouseCursor(pickedObject >= 0 ? ImGuiMouseCursor_Hand : ImGuiMouseCursor_Arrow);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (pickedObject >= 0) {
                selectedCamera = -1;
                if (ImGui::GetIO().KeyShift) { scene.objects[pickedObject]->selected = true; scene.SetActive(pickedObject); }
                else scene.SelectOnly(pickedObject);
                ShowStatus("Selected " + scene.objects[pickedObject]->name);
            } else {
                boxArmed = true;                 // may become a box once the mouse moves
                boxStart = ImGui::GetIO().MousePos;
            }
        }
    } else if (editMode) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        MeshObject* act = scene.Active();
        if (act && act->isCurve) { CurveEditClick(); return; }
        if (act && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !lastPick.hit) {
            boxArmed = true;
            boxStart = ImGui::GetIO().MousePos;
        }
        if (act && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && lastPick.hit) {
            bool shift = ImGui::GetIO().KeyShift;
            auto& obj = *act;
            if (edit.selectMode & 1) { if (!shift) obj.selVerts.clear(); if (obj.selVerts.count(lastPick.vertex)) obj.selVerts.erase(lastPick.vertex); else obj.selVerts.insert(lastPick.vertex); }
            else if (edit.selectMode & 2) { if (!shift) obj.selEdges.clear(); if (obj.selEdges.count(lastPick.edge)) obj.selEdges.erase(lastPick.edge); else obj.selEdges.insert(lastPick.edge); }
            else if (edit.selectMode & 4) { if (!shift) obj.selFaces.clear(); uint32_t poly = obj.mesh.tris[lastPick.tri].poly; if (obj.selFaces.count(poly)) obj.selFaces.erase(poly); else obj.selFaces.insert(poly); }
        }
    } else if (mode3D == Mode3D::Sculpt) {
        const bool texPaint = kSculptBrushes[sculpt.brush].icon == Icon::SculptPaint;
        if (texPaint) {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && lastPick.hit) PaintOnModel(true);
            else if (paintingModel && ImGui::IsMouseDown(ImGuiMouseButton_Left)) PaintOnModel(false);
        } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && lastPick.hit) {
            sculpting = true;
            lastSculptPoint = lastPick.point;
            SculptStroke(true);
        } else if (sculpting && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            SculptStroke(false);
        }
        if (sculpting && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) { sculpting = false; SculptStrokeEnd(); }
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
        float r = sculpt.radiusPx;
        bool onSurface = lastPick.hit;
        ImU32 ring = onSurface ? IM_COL32(230, 180, 90, 230) : IM_COL32(150, 158, 175, 120);
        dl->AddCircle(mouse, r, ring, 64, onSurface ? 1.8f : 1.2f);
        if (sculpt.hardness > 0.02f)
            dl->AddCircle(mouse, r * std::clamp(sculpt.hardness, 0.05f, 1.0f), IM_COL32(230, 180, 90, 90), 64, 1.0f);
        dl->AddCircleFilled(mouse, 2.0f, onSurface ? IM_COL32(255, 255, 255, 220) : IM_COL32(200, 200, 200, 120));
    } else if (mode3D == Mode3D::Weight) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && lastPick.hit) WeightStroke(true);
        else if (weightPainting && ImGui::IsMouseDown(ImGuiMouseButton_Left)) WeightStroke(false);
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
        dl->AddCircle(mouse, weightPaint.brush.radiusPx, lastPick.hit ? IM_COL32(230, 180, 90, 230) : IM_COL32(150, 158, 175, 120), 64, 1.6f);
    } else if (retopoMode) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
}

void App::ViewportShelf(ImVec2 origin, ImVec2 size) {
    const bool sculptMode = mode3D == Mode3D::Sculpt;
    const float btn = sculptMode ? 30.0f : 32.0f;
    const int cols = sculptMode ? 2 : 1;
    const float pad = 5.0f, spacing = 3.0f;
    const float width = cols * btn + (cols - 1) * spacing + pad * 2 + (sculptMode ? 8.0f : 0.0f);
    viewport.hudLeftInset = width + 18.0f;

    ImGui::SetCursorScreenPos(ImVec2(origin.x + 8, origin.y + 8));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.106f, 0.122f, 0.153f, 0.88f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing, spacing));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 6.0f);
    float maxH = std::max(80.0f, size.y - 16.0f);
    if (ImGui::BeginChild("##shelf", ImVec2(width, 0), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, maxH));
        auto toolColumn = [&](const ToolDef3D* tools, int count, int& current) {
            for (int i = 0; i < count; ++i) {
                ImGui::PushID(i);
                char tip[160];
                if (tools[i].shortcut[0]) std::snprintf(tip, sizeof(tip), "%s (%s)\n%s", tools[i].name, tools[i].shortcut, tools[i].hint);
                else std::snprintf(tip, sizeof(tip), "%s\n%s", tools[i].name, tools[i].hint);
                if (IconButton("##tool", tools[i].icon, current == i, tip, btn)) current = i;
                ImGui::PopID();
            }
        };
        switch (mode3D) {
            case Mode3D::Object:     toolColumn(kObjectTools, kObjectToolCount, objectTool); break;
            case Mode3D::Edit:       toolColumn(kEditTools, kEditToolCount, edit.tool); break;
            case Mode3D::Retopology: toolColumn(kRetopoTools, kRetopoToolCount, retopo.tool); break;
            case Mode3D::Pose: case Mode3D::Weight: break;
            case Mode3D::Sculpt: {
                BrushCategory lastCat = kSculptBrushes[0].category;
                int col = 0;
                for (int i = 0; i < kSculptBrushCount; ++i) {
                    const auto& b = kSculptBrushes[i];
                    if (b.category != lastCat) { ImGui::Separator(); lastCat = b.category; col = 0; }
                    if (col % cols) ImGui::SameLine();
                    ++col;
                    ImGui::PushID(i);
                    char tip[200];
                    std::snprintf(tip, sizeof(tip), "%s%s%s%s\n%s  [%s]", b.name, b.shortcut[0] ? " (" : "", b.shortcut, b.shortcut[0] ? ")" : "",
                                  b.hint, BrushCategoryName(b.category));
                    if (IconButton("##brush", b.icon, sculpt.brush == i, tip, btn)) {
                        sculpt.brush = i;
                        sculpt.strength = b.strength;
                        if (b.icon == Icon::SculptCustomImage && !brushImage.Loaded()) RequestBrushImageImport();
                    }
                    ImGui::PopID();
                }
                break;
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor();
    MeshStatsOverlay(origin, size);
}

// ---------------------------------------------------------------- keyboard (only while the pointer is over the 3D viewport)
void App::HandleShortcuts3D(bool hoveredViewport) {
    ImGuiIO& io = ImGui::GetIO();
    if (std::getenv("LEORIX_DEBUG_KEYS") && ImGui::IsKeyPressed(ImGuiKey_D, false)) {
        std::printf("[keys] D at entry: hovered=%d textInput=%d alt=%d xform=%d\n", hoveredViewport ? 1 : 0,
                    io.WantTextInput ? 1 : 0, io.KeyAlt ? 1 : 0, xform.active ? 1 : 0);
        std::fflush(stdout);
    }
    if (std::getenv("LEORIX_DEBUG_KEYS") && ImGui::IsKeyPressed(ImGuiKey_G, false))
        std::printf("[keys] G pressed: hovered=%d mode=%d textInput=%d active=%d\n",
                    hoveredViewport ? 1 : 0, (int)mode3D, io.WantTextInput ? 1 : 0, xform.active ? 1 : 0);
    // Ctrl combos (Ctrl+Space, Ctrl+J, Ctrl+E) used to be swallowed here by a blanket "Ctrl held,
    // bail" check, so none of them could ever fire. Only plain-letter shortcuts are Ctrl-guarded now.
    if (!hoveredViewport || io.WantTextInput || showNewProject || io.KeyAlt) return;
    if (xform.active) return;   // the modal operator owns the keyboard while it runs
    if (ImGui::IsKeyPressed(ImGuiKey_Tab, false)) { SetMode3D(mode3D == Mode3D::Edit ? Mode3D::Object : Mode3D::Edit); return; }
    if (std::getenv("LEORIX_DEBUG_KEYS") && ImGui::IsKeyPressed(ImGuiKey_D, false))
        { std::printf("[keys] D pressed: shift=%d ctrl=%d mode=%d\n", io.KeyShift ? 1 : 0, io.KeyCtrl ? 1 : 0, (int)mode3D); std::fflush(stdout); }
    if (!io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_D, false)) { DuplicateSelected(); return; }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
        // Object -> Sculpt -> Edit -> Retopology -> Pose -> Weight Paint -> Object
        static const Mode3D order[] = {Mode3D::Object, Mode3D::Sculpt, Mode3D::Edit, Mode3D::Retopology, Mode3D::Pose, Mode3D::Weight};
        int at = 0;
        for (int i = 0; i < 6; ++i) if (order[i] == mode3D) at = i;
        SetMode3D(order[(at + 1) % 6]);
        ShowStatus(std::string(Mode3DName(mode3D)) + " Mode");
        return;
    }
    // Every Ctrl combo lives here; after this point only plain keys are handled, so Ctrl+S (save)
    // cannot also start a Scale, Ctrl+J cannot also do something else, and so on.
    if (io.KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_J, false)) JoinMeshes();
        if (mode3D == Mode3D::Edit && ImGui::IsKeyPressed(ImGuiKey_E, false)) openEdgeMenu = true;
        return;
    }
    if (mode3D == Mode3D::Object || mode3D == Mode3D::Edit) {
        if (ImGui::IsKeyPressed(ImGuiKey_G, false)) { BeginTransform(0); return; }
        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) { BeginTransform(1); return; }
        if (ImGui::IsKeyPressed(ImGuiKey_S, false) && mode3D == Mode3D::Object) { BeginTransform(2); return; }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_I, false) && !io.KeyCtrl && !io.KeyShift && scene.Active()) {
        InsertKeyframes(true, true, true);
        return;
    }
    if (mode3D == Mode3D::Edit && scene.Active() && scene.Active()->isCurve) {
        if (!io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_E, false)) CurveEditOp('E');
        if (!io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_X, false)) CurveEditOp('X');
        return;
    }
    if (mode3D == Mode3D::Edit) {
        if (ImGui::IsKeyPressed(ImGuiKey_U, false) && !io.KeyCtrl) { UnwrapSelected(); return; }
        if (!io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_E, false)) { RunEditOp('E'); return; }
        if (!io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_X, false)) { RunEditOp('X'); return; }
        if (!io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_M, false)) { RunEditOp('M'); return; }
        if (!io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_P, false)) { SeparateSelected(); return; }
    }
    if (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_T, false) && scene.Active()) {
        RequestTextureSetImport();   // Node Wrangler style texture setup
        return;
    }
    if (mode3D == Mode3D::Object && ImGui::IsKeyPressed(ImGuiKey_X, false) && scene.Active()) {
        int idx = scene.activeIndex;
        std::string name = scene.Active()->name;
        ShowStatus("Deleted " + name);
        MeshObject* obj = scene.Active();
        PushObjectListUndo("Delete " + name, obj, idx, scene.Take(idx));
        return;
    }

    auto key = [](char c) { return static_cast<ImGuiKey>(ImGuiKey_A + (c - 'A')); };
    if (mode3D == Mode3D::Edit) {
        const ImGuiKey nums[] = {ImGuiKey_1, ImGuiKey_2, ImGuiKey_3};
        for (int i = 0; i < 3; ++i)
            if (ImGui::IsKeyPressed(nums[i], false)) {
                int bit = 1 << i;
                if (io.KeyShift) { edit.selectMode ^= bit; if (!edit.selectMode) edit.selectMode = bit; }
                else edit.selectMode = bit;
            }
    } else if (mode3D == Mode3D::Sculpt) {
        for (int i = 0; i < kSculptBrushCount; ++i) {
            const char* sc = kSculptBrushes[i].shortcut;
            if (!sc[0]) continue;
            bool shift = std::string(sc).rfind("Shift+", 0) == 0;
            char letter = sc[shift ? 6 : 0];
            if (shift == io.KeyShift && letter >= 'A' && letter <= 'Z' && ImGui::IsKeyPressed(key(letter), false)) {
                sculpt.brush = i; sculpt.strength = kSculptBrushes[i].strength;
            }
        }
    } else if (mode3D == Mode3D::Retopology) {
        if (ImGui::IsKeyPressed(ImGuiKey_Q, false)) retopo.tool = 0;
        const ImGuiKey nums[] = {ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, ImGuiKey_4, ImGuiKey_5, ImGuiKey_6, ImGuiKey_7, ImGuiKey_8, ImGuiKey_9};
        for (int i = 0; i < 9 && i + 1 < kRetopoToolCount; ++i)
            if (ImGui::IsKeyPressed(nums[i], false)) retopo.tool = i + 1;
    }
}

// ---------------------------------------------------------------- 3D Tool Settings panel
void App::Tool3DPanel() {
    if (!ImGui::Begin("3D Tool Settings", &showTool3D)) { ImGui::End(); return; }
    // Mode switch row
    const float b = 30.0f;
    for (Mode3D m : {Mode3D::Object, Mode3D::Sculpt, Mode3D::Edit, Mode3D::Retopology, Mode3D::Pose, Mode3D::Weight}) {
        ImGui::PushID(static_cast<int>(m));
        char tip[48]; std::snprintf(tip, sizeof(tip), "%s Mode%s", Mode3DName(m), m == Mode3D::Edit ? " (Tab)" : "");
        if (IconButton("##m", ModeIcon(m), mode3D == m, tip, b)) SetMode3D(m);
        ImGui::SameLine();
        ImGui::PopID();
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(kBrass, "%s Mode", Mode3DName(mode3D));
    ImGui::Separator();

    ImGui::PushItemWidth(-130.0f);
    switch (mode3D) {
        case Mode3D::Object:     ObjectSettingsUI(); break;
        case Mode3D::Sculpt:     SculptSettingsUI(); break;
        case Mode3D::Edit:       EditSettingsUI(); break;
        case Mode3D::Retopology: RetopoSettingsUI(); break;
        case Mode3D::Pose:
            ImGui::TextColored(kBrass, "Pose Mode");
            Hint("Select a bone in the Armature panel, pose it, then press I to keyframe it.");
            break;
        case Mode3D::Weight:     WeightSettingsUI(); break;
    }
    ImGui::PopItemWidth();
    ImGui::Spacing();
    Hint("Tools and settings are live; the mesh engine that applies them arrives in Milestone 2.");
    ImGui::End();
}

static const char* kPivots[] = {"Bounding Box Center", "3D Cursor", "Individual Origins", "Median Point", "Active Element"};
static const char* kOrientations[] = {"Global", "Local", "Normal", "Gimbal", "View", "Cursor", "Parent"};
static const char* kFalloffs[] = {"Smooth", "Sphere", "Root", "Inverse Square", "Sharp", "Linear", "Constant", "Random"};

void App::ObjectSettingsUI() {
    CurveSettingsUI();
    const auto& t = kObjectTools[objectTool];
    ImGui::Text("%s", t.name); Hint(t.hint);
    Section("TRANSFORM");
    static int pivot = 3, orient = 0;
    ImGui::Combo("Pivot Point", &pivot, kPivots, IM_COUNTOF(kPivots));
    ImGui::Combo("Orientation", &orient, kOrientations, IM_COUNTOF(kOrientations));
}

// Blender falloff presets, evaluated at x = 1 - distance/radius
static float FalloffCurve(int preset, float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    switch (preset) {
        case 0: return x * x * (3 - 2 * x);                              // Smooth
        case 1: return x * x * x * (x * (x * 6 - 15) + 10);              // Smoother
        case 2: return std::sqrt(std::max(0.0f, 2 * x - x * x));         // Sphere
        case 3: return std::sqrt(x);                                     // Root
        case 4: return x * x;                                            // Sharp
        case 5: return x;                                                // Linear
        case 6: return x * x * x * x;                                    // Sharper
        case 7: return x * (2 - x);                                      // Inverse Square
        default: return 1.0f;                                            // Constant
    }
}

void App::SculptSettingsUI() {
    const auto& br = kSculptBrushes[sculpt.brush];
    ImVec2 p = ImGui::GetCursorScreenPos();
    DrawIcon(ImGui::GetWindowDrawList(), br.icon, ImVec2(p.x + 14, p.y + 14), 26, ImGui::GetColorU32(kBrass));
    ImGui::Dummy(ImVec2(30, 28)); ImGui::SameLine();
    ImGui::BeginGroup(); ImGui::Text("%s", br.name); ImGui::TextDisabled("%s", BrushCategoryName(br.category)); ImGui::EndGroup();
    Hint(br.hint);

    Section("BRUSH");
    if (br.hasDirection) {
        int dir = sculpt.subtract ? 1 : 0;
        ImGui::RadioButton("Add", &dir, 0); ImGui::SameLine(); ImGui::RadioButton("Subtract", &dir, 1);
        sculpt.subtract = dir == 1;
        ImGui::SameLine(); ImGui::TextDisabled("(Ctrl inverts)");
    }
    ImGui::SliderFloat("Radius", &sculpt.radiusPx, 1, 500, sculpt.radiusInSceneUnits ? "%.2f m" : "%.0f px");
    ImGui::Checkbox("Radius in scene units", &sculpt.radiusInSceneUnits);
    ImGui::SliderFloat("Strength", &sculpt.strength, 0, 1, "%.3f");
    ImGui::SliderFloat("Hardness", &sculpt.hardness, 0, 1, "%.2f");
    ImGui::SliderFloat("Normal Radius", &sculpt.normalRadius, 0, 1, "%.2f");
    ImGui::SliderFloat("Auto-Smooth", &sculpt.autoSmooth, 0, 1, "%.2f");
    ImGui::Checkbox("Front Faces Only", &sculpt.frontFacesOnly);

    const Icon ic = br.icon;
    bool planeBrush = ic == Icon::SculptClay || ic == Icon::SculptClayStrips || ic == Icon::SculptClayThumb || ic == Icon::SculptFlatten ||
                      ic == Icon::SculptFill || ic == Icon::SculptScrape || ic == Icon::SculptMultiScrape || ic == Icon::SculptPolish ||
                      ic == Icon::SculptPlanarTrim || ic == Icon::SculptLayer;
    if (planeBrush && ImGui::CollapsingHeader("Plane", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Plane Offset", &sculpt.planeOffset, -1, 1, "%.2f");
        ImGui::Checkbox("Plane Trim", &sculpt.useTrim);
        ImGui::BeginDisabled(!sculpt.useTrim); ImGui::SliderFloat("Trim Distance", &sculpt.planeTrim, 0, 1, "%.2f"); ImGui::EndDisabled();
    }
    if ((ic == Icon::SculptCloth || ic == Icon::SculptClothFolds) && ImGui::CollapsingHeader("Cloth", ImGuiTreeNodeFlags_DefaultOpen)) {
        static const char* deform[] = {"Drag", "Push", "Pinch Point", "Pinch Perpendicular (Folds)", "Inflate", "Grab", "Expand", "Snake Hook"};
        if (ic == Icon::SculptClothFolds && sculpt.clothDeformation == 0) sculpt.clothDeformation = 3;
        ImGui::Combo("Deformation", &sculpt.clothDeformation, deform, IM_COUNTOF(deform));
        static const char* ff[] = {"Radial", "Plane"};
        ImGui::Combo("Force Falloff", &sculpt.clothFalloff, ff, IM_COUNTOF(ff));
        ImGui::SliderFloat("Mass", &sculpt.clothMass, 0.01f, 2.0f, "%.2f");
        ImGui::SliderFloat("Damping", &sculpt.clothDamping, 0.0f, 1.0f, "%.3f");
        static const char* area[] = {"Local", "Global", "Dynamic"};
        ImGui::Combo("Simulation Area", &sculpt.clothSimArea, area, IM_COUNTOF(area));
        ImGui::Checkbox("Persistent", &sculpt.clothPersistent);
    }
    if (ic == Icon::SculptPose && ImGui::CollapsingHeader("Pose", ImGuiTreeNodeFlags_DefaultOpen)) {
        static const char* origin[] = {"Topology", "Face Sets", "Face Sets FK"};
        ImGui::Combo("Rotation Origins", &sculpt.poseOrigin, origin, IM_COUNTOF(origin));
        ImGui::SliderInt("Segments", &sculpt.poseSegments, 1, 20);
        ImGui::SliderInt("Smooth Iterations", &sculpt.poseSmooth, 0, 10);
    }
    if ((ic == Icon::SculptGrab || ic == Icon::SculptElastic) && ImGui::CollapsingHeader("Grab", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Grab Active Vertex", &sculpt.grabActiveVertex);
        ImGui::Checkbox("Grab Silhouette", &sculpt.grabSilhouette);
    }
    if (ic == Icon::SculptSnakeHook && ImGui::CollapsingHeader("Snake Hook", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Rake", &sculpt.snakeRake, 0, 1, "%.2f");
        ImGui::SliderFloat("Magnify / Pinch", &sculpt.snakePinch, 0, 1, "%.2f");
    }
    if (ic == Icon::SculptWrinkle && ImGui::CollapsingHeader("Wrinkle", ImGuiTreeNodeFlags_DefaultOpen))
        ImGui::SliderFloat("Wrinkle Count", &sculpt.wrinkleCount, 1, 20, "%.0f");
    if (ic == Icon::SculptNoise && ImGui::CollapsingHeader("Noise", ImGuiTreeNodeFlags_DefaultOpen))
        ImGui::SliderFloat("Noise Scale", &sculpt.noiseScale, 0.01f, 2.0f, "%.2f");

    if (ic == Icon::SculptCustomImage && ImGui::CollapsingHeader("Custom Image Brush", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (brushImage.Loaded()) {
            float w = std::min(ImGui::GetContentRegionAvail().x, 180.0f);
            float h = w * brushImage.height / std::max(1, brushImage.width);
            ImGui::Image(brushImage.heightPreview.Id(), ImVec2(w, h));
            ImGui::TextWrapped("%s  (%d x %d)", brushImage.name.c_str(), brushImage.width, brushImage.height);
            Hint("White raises the surface, black leaves it untouched (luminance x alpha).");
        } else {
            Hint("No image yet. Import a PNG/JPG/TGA/BMP height stamp.");
        }
        if (ImGui::Button(brushImage.Loaded() ? "Replace Image..." : "Import Image...", ImVec2(-1, 0))) RequestBrushImageImport();
        static const char* mapping[] = {"View Plane", "Area Plane", "Tiled", "Random", "Stencil"};
        ImGui::Combo("Mapping", &sculpt.imageMapping, mapping, IM_COUNTOF(mapping));
        ImGui::SliderFloat("Angle", &sculpt.imageAngle, 0, 360, "%.0f deg");
        ImGui::Checkbox("Random Angle", &sculpt.imageRandomAngle);
        ImGui::SliderFloat("Scale", &sculpt.imageScale, 0.1f, 4.0f, "%.2f");
        ImGui::SliderFloat("Mid-level", &sculpt.imageMidLevel, 0, 1, "%.2f");
        ImGui::Checkbox("Invert", &sculpt.imageInvert);
        ImGui::Checkbox("Show image in 2D viewport", &sculpt.showImageIn2D);
    }

    if (ImGui::CollapsingHeader("Falloff")) {
        static const char* shapes[] = {"Sphere", "Projected"};
        ImGui::Combo("Shape", &sculpt.falloffShape, shapes, IM_COUNTOF(shapes));
        static const char* curves[] = {"Smooth", "Smoother", "Sphere", "Root", "Sharp", "Linear", "Sharper", "Inverse Square", "Constant"};
        ImGui::Combo("Curve", &sculpt.falloffCurve, curves, IM_COUNTOF(curves));
        ImVec2 a = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x, h = 60.0f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(a, ImVec2(a.x + w, a.y + h), IM_COL32(20, 23, 28, 255), 4.0f);
        ImVec2 pts[64];
        for (int i = 0; i < 64; ++i) {
            float t = i / 63.0f; // t = distance from center
            pts[i] = ImVec2(a.x + 4 + t * (w - 8), a.y + h - 4 - FalloffCurve(sculpt.falloffCurve, 1.0f - t) * (h - 8));
        }
        dl->AddPolyline(pts, 64, ImGui::GetColorU32(kBrass), 0, 2.0f);
        ImGui::Dummy(ImVec2(w, h));
    }
    if (ImGui::CollapsingHeader("Stroke")) {
        static const char* methods[] = {"Space", "Dots", "Drag Dot", "Anchored", "Airbrush", "Line", "Curve"};
        ImGui::Combo("Stroke Method", &sculpt.strokeMethod, methods, IM_COUNTOF(methods));
        ImGui::SliderFloat("Spacing", &sculpt.spacing, 0.01f, 5.0f, "%.2f");
        ImGui::Checkbox("Stabilize Stroke", &sculpt.stabilize);
        ImGui::BeginDisabled(!sculpt.stabilize);
        ImGui::SliderFloat("Radius##stab", &sculpt.stabilizeRadius, 10, 200, "%.0f px");
        ImGui::SliderFloat("Factor", &sculpt.stabilizeFactor, 0, 1, "%.2f");
        ImGui::EndDisabled();
    }
    if (ImGui::CollapsingHeader("Symmetry")) {
        ImGui::Text("Mirror"); ImGui::SameLine(110); Toggle("X##m", &sculpt.symX, 32); ImGui::SameLine(); Toggle("Y##m", &sculpt.symY, 32); ImGui::SameLine(); Toggle("Z##m", &sculpt.symZ, 32);
        ImGui::Text("Lock");   ImGui::SameLine(110); Toggle("X##l", &sculpt.lockX, 32); ImGui::SameLine(); Toggle("Y##l", &sculpt.lockY, 32); ImGui::SameLine(); Toggle("Z##l", &sculpt.lockZ, 32);
    }
    if (ImGui::CollapsingHeader("Auto-Masking")) {
        ImGui::Checkbox("Topology", &sculpt.maskTopology);
        ImGui::Checkbox("Face Sets", &sculpt.maskFaceSets);
        ImGui::Checkbox("Mesh Boundary", &sculpt.maskBoundary);
        ImGui::Checkbox("Face Sets Boundary", &sculpt.maskFaceSetBoundary);
        ImGui::Checkbox("Cavity", &sculpt.maskCavity);
        ImGui::Checkbox("View Normal", &sculpt.maskViewNormal);
    }
    if (ImGui::CollapsingHeader("Dyntopo")) {
        ImGui::Checkbox("Dynamic Topology", &sculpt.dyntopo);
        ImGui::BeginDisabled(!sculpt.dyntopo);
        static const char* detailing[] = {"Relative Detail", "Constant Detail", "Brush Detail", "Manual Detail"};
        ImGui::Combo("Detailing", &sculpt.detailing, detailing, IM_COUNTOF(detailing));
        ImGui::SliderFloat("Detail Size", &sculpt.detailSize, 0.5f, 40.0f, "%.1f px");
        static const char* refine[] = {"Subdivide Edges", "Collapse Edges", "Subdivide Collapse"};
        ImGui::Combo("Refine Method", &sculpt.refineMethod, refine, IM_COUNTOF(refine));
        ImGui::EndDisabled();
    }
    if (ImGui::CollapsingHeader("Remesh")) {
        ImGui::SliderFloat("Voxel Size", &sculpt.voxelSize, 0.001f, 1.0f, "%.3f m", ImGuiSliderFlags_Logarithmic);
        ImGui::BeginDisabled(); ImGui::Button("Remesh (Ctrl+R)", ImVec2(-1, 0)); ImGui::EndDisabled();
    }
}

void App::EditSettingsUI() {
    if (scene.Active() && scene.Active()->isCurve) { CurveSettingsUI(); return; }
    const auto& t = kEditTools[edit.tool];
    ImVec2 p = ImGui::GetCursorScreenPos();
    DrawIcon(ImGui::GetWindowDrawList(), t.icon, ImVec2(p.x + 14, p.y + 14), 26, ImGui::GetColorU32(kBrass));
    ImGui::Dummy(ImVec2(30, 28)); ImGui::SameLine();
    ImGui::BeginGroup(); ImGui::Text("%s", t.name); if (t.shortcut[0]) ImGui::TextDisabled("%s", t.shortcut); ImGui::EndGroup();
    Hint(t.hint);

    if (edit.selectMode & (int)SelectMode::Edge) {
        Section("EDGE DATA");
        bool can = EdgeOpsAvailable();
        ImGui::BeginDisabled(!can);
        if (ImGui::Button("Mark Seam")) MarkEdges(true, true);
        ImGui::SameLine();
        if (ImGui::Button("Clear Seam")) MarkEdges(true, false);
        if (ImGui::Button("Mark Sharp")) MarkEdges(false, true);
        ImGui::SameLine();
        if (ImGui::Button("Clear Sharp")) MarkEdges(false, false);
        ImGui::EndDisabled();
        if (!can) ImGui::TextDisabled("Select edges first.");
    }

    Section("SELECT MODE");
    struct { int bit; Icon icon; const char* name; } sm[] = {{1, Icon::SelVertex, "Vertex"}, {2, Icon::SelEdge, "Edge"}, {4, Icon::SelFace, "Face"}};
    for (auto& m : sm) {
        ImGui::PushID(m.bit);
        char tip[64]; std::snprintf(tip, sizeof(tip), "%s select  -  Shift+click to combine", m.name);
        if (IconButton("##sm", m.icon, (edit.selectMode & m.bit) != 0, tip, 30)) {
            if (ImGui::GetIO().KeyShift) { edit.selectMode ^= m.bit; if (!edit.selectMode) edit.selectMode = m.bit; }
            else edit.selectMode = m.bit;
        }
        ImGui::SameLine();
        ImGui::PopID();
    }
    ImGui::NewLine();

    // Tool-specific options (Blender's "Adjust Last Operation" / tool header values)
    switch (t.icon) {
        case Icon::Extrude: Section("EXTRUDE"); ImGui::Checkbox("Dissolve Orthogonal Edges", &edit.extrudeDissolve); break;
        case Icon::Inset:
            Section("INSET");
            ImGui::SliderFloat("Thickness", &edit.insetThickness, 0, 1, "%.3f m", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("Depth", &edit.insetDepth, -1, 1, "%.3f m");
            ImGui::Checkbox("Boundary", &edit.insetBoundary); ImGui::Checkbox("Edge Rail", &edit.insetEdgeRail);
            ImGui::Checkbox("Individual", &edit.insetIndividual);
            break;
        case Icon::Bevel: {
            Section("BEVEL");
            static const char* wt[] = {"Offset", "Width", "Depth", "Percent", "Absolute"};
            ImGui::Combo("Width Type", &edit.bevelWidthType, wt, IM_COUNTOF(wt));
            ImGui::SliderFloat("Width", &edit.bevelWidth, 0, 1, "%.3f m", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderInt("Segments", &edit.bevelSegments, 1, 32);
            ImGui::SliderFloat("Shape", &edit.bevelShape, 0, 1, "%.2f");
            static const char* aff[] = {"Vertices", "Edges"};
            ImGui::Combo("Affect", &edit.bevelAffect, aff, IM_COUNTOF(aff));
            ImGui::Checkbox("Clamp Overlap", &edit.bevelClamp); ImGui::Checkbox("Loop Slide", &edit.bevelLoopSlide);
            break;
        }
        case Icon::LoopCut:
            Section("LOOP CUT AND SLIDE");
            ImGui::SliderInt("Number of Cuts", &edit.loopCuts, 1, 64);
            ImGui::SliderFloat("Smoothness", &edit.loopSmoothness, -4, 4, "%.2f");
            ImGui::Checkbox("Even", &edit.loopEven); ImGui::Checkbox("Flip", &edit.loopFlip);
            break;
        case Icon::Knife: {
            Section("KNIFE");
            ImGui::Checkbox("Occlude Geometry", &edit.knifeOcclude); ImGui::Checkbox("Only Selected", &edit.knifeOnlySelected);
            ImGui::Checkbox("Cut Through (Z)", &edit.knifeCutThrough);
            static const char* snap[] = {"None", "Screen", "Relative"};
            ImGui::Combo("Angle Snapping", &edit.knifeAngleSnap, snap, IM_COUNTOF(snap));
            break;
        }
        case Icon::PolyBuild: Section("POLY BUILD"); ImGui::Checkbox("Create Quads", &edit.polyBuildQuads); break;
        case Icon::Spin:
            Section("SPIN");
            ImGui::SliderInt("Steps", &edit.spinSteps, 1, 128);
            ImGui::SliderFloat("Angle", &edit.spinAngle, -360, 360, "%.0f deg");
            ImGui::Checkbox("Auto Merge", &edit.spinAutoMerge);
            break;
        default: break;
    }

    Section("TRANSFORM");
    ImGui::Combo("Pivot Point", &edit.pivot, kPivots, IM_COUNTOF(kPivots));
    ImGui::Combo("Orientation", &edit.orientation, kOrientations, IM_COUNTOF(kOrientations));

    if (ImGui::CollapsingHeader("Proportional Editing (O)")) {
        ImGui::Checkbox("Enabled##prop", &edit.proportional);
        ImGui::BeginDisabled(!edit.proportional);
        ImGui::Combo("Falloff", &edit.propFalloff, kFalloffs, IM_COUNTOF(kFalloffs));
        ImGui::SliderFloat("Size", &edit.propSize, 0.001f, 100.0f, "%.3f m", ImGuiSliderFlags_Logarithmic);
        ImGui::Checkbox("Connected Only", &edit.propConnected);
        ImGui::Checkbox("Projected from View", &edit.propProjected);
        ImGui::EndDisabled();
    }
    if (ImGui::CollapsingHeader("Snapping")) {
        ImGui::Checkbox("Enabled##snap", &edit.snapping);
        ImGui::BeginDisabled(!edit.snapping);
        static const char* targets[] = {"Increment", "Vertex", "Edge", "Face Project", "Face Nearest", "Volume", "Edge Center", "Edge Perpendicular"};
        ImGui::Combo("Snap To", &edit.snapTarget, targets, IM_COUNTOF(targets));
        static const char* bases[] = {"Closest", "Center", "Median", "Active"};
        ImGui::Combo("Snap Base", &edit.snapBase, bases, IM_COUNTOF(bases));
        ImGui::Text("Affect"); ImGui::SameLine(110); Toggle("Move", &edit.snapMove); ImGui::SameLine(); Toggle("Rotate", &edit.snapRotate); ImGui::SameLine(); Toggle("Scale", &edit.snapScale);
        ImGui::EndDisabled();
    }
    if (ImGui::CollapsingHeader("Mesh Options")) {
        ImGui::Checkbox("Auto Merge Vertices", &edit.autoMerge);
        ImGui::BeginDisabled(!edit.autoMerge);
        ImGui::SliderFloat("Threshold", &edit.mergeThreshold, 0.00001f, 0.1f, "%.5f m", ImGuiSliderFlags_Logarithmic);
        ImGui::Checkbox("Split Edges & Faces", &edit.splitEdges);
        ImGui::EndDisabled();
        ImGui::Checkbox("Mirror X", &edit.mirrorX);
        ImGui::Checkbox("Topology Mirror", &edit.mirrorTopology);
        ImGui::Checkbox("Correct Face Attributes (UVs)", &edit.correctUVs);
    }
    if (ImGui::CollapsingHeader("Overlays")) {
        ImGui::Checkbox("X-ray (Alt+Z)", &edit.xray);
        ImGui::BeginDisabled(!edit.xray); ImGui::SliderFloat("X-ray Alpha", &edit.xrayAlpha, 0, 1, "%.2f"); ImGui::EndDisabled();
        ImGui::Checkbox("Face Orientation", &edit.faceOrientation);
        if (edit.faceOrientation) ImGui::TextDisabled("  blue = facing out, red = flipped");
        ImGui::Checkbox("Statistics", &edit.statistics);
        ImGui::Checkbox("Edge Length", &edit.edgeLength);
        ImGui::Checkbox("Edge Angle", &edit.edgeAngle);
        static const char* normals[] = {"Off", "Vertex", "Split", "Face"};
        ImGui::Combo("Normals", &edit.normalsDisplay, normals, IM_COUNTOF(normals));
        ImGui::BeginDisabled(edit.normalsDisplay == 0); ImGui::SliderFloat("Normal Size", &edit.normalsSize, 0.001f, 1.0f, "%.3f"); ImGui::EndDisabled();
    }
}

void App::RetopoSettingsUI() {
    const auto& t = kRetopoTools[retopo.tool];
    ImVec2 p = ImGui::GetCursorScreenPos();
    DrawIcon(ImGui::GetWindowDrawList(), t.icon, ImVec2(p.x + 14, p.y + 14), 26, ImGui::GetColorU32(kBrass));
    ImGui::Dummy(ImVec2(30, 28)); ImGui::SameLine();
    ImGui::BeginGroup(); ImGui::Text("%s", t.name); if (t.shortcut[0]) ImGui::TextDisabled("Key %s", t.shortcut); ImGui::EndGroup();
    Hint(t.hint);

    Section("SOURCE");
    ImGui::BeginDisabled();
    static int source = 0; static const char* none[] = {"(load a high-poly mesh)"};
    ImGui::Combo("High-poly Source", &source, none, 1);
    ImGui::EndDisabled();
    ImGui::Checkbox("Snap to Source Surface", &retopo.snapToSource);
    ImGui::SliderFloat("Surface Offset", &retopo.surfaceOffset, 0, 0.05f, "%.4f m");

    switch (t.icon) {
        case Icon::QuadDraw:
            Section("QUAD DRAW (MAYA STYLE)");
            ImGui::Checkbox("Auto Weld", &retopo.autoWeld);
            ImGui::BeginDisabled(!retopo.autoWeld); ImGui::SliderFloat("Weld Distance", &retopo.weldDistance, 1, 40, "%.0f px"); ImGui::EndDisabled();
            ImGui::Checkbox("Relax Interior Vertices", &retopo.relaxInterior);
            ImGui::Checkbox("Relax Border Vertices", &retopo.relaxBorder);
            if (ImGui::BeginTable("##qd", 2, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
                const char* rows[][2] = {
                    {"Click", "Place a dot on the source surface"},
                    {"Shift+Click", "Fill a quad from 4 dots or edges"},
                    {"Ctrl+Click", "Insert an edge loop"},
                    {"Ctrl+Shift+Click", "Delete an edge loop or face"},
                    {"Tab+Drag", "Extend a strip of quads"},
                    {"Shift+Drag", "Relax vertices"},
                    {"Ctrl+Drag", "Move a vertex, edge or face"},
                };
                for (auto& r : rows) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::TextColored(kBrass, "%s", r[0]);
                    ImGui::TableSetColumnIndex(1); ImGui::TextWrapped("%s", r[1]);
                }
                ImGui::EndTable();
            }
            break;
        case Icon::Contours:
            Section("CONTOURS");
            ImGui::SliderInt("Cut Count", &retopo.contourCuts, 3, 128);
            ImGui::Checkbox("Uniform Spacing", &retopo.contourUniform);
            break;
        case Icon::PolyStrips:
            Section("POLYSTRIPS");
            ImGui::SliderFloat("Brush Size", &retopo.stripBrushSize, 5, 200, "%.0f px");
            break;
        case Icon::Patches:
            Section("PATCHES");
            ImGui::SliderInt("Corner Angle", &retopo.patchFillAngle, 60, 170, "%d deg");
            ImGui::Checkbox("Allow Triangles", &retopo.patchAllowTris);
            break;
        case Icon::PolyPen: {
            Section("POLYPEN");
            static const char* modes[] = {"Tri / Quad", "Quad Only", "Tri Only", "Edge Only"};
            ImGui::Combo("Insert Mode", &retopo.polyPenMode, modes, IM_COUNTOF(modes));
            break;
        }
        case Icon::Tweak:
        case Icon::Relax:
        case Icon::SelectBrush:
            Section("BRUSH");
            ImGui::SliderFloat("Radius", &retopo.brushRadius, 5, 300, "%.0f px");
            ImGui::SliderFloat("Strength", &retopo.brushStrength, 0, 1, "%.2f");
            ImGui::Combo("Falloff", &retopo.brushFalloff, kFalloffs, IM_COUNTOF(kFalloffs) - 1);
            ImGui::Checkbox("Mask Boundary", &retopo.maskBoundary);
            ImGui::Checkbox("Mask Selected", &retopo.maskSelected);
            break;
        default: break;
    }

    if (ImGui::CollapsingHeader("Symmetry", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Mirror"); ImGui::SameLine(110); Toggle("X##rs", &retopo.symX, 32); ImGui::SameLine(); Toggle("Y##rs", &retopo.symY, 32); ImGui::SameLine(); Toggle("Z##rs", &retopo.symZ, 32);
        ImGui::SliderFloat("Mirror Threshold", &retopo.mirrorThreshold, 0.00001f, 0.1f, "%.5f m", ImGuiSliderFlags_Logarithmic);
    }
    if (ImGui::CollapsingHeader("Display")) {
        static const char* disp[] = {"Overlay", "Hidden Line", "X-ray"};
        ImGui::Combo("Display", &retopo.display, disp, IM_COUNTOF(disp));
        ImGui::SliderFloat("Overlay Opacity", &retopo.overlayOpacity, 0, 1, "%.2f");
        ImGui::Checkbox("Backface Culling", &retopo.backfaceCulling);
    }
}

// ---------------------------------------------------------------- custom image brush import
static void SDLCALL OnBrushFileChosen(void* userdata, const char* const* filelist, int) {
    auto* app = static_cast<App*>(userdata);
    if (!filelist) { app->QueueBrushImport(std::string("!") + SDL_GetError()); return; } // "!" marks an error
    if (!filelist[0]) return; // cancelled
    app->QueueBrushImport(filelist[0]);
}

void App::RequestBrushImageImport() {
    static const SDL_DialogFileFilter filters[] = {
        {"Images", "png;jpg;jpeg;tga;bmp;psd;gif"},
        {"All files", "*"},
    };
    SDL_ShowOpenFileDialog(OnBrushFileChosen, this, window, filters, 2, nullptr, false);
}

void App::QueueBrushImport(const std::string& path) {
    std::lock_guard<std::mutex> lock(importMutex);
    if (!path.empty() && path[0] == '!') pendingImportError = path.substr(1);
    else { pendingImportPath = path; queuedKind = ImportKind::BrushImage; }
}

void App::ProcessPendingImport() {
    std::string path, dialogError, exportPath, projSave, projOpen, hdriPending;
    std::vector<std::string> textures;
    ImportKind kind = ImportKind::None;
    {
        std::lock_guard<std::mutex> lock(importMutex);
        textures.swap(pendingTextureFiles);
        projSave.swap(pendingProjectSave);
        hdriPending.swap(pendingHdri);
        projOpen.swap(pendingProjectOpen);
        path.swap(pendingImportPath);
        dialogError.swap(pendingImportError);
        exportPath.swap(pendingExportPath);
        kind = queuedKind; queuedKind = ImportKind::None;
    }
    if (!dialogError.empty()) ShowStatus("File dialog failed: " + dialogError);
    if (!textures.empty()) ApplyTextureFiles(textures);
    if (!hdriPending.empty()) {
        std::string err;
        if (hdri.Load(hdriPending, err)) {
            hdriPath = hdriPending;
            hdriPreset = -1;
            hdriBgDirty = true;
            size_t slash = hdriPath.find_last_of("/\\");
            ShowStatus("Loaded environment " + (slash == std::string::npos ? hdriPath : hdriPath.substr(slash + 1)) +
                       " (" + std::to_string(hdri.width) + " x " + std::to_string(hdri.height) + ")");
        } else ShowStatus("Could not load the environment image: " + err);
    }

    if (!projSave.empty()) {
        if (projSave.size() < 7 || projSave.substr(projSave.size() - 7) != ".leorix") projSave += ".leorix";
        ProjectSaveInfo r = SaveLeorixProject(projSave, paint, brush2D, visualStarted ? SaveGraph(visualGraph) : std::string(),
                                                   &scene, &playback);
        if (r.ok) { projectPath = projSave; paint.name = projSave; }
        ShowStatus(r.message);
    }
    if (!projOpen.empty()) {
        std::string blocks;
        SetMode3D(Mode3D::Object);   // edit/sculpt/pose state refers to objects that may be replaced
        ProjectSaveInfo r = LoadLeorixProject(projOpen, paint, brush2D, vk, &blocks, &scene, &playback);
        if (r.ok) {
            scriptLiveObj = visualLiveObj = nullptr;
            activeBone = -1;
            uvLayout.valid = false;
            for (auto& o : scene.objects) UpdateSkinning(*o);
            if (!blocks.empty()) {
                visualMessageOk = LoadGraph(visualGraph, blocks, visualMessage);
                visualStarted = visualStarted || visualMessageOk;
            }
            projectPath = projOpen;
            canvasW = paint.width; canvasH = paint.height;
            undoStack.Clear();
            showNewProject = false;
        }
        ShowStatus(r.message);
    }

    if (!exportPath.empty() && scene.HasMesh() && scene.Active()->isControl) {
        ShowStatus("Rig controls are only wire handles; select a mesh to export");
    } else if (!exportPath.empty() && scene.HasMesh()) {
        std::string error;
        const MeshObject* eo = scene.Active();
        Mesh linkedMesh; Armature linkedArm; const Action* linkedAct = nullptr;   // a mesh deformed by another object's bones
        const bool linked = LinkedRigForExport(scene, *eo, linkedMesh, linkedArm, &linkedAct);
        if (ExportMeshAnimated(exportPath, linked ? linkedMesh : eo->mesh, linked ? &linkedArm : &eo->armature,
                               linked ? linkedAct : &eo->action, playback.fps, error)) {
            size_t slash = exportPath.find_last_of("/\\");
            ShowStatus("Exported " + (slash == std::string::npos ? exportPath : exportPath.substr(slash + 1)));
        } else ShowStatus(error);
    }
    if (path.empty()) return;

    if (kind == ImportKind::Mesh) {
        std::string error; Mesh m;
        if (!ImportMesh(path, m, error)) { ShowStatus(error); return; }
        std::string label = m.name;
        size_t slash = path.find_last_of("/\\");
        if (slash != std::string::npos) label = path.substr(slash + 1);
        size_t dot = label.find_last_of('.');
        if (dot != std::string::npos) label = label.substr(0, dot);
        MeshObject* added = scene.AddMesh(std::move(m), label);
        PushObjectListUndo("Import " + added->name, added, scene.activeIndex);
        showViewport3D = true;
        ShowStatus("Imported " + added->name + " (" + std::to_string(added->mesh.VertexCount()) + " verts)");
        return;
    }

    // brush image
    std::string error;
    if (!brushImage.Load(vk, path, error)) { ShowStatus(error); return; }
    for (int i = 0; i < kSculptBrushCount; ++i)
        if (kSculptBrushes[i].icon == Icon::SculptCustomImage) sculpt.brush = i;
    SetMode3D(Mode3D::Sculpt);
    showTool3D = true;
    if (!showCanvas2D) { showCanvas2D = true; resetLayout = true; }
    sculpt.showImageIn2D = true;
    ShowStatus("Loaded sculpt brush " + brushImage.name);
}

// ---------------------------------------------------------------- Add Primitive (Blender's Shift+A menu)
void App::AddPrimitiveMenu() {
    if (showAddPrimitive && !ImGui::IsPopupOpen("Add Primitive")) ImGui::OpenPopup("Add Primitive");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Add Primitive", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) return;

    static Primitive prim = Primitive::Cube;
    static PrimitiveParams p = DefaultParams(Primitive::Cube);
    struct Item { Primitive t; const char* name; };
    static const Item items[] = {
        {Primitive::Plane, "Plane"}, {Primitive::Cube, "Cube"}, {Primitive::Circle, "Circle"},
        {Primitive::UVSphere, "UV Sphere"}, {Primitive::IcoSphere, "Ico Sphere"}, {Primitive::Cylinder, "Cylinder"},
        {Primitive::Cone, "Cone"}, {Primitive::Torus, "Torus"}, {Primitive::Grid, "Grid"},
    };
    ImGui::TextColored(kBrass, "Clean topology - same as Blender's defaults, no subdivision.");
    ImGui::Spacing();
    const float cell = 118.0f;
    for (int i = 0; i < (int)(sizeof(items) / sizeof(items[0])); ++i) {
        if (i % 3) ImGui::SameLine();
        bool sel = prim == items[i].t;
        if (sel) { ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.54f, 0.37f, 0.22f, 1)); ImGui::PushStyleColor(ImGuiCol_Text, kBrass); }
        ImGui::PushID(i);
        if (ImGui::Button(items[i].name, ImVec2(cell, 30))) { prim = items[i].t; p = DefaultParams(prim); }
        ImGui::PopID();
        if (sel) ImGui::PopStyleColor(2);
    }
    ImGui::Separator();
    switch (prim) {
        case Primitive::Cube: case Primitive::Plane: ImGui::SliderFloat("Size", &p.size, 0.1f, 10.0f, "%.2f m"); break;
        case Primitive::Circle: ImGui::SliderInt("Vertices", &p.verticesRing, 3, 256); ImGui::SliderFloat("Radius", &p.radius, 0.1f, 10.0f, "%.2f m"); ImGui::Checkbox("Fill cap", &p.capFill); break;
        case Primitive::UVSphere: ImGui::SliderInt("Segments", &p.verticesRing, 3, 256); ImGui::SliderInt("Rings", &p.rings, 2, 256); ImGui::SliderFloat("Radius", &p.radius, 0.1f, 10.0f, "%.2f m"); break;
        case Primitive::IcoSphere: ImGui::SliderInt("Subdivisions", &p.subdivisions, 1, 6); ImGui::SliderFloat("Radius", &p.radius, 0.1f, 10.0f, "%.2f m"); break;
        case Primitive::Cylinder: case Primitive::Cone: ImGui::SliderInt("Vertices", &p.verticesRing, 3, 256); ImGui::SliderFloat("Radius", &p.radius, 0.1f, 10.0f, "%.2f m"); ImGui::SliderFloat("Depth", &p.depth, 0.1f, 10.0f, "%.2f m"); ImGui::Checkbox("Fill caps", &p.capFill); break;
        case Primitive::Torus: ImGui::SliderFloat("Major Radius", &p.radius, 0.1f, 10.0f, "%.2f m"); ImGui::SliderFloat("Minor Radius", &p.radius2, 0.05f, 5.0f, "%.2f m"); ImGui::SliderInt("Major Segments", &p.verticesRing, 3, 256); ImGui::SliderInt("Minor Segments", &p.rings, 3, 256); break;
        case Primitive::Grid: ImGui::SliderInt("X Subdivisions", &p.gridX, 1, 256); ImGui::SliderInt("Y Subdivisions", &p.gridY, 1, 256); ImGui::SliderFloat("Size", &p.size, 0.1f, 10.0f, "%.2f m"); break;
        default: break;
    }
    Mesh preview = BuildPrimitive(prim, p);
    ImGui::TextDisabled("%zu verts  -  %zu tris  -  %u polys", preview.VertexCount(), preview.TriCount(), preview.polyCount);
    ImGui::Separator();
    ImGui::TextColored(kBrass, "Curves");
    const float ccell = 118.0f;
    struct CurveItem { const char* name; int kind; };
    static const CurveItem curveItems[] = {{"Bezier Curve", 0}, {"Circle", 1}, {"Spiral", 2}, {"Path", 3}, {"NURBS Curve", 4}, {"NURBS Circle", 5}};
    for (int i = 0; i < 6; ++i) {
        if (i % 4) ImGui::SameLine();
        if (ImGui::Button(curveItems[i].name, ImVec2(ccell, 30))) {
            AddCurve(curveItems[i].kind);
            showAddPrimitive = false;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::Separator();
    ImGui::TextColored(kBrass, "Camera");
    if (ImGui::Button("Camera (from current view)", ImVec2(-1, 30))) {
        AddCameraAtView();
        showAddPrimitive = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::Separator();
    if (ImGui::Button("Armature (single bone)", ImVec2(-1, 0))) {
        AddArmatureObject();
        showAddPrimitive = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::Separator();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) { showAddPrimitive = false; ImGui::CloseCurrentPopup(); }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.54f, 0.37f, 0.22f, 1)); ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kBrass);
    if (ImGui::Button("Add", ImVec2(120, 0))) {
        MeshObject* added = scene.AddMesh(BuildPrimitive(prim, p), PrimitiveName(prim));
        PushObjectListUndo(std::string("Add ") + PrimitiveName(prim), added, scene.activeIndex);
        ShowStatus(std::string("Added ") + PrimitiveName(prim));
        showAddPrimitive = false; ImGui::CloseCurrentPopup();
    }
    ImGui::PopStyleColor(2);
    ImGui::EndPopup();
}

// ---------------------------------------------------------------- Scripting panel
void App::ScriptingPanel() {
    if (!ImGui::Begin("Leorix Scripting", &showScripting)) { ImGui::End(); return; }
    if (scriptCode.empty()) scriptCode = DefaultScriptExample();
    ImGui::TextColored(kBrass, "Lua - build meshes from parameters");
    ImGui::TextDisabled("Primitives: cube(size), sphere(r,seg,rings), ico_sphere(r,subdiv), cylinder, cone, torus, circle, grid, plane.");
    ImGui::TextDisabled("Manual: m=mesh(); a=m:vert(x,y,z); m:face(a,b,c[,d]); m:build()");

    static char buffer[1 << 16];
    if (ImGui::IsWindowAppearing()) { std::snprintf(buffer, sizeof(buffer), "%s", scriptCode.c_str()); }
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::InputTextMultiline("##code", buffer, sizeof(buffer), ImVec2(-1, avail.y * 0.55f), ImGuiInputTextFlags_AllowTabInput);
    bool run = false;
    if (ImGui::Button("Run  (Ctrl+Enter)", ImVec2(160, 0))) run = true;
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Enter, false)) run = true;
    ImGui::SameLine();
    if (ImGui::Button("Reset Example")) { std::snprintf(buffer, sizeof(buffer), "%s", DefaultScriptExample()); }
    ImGui::SameLine(); ImGui::Checkbox("Update every frame", &scriptLive);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Re-runs the script whenever the timeline moves and replaces the mesh it made,\nso scripts that use frame or time animate. Run once, then press Play.");
    ImGui::SameLine(); ImGui::TextDisabled("Each run adds a new object to the scene.");
    if (scriptLive && !run && playback.frame != scriptLastFrame && LiveObjectExists(scriptLiveObj)) {
        scriptLastFrame = playback.frame;
        ScriptResult r = RunMeshScript(scriptCode, playback.frame, playback.fps);
        if (r.ok && !r.meshes.empty()) {
            scriptLiveObj->mesh = MergeScriptMeshes(std::move(r.meshes));
            scriptLiveObj->mesh.EnsureUVs();
            scriptLiveObj->MarkTopoDirty();
            scriptLiveObj->stackDirty = true;
        } else scriptOutput = r.output;
    }
    if (run) {
        scriptCode = buffer;
        ScriptResult r = RunMeshScript(scriptCode, playback.frame, playback.fps);
        scriptOutput = r.output;
        scriptLastFrame = playback.frame;
        if (r.ok && !r.meshes.empty()) {
            scriptLiveObj = scene.AddMesh(MergeScriptMeshes(std::move(r.meshes)), "Script mesh");
            PushObjectListUndo("Run Script", scriptLiveObj, scene.activeIndex);
            showViewport3D = true;
            ShowStatus("Script created a mesh");
        }
    }
    ImGui::Separator();
    ImGui::TextColored(kBrass, "Output");
    ImGui::BeginChild("##out", ImVec2(-1, -1), ImGuiChildFlags_Borders);
    ImGui::TextUnformatted(scriptOutput.empty() ? "(no output yet)" : scriptOutput.c_str());
    ImGui::EndChild();
    ImGui::End();
}

// ---------------------------------------------------------------- mesh import / export
static void SDLCALL OnMeshFileChosen(void* userdata, const char* const* filelist, int) {
    auto* app = static_cast<App*>(userdata);
    if (!filelist) { app->QueueMeshImport(std::string("!") + SDL_GetError()); return; }
    if (!filelist[0]) return;
    app->QueueMeshImport(filelist[0]);
}
static void SDLCALL OnMeshSaveChosen(void* userdata, const char* const* filelist, int) {
    auto* app = static_cast<App*>(userdata);
    if (!filelist || !filelist[0]) return;
    app->QueueMeshExport(filelist[0]);
}

void App::RequestMeshImport() {
    static const SDL_DialogFileFilter filters[] = {
        {"All meshes (obj, fbx, gltf, glb, usd)", "obj;fbx;gltf;glb;usd;usda;usdc"},
        {"Wavefront OBJ", "obj"}, {"Autodesk FBX", "fbx"}, {"glTF", "gltf;glb"}, {"USD (ASCII)", "usd;usda"},
        {"All files", "*"},
    };
    SDL_ShowOpenFileDialog(OnMeshFileChosen, this, window, filters, 6, nullptr, false);
}
void App::RequestMeshExport() {
    if (!scene.HasMesh()) { ShowStatus("Nothing to export - add or import a mesh first."); return; }
    static const SDL_DialogFileFilter filters[] = {
        {"Wavefront OBJ", "obj"}, {"glTF", "gltf"}, {"glTF Binary", "glb"}, {"USD (ASCII)", "usda"},
    };
    SDL_ShowSaveFileDialog(OnMeshSaveChosen, this, window, filters, 4, nullptr);
}
void App::QueueMeshImport(const std::string& path) {
    std::lock_guard<std::mutex> lock(importMutex);
    if (!path.empty() && path[0] == '!') pendingImportError = path.substr(1);
    else { pendingImportPath = path; queuedKind = ImportKind::Mesh; }
}
void App::QueueMeshExport(const std::string& path) {
    std::lock_guard<std::mutex> lock(importMutex);
    pendingExportPath = path;
}

// Screenshot helper: dismiss the modal, import a mesh, enter a mode.
// mode 5 builds a two-object scene so the hierarchy and transforms can be tested deterministically.
void App::OpenDemo(const std::string& meshPath, int mode) {
    if (mode == 22) {     // background checkbox off: must fall back to the flat gradient
        OpenDemo(meshPath, 0);
        ApplyHdriPreset(7);
        hdriAsBackground = false;
        SetShading(3);
        viewport.FrameBounds(Vec3{0, 1.0f, 0}, 2.6f);
        return;
    }
    if (mode == 21) {     // HDRI background check: Forest Clearing preset, Rendered mode, orbit
        OpenDemo(meshPath, 0);
        ApplyHdriPreset(7);            // Forest Clearing
        hdriAsBackground = true;
        SetShading(3);
        viewport.FrameBounds(Vec3{0, 1.0f, 0}, 2.6f);
        return;
    }
    if (mode == 20) {     // shade smooth check: one flat sphere, one smooth sphere, side by side
        OpenDemo(meshPath, 0);
        scene.objects.clear(); scene.activeIndex = -1;
        PrimitiveParams pp;
        MeshObject* a = scene.AddMesh(BuildPrimitive(Primitive::UVSphere, pp), "Flat");
        a->transform.position = {-1.2f, 1.0f, 0};
        MeshObject* b = scene.AddMesh(BuildPrimitive(Primitive::UVSphere, pp), "Smooth");
        b->transform.position = {1.2f, 1.0f, 0};
        b->shadeSmooth = true;
        for (auto& o : scene.objects) o->selected = false;
        viewport.FrameBounds(Vec3{0, 1.0f, 0}, 2.6f);
        return;
    }
    if (mode == 19) {     // two cameras: the original plus one added the Shift+A way
        OpenDemo(meshPath, 5);
        scene.Camera().target = {0, 0.8f, 0};
        scene.Camera().yaw = -0.9f; scene.Camera().pitch = 0.25f; scene.Camera().distance = 4.0f;
        // pose the viewport somewhere else, then add a camera from that view
        viewport.SetOrbit(0.9f, 0.35f);
        viewport.SetTarget(0.0f, 0.8f, 0.0f);
        viewport.SetDistance(4.5f);
        AddCameraAtView();
        std::printf("[cams] count=%zu active=%s selected=%s\n", scene.cameras.size(),
                    scene.cameraNames[scene.activeCamera].c_str(), scene.cameraNames[selectedCamera].c_str());
        std::fflush(stdout);
        viewport.FrameBounds(Vec3{0.0f, 1.0f, 0.0f}, 11.0f);
        viewport.SetOrbit(0.05f, 0.95f);
        return;
    }
    if (mode == 18) {     // camera gizmo visible in the scene
        OpenDemo(meshPath, 5);
        scene.Camera().target = {0, 0.8f, 0};
        scene.Camera().yaw = -0.9f;
        scene.Camera().pitch = 0.25f;
        scene.Camera().distance = 4.0f;
        scene.Camera().focalLengthMm = 35.0f;
        selectedCamera = scene.activeCamera;
        showCamera = true;
        cameraDirty = true;
        viewport.FrameBounds(Vec3{1.4f, 1.1f, -0.8f}, 6.5f);
        return;
    }
    if (mode == 17) {     // map smoke test: emissive / roughness / opacity driven entirely by maps
        OpenDemo(meshPath, 0);
        scene.objects.clear(); scene.activeIndex = -1;
        PrimitiveParams gp; gp.gridX = 1; gp.gridY = 1; gp.size = 14.0f;
        MeshObject* flr = scene.AddMesh(BuildPrimitive(Primitive::Grid, gp), "Floor");
        flr->transform.position = {0, -0.01f, 0};
        PrimitiveParams pp; pp.verticesRing = 32; pp.rings = 16;
        MeshObject* ball = scene.AddMesh(BuildPrimitive(Primitive::UVSphere, pp), "Panel");
        ball->transform.position = {0, 1.1f, 0};
        ball->shadeSmooth = true;
        ball->mesh.EnsureUVs();
        const char* which = std::getenv("LEORIX_MAP_TEST");
        std::string err;
        if (which && std::string(which) == "emissive") {
            LoadMaterialMap(vk, ball->material, MapSlot::Emissive, "/tmp/lxrun/emis_map.png", err);
            ball->material.baseColor[0] = ball->material.baseColor[1] = ball->material.baseColor[2] = 0.05f;
        } else if (which && std::string(which) == "opacity") {
            LoadMaterialMap(vk, ball->material, MapSlot::Opacity, "/tmp/lxrun/opac_map.png", err);
        } else if (which && std::string(which) == "rough") {
            LoadMaterialMap(vk, ball->material, MapSlot::Roughness, "/tmp/lxrun/rough_map.png", err);
            ball->material.metallic = 1.0f;
        }
        if (!err.empty()) std::printf("[maptest] %s\n", err.c_str());
        rt.width = 420; rt.height = 240; rt.samples = 40; rt.maxBounces = 4; rt.denoise = true;
        world.sunStrength = 0.4f; world.strength = 0.25f;
        ApplyHdriPreset(3);
        hdriStrength = 0.35f;
        viewport.FrameBounds(Vec3{0, 1.0f, 0}, 2.4f);
        SetShading(3);
        return;
    }
    if (mode == 16) {     // face orientation overlay: flip one face after import and show it
        OpenDemo(meshPath, 2);
        MeshObject* o = scene.Active();
        if (o && o->mesh.tris.size() >= 2) {
            // reverse the top face (third polygon of the sample cube) so it points inward
            // flip whichever polygon sits highest, so the flipped face is the one facing us
            uint32_t topPoly = 0; float bestY = -1e9f;
            for (const Tri& t : o->mesh.tris) {
                float y = (o->mesh.positions[t.a].y + o->mesh.positions[t.b].y + o->mesh.positions[t.c].y) / 3.0f;
                if (y > bestY) { bestY = y; topPoly = t.poly; }
            }
            int n = 0;
            for (Tri& t : o->mesh.tris) if (t.poly == topPoly) { std::swap(t.b, t.c); ++n; }
            o->mesh.ComputeNormals();
            o->MarkTopoDirty();
            std::printf("[demo] flipped poly %u (%d tris) at y=%.2f\n", topPoly, n, bestY);
        }
        edit.faceOrientation = true;
        SetMode3D(Mode3D::Edit);
        return;
    }
    if (mode == 15) {     // HDRI + glass + emission render test
        OpenDemo(meshPath, 0);
        scene.objects.clear(); scene.activeIndex = -1;
        PrimitiveParams gp; gp.gridX = 1; gp.gridY = 1; gp.size = 16.0f;
        MeshObject* flr = scene.AddMesh(BuildPrimitive(Primitive::Grid, gp), "Floor");
        flr->transform.position = {0, -0.01f, 0};
        flr->material.baseColor[0] = flr->material.baseColor[1] = flr->material.baseColor[2] = 0.55f;
        PrimitiveParams sp; sp.verticesRing = 48; sp.rings = 24;
        MeshObject* glass = scene.AddMesh(BuildPrimitive(Primitive::UVSphere, sp), "Glass");
        glass->transform.position = {-1.3f, 1.0f, 0};
        glass->shadeSmooth = true;
        glass->material.transmission = 1.0f;
        glass->material.transmissionIOR = 1.5f;
        glass->material.baseColor[0] = 0.95f; glass->material.baseColor[1] = 0.98f; glass->material.baseColor[2] = 1.0f;
        MeshObject* lamp = scene.AddMesh(BuildPrimitive(Primitive::Cube, DefaultParams(Primitive::Cube)), "Lamp");
        lamp->transform.position = {1.6f, 0.9f, 0};
        lamp->transform.scale = {0.45f, 0.45f, 0.45f};
        lamp->material.emissiveStrengthRT = 12.0f;
        lamp->material.baseColor[0] = 1.0f; lamp->material.baseColor[1] = 0.75f; lamp->material.baseColor[2] = 0.4f;
        rt.width = 640; rt.height = 360; rt.samples = 160; rt.maxBounces = 6;
        rt.denoise = true;
        world.sunStrength = 1.2f; world.sunElevation = 35.0f; world.strength = 0.5f;
        hdriStrength = 1.0f;
        viewport.FrameBounds(Vec3{0, 1.0f, 0}, 3.4f);
        SetShading(3);
        return;
    }
    if (mode == 14) {     // headless sculpt check: stamp strokes at known points on the sphere
        OpenDemo(meshPath, 13);
        MeshObject* o = scene.Active();
        if (o) {
            sculpt.brush = 0;              // Draw
            sculpt.strength = 1.0f;
            sculpt.radiusPx = 120.0f;
            o->mesh.ComputeBounds();
            float before = o->multires.Evaluate().bmax.y;
            // pretend the cursor is on the top of the sphere
            lastPick.hit = true;
            lastPick.point = {0.0f, 1.0f, 0.0f};
            lastSculptPoint = lastPick.point;
            for (int i = 0; i < 6; ++i) SculptStroke(i == 0);
            size_t moved = 0;
            for (const Float3& d : o->multires.displacement)
                if (std::fabs(d.x) + std::fabs(d.y) + std::fabs(d.z) > 1e-6f) ++moved;
            Mesh ev = o->multires.Evaluate();
            std::printf("[sculpt] displaced %zu of %zu verts; top y %.4f -> %.4f (%s)\n",
                        moved, o->multires.VertexCount(), before, ev.bmax.y,
                        ev.bmax.y > before + 0.01f ? "raised" : "NO CHANGE");
            std::printf("[sculpt] base mesh still %zu verts (%s)\n", o->mesh.VertexCount(),
                        o->mesh.VertexCount() == 290 ? "untouched" : "check");
        }
        quitRequested = true;
        return;
    }
    if (mode == 13) {     // multires sculpt test: subdivide, then stamp strokes programmatically
        OpenDemo(meshPath, 0);
        MeshObject* o = scene.Active();
        if (o) {
            PrimitiveParams pp; pp.verticesRing = 24; pp.rings = 12;
            o->mesh = BuildPrimitive(Primitive::UVSphere, pp);
            o->MarkTopoDirty();
            o->multires.Rebuild(o->mesh, 2);
            o->displayMesh = o->multires.Evaluate();
            o->hasDisplay = true;
            showMultires = true;
            SetMode3D(Mode3D::Sculpt);
            std::printf("[multires] base=%zu level2=%zu\n", o->mesh.VertexCount(), o->multires.VertexCount());
        }
        return;
    }
    if (mode == 12) {     // path tracer smoke test: a couple of objects, render, save the PNG
        OpenDemo(meshPath, 5);
        PrimitiveParams gp; gp.gridX = 1; gp.gridY = 1; gp.size = 14.0f;
        MeshObject* floorObj = scene.AddMesh(BuildPrimitive(Primitive::Grid, gp), "Floor");
        floorObj->transform.position = {0, -0.01f, 0};
        floorObj->material.baseColor[0] = 0.62f; floorObj->material.baseColor[1] = 0.60f; floorObj->material.baseColor[2] = 0.58f;
        if (scene.objects.size() > 1) {
            scene.objects[1]->material.metallic = 0.9f;
            scene.objects[1]->material.roughness = 0.15f;
        }
        rt.width = 640; rt.height = 360; rt.samples = 96; rt.maxBounces = 4;
        world.sunStrength = 3.2f; world.sunElevation = 26.0f; world.sunAzimuth = -120.0f; world.strength = 0.5f;
        SetShading(3);
        StartRender();
        return;
    }
    if (mode == 11) {     // curve deform + subsurface demo
        OpenDemo(meshPath, 0);
        AddCurve(2);                                  // spiral
        int curveIndex = scene.activeIndex;
        PrimitiveParams pp; pp.gridX = 60; pp.gridY = 2; pp.size = 2.0f;   // dense along X, the deform axis
        MeshObject* bar = scene.AddMesh(BuildPrimitive(Primitive::Grid, pp), "Ribbon");
        for (auto& p : bar->mesh.positions) { p.x *= 4.0f; p.z *= 0.25f; }
        Modifier cd; cd.type = ModifierType::CurveDeform; cd.curveTarget = (int)scene.objects[curveIndex]->id; cd.curveAxis = 0;
        Modifier so; so.type = ModifierType::Solidify; so.solidifyThickness = 0.06f;
        bar->modifiers.push_back(cd);
        bar->modifiers.push_back(so);
        bar->material.sssWeight = 0.8f;
        bar->material.baseColor[0] = 0.85f; bar->material.baseColor[1] = 0.6f; bar->material.baseColor[2] = 0.5f;
        bar->stackDirty = true;
        RefreshModifiers(*bar);
        showModifiers = true;
        SetShading(3);
        return;
    }
    if (mode == 10) {     // modifier demo: cube with Subdivision + Array previewing live
        OpenDemo(meshPath, 0);
        MeshObject* o = scene.Active();
        if (o) {
            Modifier sub; sub.type = ModifierType::Subdivide; sub.subdivLevels = 2; sub.subdivSmooth = true;
            Modifier arr; arr.type = ModifierType::Array; arr.arrayCount = 3; arr.arrayOffset[0] = 1.3f;
            o->modifiers.push_back(sub);
            o->modifiers.push_back(arr);
            o->stackDirty = true;
            RefreshModifiers(*o);
        }
        showModifiers = true;
        return;
    }
    if (mode == 9) {      // join demo: cube + offset sphere, both selected, then joined
        OpenDemo(meshPath, 5);
        for (auto& o : scene.objects) o->selected = true;
        if (!scene.objects.empty()) scene.SetActive(0);
        JoinMeshes();
        showHierarchy = true;
        return;
    }
    if (mode == 8) {      // seam + unwrap demo: cube, all polygon edges marked, then unwrapped
        OpenDemo(meshPath, 2);
        MeshObject* obj = scene.Active();
        if (obj) {
            obj->BuildTopology();
            for (uint64_t e : obj->polyEdges) obj->mesh.seamEdges.insert(e);
            edit.selectMode = (int)SelectMode::Edge;
            UnwrapSelected();
            showUV = true;
        }
        return;
    }
    if (mode == 7) {      // same rig, bent and keyframed, to verify deformation and the timeline
        OpenDemo(meshPath, 6);
        MeshObject* obj = scene.Active();
        if (obj && obj->armature.bones.size() > 1) {
            activeBone = 1;
            obj->action.frameStart = 1; obj->action.frameEnd = 48;
            obj->armature.bones[1].poseRotation = {0, 0, 0};
            InsertKeyframes(true, true, true);
            playback.frame = 48;
            obj->armature.bones[1].poseRotation = {0, 0, 1.0f};   // ~57 degrees around Z
            InsertKeyframes(true, true, true);
            playback.frame = 34;
            ApplyAnimation(playback.frame);
            UpdateSkinning(*obj);
        }
        return;
    }
    if (mode == 6) {      // rigging demo: a cylinder with a two-bone chain bound to it
        showNewProject = false;
        ApplyWorkspace(Workspace::Animation3D);
        PrimitiveParams pp; pp.verticesRing = 16; pp.radius = 0.35f; pp.depth = 4.0f;
        MeshObject* obj = scene.AddMesh(BuildPrimitive(Primitive::Cylinder, pp), "Limb");
        obj->mesh.ComputeBounds();
        float lo = obj->mesh.bmin.y, hi = obj->mesh.bmax.y, midY = (lo + hi) * 0.5f;
        int root = obj->armature.AddBone("Bone.1", {0, lo, 0}, {0, midY, 0}, -1);
        obj->armature.AddBone("Bone.2", {0, midY, 0}, {0, hi, 0}, root);
        obj->armature.BindAutomaticWeights(obj->mesh);
        UpdateSkinning(*obj);
        activeBone = 1;
        showArmature = true; showTimeline = true; showViewport3D = true;
        SetMode3D(Mode3D::Pose);
        return;
    }
    if (mode == 5) {
        showNewProject = false;
        ApplyWorkspace(Workspace::Design3D);
        std::string err; Mesh m;
        if (ImportMesh(meshPath, m, err)) scene.AddMesh(std::move(m), "Cube");
        PrimitiveParams pp; pp.radius = 0.8f;
        MeshObject* sphere = scene.AddMesh(BuildPrimitive(Primitive::UVSphere, pp), "Sphere");
        sphere->transform.position = {2.4f, 0.9f, 0.0f};
        showHierarchy = true;
        showViewport3D = true;
        SetMode3D(Mode3D::Object);
        return;
    }
    showNewProject = false;
    if (mode == 1) ApplyWorkspace(Workspace::Creation3D);
    else ApplyWorkspace(Workspace::Design3D);
    std::string error; Mesh m;
    if (ImportMesh(meshPath, m, error)) { scene.AddMesh(std::move(m), "Mesh"); showViewport3D = true; }
    else ShowStatus(error);
    if (mode >= 2) SetMode3D(Mode3D::Edit);
    else if (mode == 1) SetMode3D(Mode3D::Sculpt);
    if (mode == 3 && scene.HasMesh()) { edit.selectMode = 2; // edge: select a couple of edges to show orange
        const auto& tris = scene.Active()->mesh.tris;
        if (!tris.empty()) { scene.Active()->selEdges.insert(Mesh::EdgeKey(tris[0].a, tris[0].b));
                             scene.Active()->selEdges.insert(Mesh::EdgeKey(tris[0].b, tris[0].c)); } }
    if (mode == 4 && scene.HasMesh()) { edit.selectMode = 4; // face: select a face to show yellow
        if (scene.Active()->mesh.polyCount) scene.Active()->selFaces.insert(0); }
}

// ---------------------------------------------------------------- Hierarchy (outliner)
void App::HierarchyPanel() {
    if (!ImGui::Begin("Hierarchy", &showHierarchy)) { ImGui::End(); return; }
    ImGui::TextColored(kBrass, "SCENE");
    ImGui::SameLine();
    ImGui::TextDisabled("(%zu object%s)", scene.objects.size(), scene.objects.size() == 1 ? "" : "s");
    ImGui::Separator();

    int removeIndex = -1, duplicateIndex = -1;
    for (int i = 0; i < (int)scene.objects.size(); ++i) {
        MeshObject& o = *scene.objects[i];
        ImGui::PushID(i);
        // visibility toggle
        if (IconButton("##vis", o.visible ? Icon::XRay : Icon::SelectBox, o.visible, o.visible ? "Hide" : "Show", 20.0f))
            o.visible = !o.visible;
        ImGui::SameLine();
        ImVec2 p = ImGui::GetCursorScreenPos();
        DrawIcon(ImGui::GetWindowDrawList(), Icon::ModeObject, ImVec2(p.x + 9, p.y + ImGui::GetTextLineHeight() * 0.5f), 15,
                 o.selected ? ImGui::GetColorU32(kBrass) : ImGui::GetColorU32(ImGuiCol_TextDisabled));
        ImGui::Dummy(ImVec2(20, 0));
        ImGui::SameLine();
        char label[160];
        std::snprintf(label, sizeof(label), "%s##row", o.name.c_str());
        if (ImGui::Selectable(label, o.selected)) {
            if (ImGui::GetIO().KeyShift) { o.selected = !o.selected; scene.SetActive(i); }
            else scene.SelectOnly(i);
        }
        if (ImGui::BeginPopupContextItem("##ctx")) {
            if (ImGui::MenuItem(o.shadeSmooth ? "Shade Flat" : "Shade Smooth")) o.shadeSmooth = !o.shadeSmooth;
            if (ImGui::MenuItem("Duplicate")) duplicateIndex = i;
            if (ImGui::MenuItem("Delete", "X")) removeIndex = i;
            ImGui::Separator();
            static char rename[128];
            if (ImGui::IsWindowAppearing()) std::snprintf(rename, sizeof(rename), "%s", o.name.c_str());
            if (ImGui::InputText("Name", rename, sizeof(rename), ImGuiInputTextFlags_EnterReturnsTrue)) {
                o.name = rename;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (o.active) { ImGui::SameLine(); ImGui::TextColored(kBrass, "*"); }
        ImGui::SameLine();
        ImGui::TextDisabled("%zu v", o.mesh.VertexCount());
        ImGui::PopID();
    }
    ImGui::Separator();
    int deleteCamera = -1;
    for (int ci = 0; ci < (int)scene.cameras.size(); ++ci) {
        ImGui::PushID(5000 + ci);
        bool sel = selectedCamera == ci;
        bool isActive = scene.activeCamera == ci;
        ImVec2 p = ImGui::GetCursorScreenPos();
        DrawIcon(ImGui::GetWindowDrawList(), Icon::Transform3D, ImVec2(p.x + 9, p.y + ImGui::GetTextLineHeight() * 0.5f), 15,
                 sel ? ImGui::GetColorU32(kBrass) : ImGui::GetColorU32(ImGuiCol_TextDisabled));
        ImGui::Dummy(ImVec2(20, 0)); ImGui::SameLine();
        char label[64];
        std::snprintf(label, sizeof(label), "%s##cam", scene.cameraNames[ci].c_str());
        if (ImGui::Selectable(label, sel)) {
            selectedCamera = sel ? -1 : ci;
            if (selectedCamera >= 0) { for (auto& ob : scene.objects) ob->selected = false; showCamera = true; }
        }
        if (ImGui::BeginPopupContextItem("##camctx")) {
            if (ImGui::MenuItem("Set Active Camera", "Ctrl+Numpad 0", isActive)) { scene.activeCamera = ci; cameraDirty = true; }
            if (ImGui::MenuItem("Delete", nullptr, false, scene.cameras.size() > 1)) deleteCamera = ci;
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        if (isActive) ImGui::TextColored(kBrass, "active");
        else ImGui::TextDisabled("%.0f mm", scene.cameras[ci].focalLengthMm);
        ImGui::PopID();
    }
    if (deleteCamera >= 0 && scene.cameras.size() > 1) {
        scene.cameras.erase(scene.cameras.begin() + deleteCamera);
        scene.cameraNames.erase(scene.cameraNames.begin() + deleteCamera);
        if (scene.activeCamera >= deleteCamera) scene.activeCamera = std::max(0, scene.activeCamera - 1);
        selectedCamera = -1;
        cameraDirty = true;
        ShowStatus("Deleted camera");
    }

    if (scene.objects.empty()) ImGui::TextDisabled("Empty. Shift+A adds a primitive, Ctrl+I imports a mesh.");

    if (duplicateIndex >= 0) {
        MeshObject& src = *scene.objects[duplicateIndex];
        MeshObject* copy = scene.AddMesh(src.mesh, src.name);
        copy->transform = src.transform;
    }
    if (removeIndex >= 0) scene.Remove(removeIndex);

    // Object transform fields for the active object
    if (MeshObject* o = scene.Active()) {
        ImGui::Separator();
        ImGui::TextColored(kBrass, "TRANSFORM");
        ImGui::PushItemWidth(-70.0f);
        // Shown and edited in Blender's axes: X left/right, Y front/back, Z up/down.
        Float3 loc = ToUser(o->transform.position);
        if (ImGui::DragFloat3("Location", &loc.x, 0.01f, 0, 0, "%.3f m")) o->transform.position = FromUser(loc);
        Float3 rot = RotToUser(o->transform.rotation);
        Float3 degrees{rot.x * 57.2957795f, rot.y * 57.2957795f, rot.z * 57.2957795f};
        if (ImGui::DragFloat3("Rotation", &degrees.x, 0.5f, 0, 0, "%.1f deg"))
            o->transform.rotation = RotFromUser({degrees.x / 57.2957795f, degrees.y / 57.2957795f, degrees.z / 57.2957795f});
        Float3 scl = ScaleToUser(o->transform.scale);
        if (ImGui::DragFloat3("Scale", &scl.x, 0.01f, 0.001f, 1000.0f, "%.3f")) o->transform.scale = ScaleFromUser(scl);
        ImGui::PopItemWidth();
        ImGui::TextDisabled("G move  |  R rotate  |  S scale  |  X/Y/Z axis  (Z is up)");
    }
    ImGui::End();
}

// ---------------------------------------------------------------- modal transform (G / R / S)
void App::BeginTransform(int mode) {
    if (CameraSelected()) {
        xform = TransformOp{};
        xform.mode = mode;
        xform.axis = -1;
        xform.startMouse = ImGui::GetIO().MousePos;
        xform.objectIndex = -1;
        xform.savedTransform.position = SelCam().target;
        xform.savedTransform.rotation = {SelCam().yaw, SelCam().pitch, 0.0f};
        xform.pivot = SelCam().target;
        Vec3 d{xform.pivot.x - viewport.CameraEye().x, xform.pivot.y - viewport.CameraEye().y,
               xform.pivot.z - viewport.CameraEye().z};
        xform.pivotDepth = std::max(0.2f, Dot(d, viewport.CameraForward()));
        xform.active = true;
        ShowStatus(mode == 0 ? "Move camera" : "Aim camera");
        return;
    }
    MeshObject* o = scene.Active();
    if (!o) return;
    xform = TransformOp{};
    xform.mode = mode;
    xform.axis = -1;
    xform.startMouse = ImGui::GetIO().MousePos;
    xform.objectIndex = scene.activeIndex;

    if (mode3D == Mode3D::Edit && o->isCurve) {
        // Curve points and handles. A point carries its Aligned/Free handles with it; Auto and
        // Vector handles are recomputed from the points anyway.
        o->curve.Sync();
        std::set<int> elems;
        for (int e : o->curveSel) {
            if (e < 0 || e >= o->curve.ElemCount()) continue;
            elems.insert(e);
            HandleType t = o->curve.handleType[e / 3];
            if (e % 3 == 0 && (t == HandleType::Aligned || t == HandleType::Free)) { elems.insert(e + 1); elems.insert(e + 2); }
        }
        if (elems.empty()) { ShowStatus("Nothing selected - click a curve point or handle first."); return; }
        xform.curve = true;
        xform.savedCurve = o->curve;
        Float3 sum{0, 0, 0};
        for (int e : elems) {
            xform.movedVerts.push_back((uint32_t)e);
            xform.savedVerts.push_back(o->curve.Elem(e));
            Float3 w = o->transform.Apply(o->curve.Elem(e));
            sum.x += w.x; sum.y += w.y; sum.z += w.z;
        }
        float n = (float)elems.size();
        xform.pivot = {sum.x / n, sum.y / n, sum.z / n};
    } else if (mode3D == Mode3D::Edit) {
        // Collect the vertices the current select mode implies.
        std::unordered_set<uint32_t> verts = o->selVerts;
        for (uint64_t e : o->selEdges) { verts.insert(uint32_t(e >> 32)); verts.insert(uint32_t(e & 0xFFFFFFFF)); }
        if (!o->selFaces.empty())
            for (const Tri& t : o->mesh.tris)
                if (o->selFaces.count(t.poly)) { verts.insert(t.a); verts.insert(t.b); verts.insert(t.c); }
        if (verts.empty()) { ShowStatus("Nothing selected - click a vertex, edge or face first."); return; }
        xform.movedVerts.assign(verts.begin(), verts.end());
        xform.savedVerts.reserve(xform.movedVerts.size());
        Float3 sum{0, 0, 0};
        for (uint32_t v : xform.movedVerts) {
            xform.savedVerts.push_back(o->mesh.positions[v]);
            Float3 w = o->transform.Apply(o->mesh.positions[v]);
            sum.x += w.x; sum.y += w.y; sum.z += w.z;
        }
        float n = (float)xform.movedVerts.size();
        xform.pivot = {sum.x / n, sum.y / n, sum.z / n};
    } else {
        xform.savedTransform = o->transform;
        xform.pivot = o->transform.position;
    }
    Vec3 d{xform.pivot.x - viewport.CameraEye().x, xform.pivot.y - viewport.CameraEye().y, xform.pivot.z - viewport.CameraEye().z};
    xform.pivotDepth = std::max(0.2f, Dot(d, viewport.CameraForward()));
    xform.active = true;
    if (std::getenv("LEORIX_DEBUG_KEYS"))
        std::printf("[xform] begin mode=%d obj=%d verts=%zu pivotDepth=%.2f\n",
                    xform.mode, xform.objectIndex, xform.movedVerts.size(), xform.pivotDepth);
    const char* names[] = {"Move", "Rotate", "Scale"};
    ShowStatus(std::string(names[mode]) + ": move the mouse, X/Y/Z to constrain, click to confirm, Esc to cancel");
}

void App::CancelTransform() {
    MeshObject* o = (xform.objectIndex >= 0 && xform.objectIndex < (int)scene.objects.size())
                        ? scene.objects[xform.objectIndex].get() : nullptr;
    if (o && xform.curve) {
        o->curve = xform.savedCurve;
        RebuildCurveMesh(*o);
    } else if (o) {
        if (!xform.movedVerts.empty()) {
            for (size_t i = 0; i < xform.movedVerts.size(); ++i) o->mesh.positions[xform.movedVerts[i]] = xform.savedVerts[i];
            o->mesh.ComputeNormals();
        } else {
            o->transform = xform.savedTransform;
        }
    }
    xform = TransformOp{};
}

void App::UpdateTransform(bool) {
    if (!xform.active) return;
    if (xform.objectIndex < 0 && CameraSelected()) {
        SceneCamera& cam = SelCam();
        ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            cam.target = xform.savedTransform.position;
            cam.yaw = xform.savedTransform.rotation.x;
            cam.pitch = xform.savedTransform.rotation.y;
            xform = TransformOp{};
            cameraDirty = true;
            return;
        }
        float dx = io.MousePos.x - xform.startMouse.x, dy = io.MousePos.y - xform.startMouse.y;
        if (xform.mode == 0) {
            Vec3 r = viewport.CameraRight(), u = viewport.CameraUp();
            float k = viewport.WorldPerPixel(xform.pivotDepth);
            cam.target = {xform.savedTransform.position.x + (r.x * dx - u.x * dy) * k,
                                   xform.savedTransform.position.y + (r.y * dx - u.y * dy) * k,
                                   xform.savedTransform.position.z + (r.z * dx - u.z * dy) * k};
        } else {
            cam.yaw = xform.savedTransform.rotation.x + dx * 0.005f;
            cam.pitch = std::clamp(xform.savedTransform.rotation.y + dy * 0.005f, -1.5f, 1.5f);
        }
        cameraDirty = true;
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
            xform = TransformOp{};
            ShowStatus("Camera moved");
        }
        return;
    }
    MeshObject* o = (xform.objectIndex >= 0 && xform.objectIndex < (int)scene.objects.size())
                        ? scene.objects[xform.objectIndex].get() : nullptr;
    if (!o) { xform = TransformOp{}; return; }
    ImGuiIO& io = ImGui::GetIO();

    // axis constraints
    // Keys are Blender's axes (Z up, Y front/back); xform.axis stays an internal index.
    auto toggleAxis = [&](int userAxis) {
        int a = InternalAxis(userAxis);
        xform.axis = xform.axis == a ? -1 : a;
        ShowStatus(xform.axis < 0 ? std::string("Free") : std::string("Along ") + UserAxisName(userAxis));
    };
    if (ImGui::IsKeyPressed(ImGuiKey_X, false)) toggleAxis(0);
    if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) toggleAxis(1);
    if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) toggleAxis(2);
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) { CancelTransform(); return; }

    ImVec2 m = io.MousePos;
    float dx = m.x - xform.startMouse.x, dy = m.y - xform.startMouse.y;
    float perPixel = viewport.WorldPerPixel(xform.pivotDepth);
    if (std::getenv("LEORIX_DEBUG_KEYS") && (dx != 0 || dy != 0))
        std::printf("[xform] update dx=%.1f dy=%.1f perPixel=%.4f pos=%.2f\n", dx, dy, perPixel, o->transform.position.x);

    auto applyToVerts = [&](auto fn) {
        if (xform.curve) {
            o->curve = xform.savedCurve;
            for (size_t i = 0; i < xform.movedVerts.size(); ++i) o->curve.SetElem((int)xform.movedVerts[i], fn(xform.savedVerts[i]));
            RebuildCurveMesh(*o);
            return;
        }
        for (size_t i = 0; i < xform.movedVerts.size(); ++i) o->mesh.positions[xform.movedVerts[i]] = fn(xform.savedVerts[i]);
        o->mesh.ComputeNormals();
    };

    if (xform.mode == 0) { // ---- grab
        Vec3 r = viewport.CameraRight(), u = viewport.CameraUp();
        Float3 delta{ (r.x * dx - u.x * dy) * perPixel,
                      (r.y * dx - u.y * dy) * perPixel,
                      (r.z * dx - u.z * dy) * perPixel };
        delta = AxisMask(delta, xform.axis);

        // Snapping. Ctrl toggles it while dragging, the way Blender does.
        bool snapNow = snapEnabled != io.KeyCtrl;
        if (snapNow) {
            // Object mode snaps the origin; Edit Mode snaps the selection's median (the pivot).
            Float3 base = xform.movedVerts.empty() ? xform.savedTransform.position : xform.pivot;
            Float3 want{base.x + delta.x, base.y + delta.y, base.z + delta.z};
            SnapTo to = (SnapTo)snapMode;
            // shortcut: Edit Mode skips its own object (its verts are the ones moving), so it can't snap to its own unselected verts yet.
            if (!SnapToGeometry(scene, xform.objectIndex, to, want, want) && (to == SnapTo::Increment || to == SnapTo::Grid)) {
                float inc = std::max(0.0001f, to == SnapTo::Grid ? 1.0f : snapIncrement);
                want = {std::round(want.x / inc) * inc, std::round(want.y / inc) * inc, std::round(want.z / inc) * inc};
            }
            delta = AxisMask({want.x - base.x, want.y - base.y, want.z - base.z}, xform.axis);   // G X stays on X
        }
        if (!xform.movedVerts.empty()) {
            // Vertices live in object space, so undo the object's scale/rotation for the move.
            Float3 inv = delta;
            const Float3& sc = o->transform.scale;
            inv = {inv.x / (sc.x != 0 ? sc.x : 1), inv.y / (sc.y != 0 ? sc.y : 1), inv.z / (sc.z != 0 ? sc.z : 1)};
            applyToVerts([&](Float3 v) { return Float3{v.x + inv.x, v.y + inv.y, v.z + inv.z}; });
        } else {
            o->transform.position = {xform.savedTransform.position.x + delta.x,
                                     xform.savedTransform.position.y + delta.y,
                                     xform.savedTransform.position.z + delta.z};
        }
    } else if (xform.mode == 1) { // ---- rotate
        float zpv; ImVec2 pivotScreen;
        viewport.ProjectPoint(xform.pivot, zpv, pivotScreen);
        float a0 = std::atan2(xform.startMouse.y - pivotScreen.y, xform.startMouse.x - pivotScreen.x);
        float a1 = std::atan2(m.y - pivotScreen.y, m.x - pivotScreen.x);
        float angle = a1 - a0;
        if (snapEnabled != io.KeyCtrl) angle = SnapAngle(angle, io.KeyShift);   // 15 degree steps, Shift for 5
        int axis = xform.axis >= 0 ? xform.axis : 1;   // free rotate defaults to the up axis
        if (!xform.movedVerts.empty()) {
            float c = std::cos(angle), s = std::sin(angle);
            applyToVerts([&](Float3 v) {
                // rotate around the pivot, in object space
                Float3 pv = o->transform.Apply(v);
                Float3 d{pv.x - xform.pivot.x, pv.y - xform.pivot.y, pv.z - xform.pivot.z};
                Float3 rd = axis == 0 ? Float3{d.x, d.y * c - d.z * s, d.y * s + d.z * c}
                          : axis == 1 ? Float3{d.x * c + d.z * s, d.y, -d.x * s + d.z * c}
                                      : Float3{d.x * c - d.y * s, d.x * s + d.y * c, d.z};
                Float3 world{xform.pivot.x + rd.x, xform.pivot.y + rd.y, xform.pivot.z + rd.z};
                // back to object space (translation + scale only; object rotation stays applied)
                const Transform& t = o->transform;
                Float3 local{(world.x - t.position.x) / (t.scale.x != 0 ? t.scale.x : 1),
                             (world.y - t.position.y) / (t.scale.y != 0 ? t.scale.y : 1),
                             (world.z - t.position.z) / (t.scale.z != 0 ? t.scale.z : 1)};
                return local;
            });
        } else {
            Float3 rot = xform.savedTransform.rotation;
            if (axis == 0) rot.x += angle; else if (axis == 1) rot.y += angle; else rot.z += angle;
            o->transform.rotation = rot;
        }
    } else { // ---- scale
        float zpv; ImVec2 pivotScreen;
        viewport.ProjectPoint(xform.pivot, zpv, pivotScreen);
        float d0 = std::max(8.0f, std::hypot(xform.startMouse.x - pivotScreen.x, xform.startMouse.y - pivotScreen.y));
        float d1 = std::hypot(m.x - pivotScreen.x, m.y - pivotScreen.y);
        float f = std::clamp(d1 / d0, 0.01f, 100.0f);
        if (snapEnabled != io.KeyCtrl) f = SnapScale(f);
        Float3 factor{f, f, f};
        if (xform.axis == 0) factor = {f, 1, 1};
        else if (xform.axis == 1) factor = {1, f, 1};
        else if (xform.axis == 2) factor = {1, 1, f};
        if (!xform.movedVerts.empty()) {
            const Transform& t = o->transform;
            Float3 localPivot{(xform.pivot.x - t.position.x), (xform.pivot.y - t.position.y), (xform.pivot.z - t.position.z)};
            applyToVerts([&](Float3 v) {
                return Float3{localPivot.x + (v.x - localPivot.x) * factor.x,
                              localPivot.y + (v.y - localPivot.y) * factor.y,
                              localPivot.z + (v.z - localPivot.z) * factor.z};
            });
        } else {
            o->transform.scale = {xform.savedTransform.scale.x * factor.x,
                                  xform.savedTransform.scale.y * factor.y,
                                  xform.savedTransform.scale.z * factor.z};
        }
    }

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
        o->MarkTopoDirty();
        static const char* names[] = {"Move", "Rotate", "Scale"};
        int objIndex = xform.objectIndex;
        bool bakeCleared = false;
        if (xform.curve) {
            Mesh beforeMesh = CurveToMesh(xform.savedCurve, CurveProfile(*o));
            beforeMesh.name = o->name;
            PushCurveUndo(std::string(names[xform.mode]) + " Curve Points", o, xform.savedCurve, beforeMesh, true);
        } else if (!xform.movedVerts.empty()) {
            auto verts = std::make_shared<std::vector<uint32_t>>(xform.movedVerts);
            auto before = std::make_shared<std::vector<Float3>>(xform.savedVerts);
            auto after = std::make_shared<std::vector<Float3>>();
            for (uint32_t v : *verts) after->push_back(o->mesh.positions[v]);
            auto apply = [this, objIndex, verts](std::shared_ptr<std::vector<Float3>> src) {
                if (objIndex < 0 || objIndex >= (int)scene.objects.size()) return;
                MeshObject& ob = *scene.objects[objIndex];
                for (size_t i = 0; i < verts->size(); ++i)
                    if ((*verts)[i] < ob.mesh.positions.size()) ob.mesh.positions[(*verts)[i]] = (*src)[i];
                ob.mesh.ComputeNormals();
                ob.MarkTopoDirty();
            };
            undoStack.Push(std::string(names[xform.mode]) + " Vertices",
                           [apply, before] { apply(before); }, [apply, after] { apply(after); },
                           verts->size() * sizeof(Float3) * 2);
        } else {
            // a moved baked object drops its bake; undo hands the bake back with the old transform
            auto bake = std::make_shared<RigidBody>(o->rigidBody);
            auto clothBake = std::make_shared<Cloth>(o->cloth);   // its cache and rest, for undo
            if (o->rigidBody.Baked()) { o->rigidBody.cache.clear(); bakeCleared = true; }
            if (o->cloth.Baked()) { DropClothBake(*o, false); bakeCleared = true; }
            Transform before = xform.savedTransform, after = o->transform;
            undoStack.Push(names[xform.mode],
                [this, objIndex, before, bake, clothBake] {
                    if (objIndex >= (int)scene.objects.size()) return;
                    MeshObject& ob = *scene.objects[objIndex];
                    ob.transform = before;
                    if (bake->Baked()) { ob.rigidBody.cache = bake->cache; ob.rigidBody.cacheStart = bake->cacheStart; }
                    if (clothBake->Baked() && clothBake->rest.size() == ob.mesh.positions.size()) {
                        ob.cloth.cache = clothBake->cache; ob.cloth.rest = clothBake->rest;
                        ob.cloth.cacheStart = clothBake->cacheStart; ob.cloth.shown = -1;
                        ApplyRigidBodyCache(scene, playback.frame);   // show the restored cloth frame
                    }
                },
                [this, objIndex, after, bake, clothBake] {
                    if (objIndex >= (int)scene.objects.size()) return;
                    MeshObject& ob = *scene.objects[objIndex];
                    ob.transform = after;
                    if (bake->Baked()) ob.rigidBody.cache.clear();
                    if (clothBake->Baked()) DropClothBake(ob, false);
                },
                bake->cache.size() * sizeof(RigidTransform) +
                    (clothBake->rest.size() + clothBake->cache.size() * clothBake->rest.size()) * sizeof(Float3));
        }
        xform = TransformOp{};
        ShowStatus(bakeCleared ? "Moved a baked object: its bake was cleared, bake again" : "Transform applied");
    }
}

// ---------------------------------------------------------------- world / environment
ViewLighting App::BuildLighting() const {
    ViewLighting l;
    float el = world.sunElevation * 3.14159265f / 180.0f;
    float az = world.sunAzimuth * 3.14159265f / 180.0f;
    l.sunDir = {std::cos(el) * std::sin(az), std::sin(el), std::cos(el) * std::cos(az)};
    for (int i = 0; i < 3; ++i) { l.sunColor[i] = world.sunColor[i]; l.worldColor[i] = world.color[i]; }
    l.sunStrength = world.sunStrength;
    l.worldStrength = world.strength;
    l.exposure = world.exposure;
    l.studio = world.studioLightInSolid;
    return l;
}

void App::WorldPanel() {
    if (!ImGui::Begin("Environment", &showWorld)) { ImGui::End(); return; }
    ImGui::TextColored(kBrass, "WORLD");
    ImGui::TextDisabled("Lights Material Preview and Rendered. Solid uses a fixed studio light.");
    ImGui::PushItemWidth(-110.0f);
    static const char* bg[] = {"Flat Color", "Sky Gradient", "World Color (no horizon)"};
    ImGui::Combo("Background", &world.background, bg, IM_COUNTOF(bg));
    ImGui::ColorEdit3("Sky / World", world.color, ImGuiColorEditFlags_NoInputs);
    ImGui::BeginDisabled(world.background != 1);
    ImGui::ColorEdit3("Horizon", world.horizon, ImGuiColorEditFlags_NoInputs);
    ImGui::EndDisabled();
    ImGui::SliderFloat("Strength", &world.strength, 0.0f, 4.0f, "%.2f");

    ImGui::Spacing(); ImGui::TextColored(kBrass, "HDRI ENVIRONMENT"); ImGui::Separator();
    if (hdri.Loaded()) {
        size_t slash = hdriPath.find_last_of("/\\");
        ImGui::TextDisabled("%s  (%d x %d)", (slash == std::string::npos ? hdriPath : hdriPath.substr(slash + 1)).c_str(),
                            hdri.width, hdri.height);
    } else ImGui::TextDisabled("None. The sky gradient above is used instead.");
    BuildHdriThumbnails();
    // Built-in environments, generated rather than shipped as files.
    const float cell = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 3) / 4.0f;
    for (int i = 0; i < (int)HdriPreset::Count; ++i) {
        if (i % 4) ImGui::SameLine();
        ImGui::PushID(1000 + i);
        bool active = hdriPreset == i && hdri.Loaded();
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.54f, 0.37f, 0.22f, 1.0f));
        if (hdriThumbs[i].Valid()) {
            if (ImGui::ImageButton("##hdri", hdriThumbs[i].Id(), ImVec2(cell - 8, (cell - 8) * 0.5f))) ApplyHdriPreset(i);
        } else if (ImGui::Button(HdriPresetName((HdriPreset)i), ImVec2(cell, 32))) ApplyHdriPreset(i);
        if (active) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("%s\n%s", HdriPresetName((HdriPreset)i), HdriPresetHint((HdriPreset)i));
        ImGui::PopID();
    }
    ImGui::Spacing();
    if (ImGui::Button(hdri.Loaded() ? "Load Custom HDRI..." : "Load HDRI...", ImVec2(-1, 0))) RequestHdriImport();
    ImGui::BeginDisabled(!hdri.Loaded());
    ImGui::PushItemWidth(-110.0f);
if (ImGui::SliderFloat("HDRI Strength", &hdriStrength, 0.0f, 5.0f, "%.2f")) hdriBgDirty = true;
    float deg = hdriRotation * 57.2957795f;
    if (ImGui::SliderFloat("HDRI Rotation", &deg, -180.0f, 180.0f, "%.0f deg")) hdriRotation = deg / 57.2957795f;
    ImGui::PopItemWidth();
    ImGui::Checkbox("Use as viewport background", &hdriAsBackground);
    if (ImGui::Button("Clear HDRI", ImVec2(-1, 0))) { hdri.Clear(); hdriPath.clear(); hdriBgDirty = true; ShowStatus("Environment cleared"); }
    ImGui::EndDisabled();
    ImGui::TextDisabled("Shift + right-drag in the viewport spins the environment.");

    ImGui::Spacing(); ImGui::TextColored(kBrass, "SUN"); ImGui::Separator();
    ImGui::SliderFloat("Elevation", &world.sunElevation, -90.0f, 90.0f, "%.0f deg");
    ImGui::SliderFloat("Rotation", &world.sunAzimuth, -180.0f, 180.0f, "%.0f deg");
    ImGui::ColorEdit3("Sun Color", world.sunColor, ImGuiColorEditFlags_NoInputs);
    ImGui::SliderFloat("Sun Strength", &world.sunStrength, 0.0f, 8.0f, "%.2f");

    ImGui::Spacing(); ImGui::TextColored(kBrass, "FILM"); ImGui::Separator();
    ImGui::SliderFloat("Exposure", &world.exposure, 0.05f, 8.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
    ImGui::Checkbox("Transparent background", &world.filmTransparent);
    ImGui::Checkbox("Studio light in Solid mode", &world.studioLightInSolid);

    ImGui::Spacing(); ImGui::TextColored(kBrass, "WIREFRAME"); ImGui::Separator();
    ImGui::ColorEdit3("Wire colour", wireColor, ImGuiColorEditFlags_NoInputs);
    ImGui::SliderFloat("Glow", &meshPrefs.wireGlow, 0.0f, 2.0f, "%.2f");
    meshPrefs.wireNeon = IM_COL32((int)(wireColor[0] * 255), (int)(wireColor[1] * 255), (int)(wireColor[2] * 255), 255);
    ImGui::PopItemWidth();
    ImGui::Spacing();
    Hint("HDRI environment maps and true path-traced lighting arrive with the Vulkan ray tracer; "
         "these settings already drive the viewport shading you see now.");
    ImGui::End();
}

// ---------------------------------------------------------------- undo helpers
// Rough sizes so the undo memory cap also counts armature and keyframe steps.
template <class T> static size_t UndoBytes(const T&) { return sizeof(T); }
static size_t UndoBytes(const Armature& a) {
    return sizeof(Armature) + a.bones.size() * sizeof(Bone) + a.weights.size() * sizeof(VertexWeights) +
           a.restPositions.size() * sizeof(Float3);
}
static size_t UndoBytes(const Action& a) {
    size_t n = sizeof(Action);
    for (const Channel& c : a.channels) n += sizeof(Channel) + c.keys.size() * sizeof(Keyframe);
    return n;
}

template <class T, class F>
void App::PushObjectUndo(const std::string& label, T before, T after, F apply, size_t bytes) {
    int idx = scene.activeIndex;
    if (bytes == 0) bytes = UndoBytes(before) + UndoBytes(after);
    auto b = std::make_shared<T>(std::move(before)), a = std::make_shared<T>(std::move(after));
    auto run = [this, idx, apply](const std::shared_ptr<T>& v) {
        if (idx >= 0 && idx < (int)scene.objects.size()) apply(*scene.objects[idx], *v);
    };
    undoStack.Push(label, [run, b] { run(b); }, [run, a] { run(a); }, bytes);
}

// The object itself moves between the scene and the step, so undo/redo bring back that exact object
// (material, modifiers and all) and find it by identity, not by a list index that may have shifted.
void App::PushObjectListUndo(const std::string& label, MeshObject* obj, int index, std::unique_ptr<MeshObject> removed) {
    if (!obj) return;
    const bool added = !removed;
    auto held = std::make_shared<std::unique_ptr<MeshObject>>(std::move(removed));
    auto take = [this, obj, held] { int i = scene.IndexOf(obj); if (i >= 0) *held = scene.Take(i); uvLayout.valid = false; };
    auto put = [this, index, held] { if (*held) scene.Insert(index, std::move(*held)); uvLayout.valid = false; };
    size_t bytes = obj->mesh.positions.size() * sizeof(Float3) + obj->mesh.tris.size() * sizeof(Tri);
    if (added) undoStack.Push(label, take, put, bytes);
    else undoStack.Push(label, put, take, bytes);
}

// Runs `change` on the active material's texture slots and records the slots it altered. Only the
// CPU pixels are kept; the GPU copy is rebuilt from them on undo/redo.
void App::PushMaterialTextureUndo(const std::string& label, std::function<void()> change, int onlySlot) {
    MeshObject* o = scene.Active();
    if (!o) { change(); return; }
    constexpr int N = (int)MapSlot::Count;
    struct Slots { bool changed[N] = {}; TextureData cpu[N]; std::string paths[N]; };
    Slots before, after;
    auto watched = [onlySlot](int i) { return onlySlot < 0 || i == onlySlot; };
    for (int i = 0; i < N; ++i)
        if (watched(i)) { before.cpu[i] = o->material.cpu[i]; before.paths[i] = o->material.paths[i]; }
    change();
    size_t bytes = 0;
    bool any = false;
    for (int i = 0; i < N; ++i) {
        if (!watched(i)) continue;
        const Material& m = o->material;
        bool same = before.paths[i] == m.paths[i] && before.cpu[i].width == m.cpu[i].width &&
                    before.cpu[i].height == m.cpu[i].height && before.cpu[i].pixels == m.cpu[i].pixels;
        if (same) { before.cpu[i].Clear(); before.paths[i].clear(); continue; }
        before.changed[i] = after.changed[i] = any = true;
        after.cpu[i] = m.cpu[i]; after.paths[i] = m.paths[i];
        bytes += before.cpu[i].pixels.size() + after.cpu[i].pixels.size();
    }
    if (!any) return;
    PushObjectUndo(label, std::move(before), std::move(after), [this](MeshObject& obj, Slots& s) {
        for (int i = 0; i < N; ++i) {
            if (!s.changed[i]) continue;
            const TextureData& t = s.cpu[i];
            if (t.Valid()) SetMaterialMap(vk, obj.material, (MapSlot)i, t.pixels.data(), t.width, t.height, s.paths[i]);
            else { obj.material.maps[i].Destroy(vk); obj.material.cpu[i].Clear(); obj.material.paths[i].clear(); }
        }
    }, bytes);
}

// ---------------------------------------------------------------- material / texture set
void App::MaterialPanel() {
    if (!ImGui::Begin("Material", &showMaterial)) { ImGui::End(); return; }
    MeshObject* o = scene.Active();
    if (!o) { ImGui::TextDisabled("No object selected."); ImGui::End(); return; }
    Material& mat = o->material;
    // One undo step per drag: the snapshot is taken from the state at the start of the frame the
    // widget activated, because a slider can already change the value on that first click.
    static std::shared_ptr<Material> dragBefore;
    Material frameStart;
    CopyMaterialParams(frameStart, mat);
    auto pushParams = [&](const char* label, const Material& before) {
        Material after;
        CopyMaterialParams(after, mat);
        PushObjectUndo(std::string(label), before, std::move(after),
                       [](MeshObject& ob, Material& v) { CopyMaterialParams(ob.material, v); });
    };
    auto track = [&](const char* label) {
        if (ImGui::IsItemActivated()) { dragBefore = std::make_shared<Material>(); CopyMaterialParams(*dragBefore, frameStart); }
        if (ImGui::IsItemDeactivatedAfterEdit() && dragBefore) { pushParams(label, *dragBefore); dragBefore.reset(); }
    };
    ImGui::TextColored(kBrass, "%s", o->name.c_str());
    ImGui::PushItemWidth(-110.0f);
    ImGui::ColorEdit3("Base Color", mat.baseColor, ImGuiColorEditFlags_NoInputs); track("Edit Base Color");
    ImGui::SliderFloat("Roughness", &mat.roughness, 0.0f, 1.0f, "%.2f"); track("Edit Roughness");
    ImGui::SliderFloat("Metallic", &mat.metallic, 0.0f, 1.0f, "%.2f"); track("Edit Metallic");
    ImGui::Checkbox("Use textures", &mat.useTextures); track("Toggle Use Textures");
    ImGui::PopItemWidth();

    ImGui::Spacing();
    ImGui::TextColored(kBrass, "SURFACE");
    ImGui::Separator();
    ImGui::PushItemWidth(-110.0f);
    ImGui::SliderFloat("Specular", &mat.specular, 0.0f, 1.0f, "%.2f"); track("Edit Specular");
    ImGui::SliderFloat("Opacity", &mat.opacity, 0.0f, 1.0f, "%.2f"); track("Edit Opacity");
    ImGui::PopItemWidth();
    ImGui::Checkbox("Parallax occlusion (height map)", &mat.parallax); track("Toggle Parallax");
    ImGui::BeginDisabled(!mat.parallax);
    ImGui::PushItemWidth(-110.0f);
    ImGui::SliderFloat("Parallax Depth", &mat.parallaxScale, 0.0f, 0.2f, "%.3f"); track("Edit Parallax Depth");
    ImGui::SliderInt("Parallax Steps", &mat.parallaxSteps, 4, 64); track("Edit Parallax Steps");
    ImGui::PopItemWidth();
    ImGui::EndDisabled();
    if (mat.parallax && !mat.HasCpu(MapSlot::Height)) ImGui::TextDisabled("Needs a Height map.");

    ImGui::Spacing();
    ImGui::TextColored(kBrass, "EMISSION & TRANSMISSION");
    ImGui::Separator();
    ImGui::PushItemWidth(-110.0f);
    ImGui::ColorEdit3("Emission Color", mat.emissionColor, ImGuiColorEditFlags_NoInputs); track("Edit Emission Color");
    ImGui::SliderFloat("Emission", &mat.emissiveStrengthRT, 0.0f, 20.0f, "%.2f", ImGuiSliderFlags_Logarithmic); track("Edit Emission");
    if (mat.Has(MapSlot::Emissive)) ImGui::TextDisabled("Emissive map drives the colour.");
    else if (mat.emissionColor[0] + mat.emissionColor[1] + mat.emissionColor[2] <= 0.0f)
        ImGui::TextDisabled("Emission colour is black, so nothing is emitted yet.");
    ImGui::SliderFloat("Transmission", &mat.transmission, 0.0f, 1.0f, "%.2f"); track("Edit Transmission");
    ImGui::BeginDisabled(mat.transmission <= 0.0f);
    ImGui::SliderFloat("IOR##trans", &mat.transmissionIOR, 1.0f, 2.5f, "%.2f"); track("Edit IOR");
    ImGui::EndDisabled();
    ImGui::PopItemWidth();
    ImGui::TextDisabled("Emission and glass apply in the path tracer (F12).");

    ImGui::Spacing();
    ImGui::TextColored(kBrass, "SUBSURFACE SCATTERING");
    ImGui::Separator();
    ImGui::PushItemWidth(-110.0f);
    ImGui::SliderFloat("Weight", &mat.sssWeight, 0.0f, 1.0f, "%.3f"); track("Edit Weight");
    ImGui::BeginDisabled(mat.sssWeight <= 0.0f);
    static const char* methods[] = {"Christensen-Burley", "Random Walk"};
    if (ImGui::Combo("Method", &mat.sssMethod, methods, 2)) pushParams("Edit Method", frameStart);
    ImGui::ColorEdit3("Radius", mat.sssRadius, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_Float); track("Edit Radius");
    ImGui::SliderFloat("Scale", &mat.sssScale, 0.0f, 1.0f, "%.3f m", ImGuiSliderFlags_Logarithmic); track("Edit Scale");
    ImGui::SliderFloat("Anisotropy", &mat.sssAnisotropy, -1.0f, 1.0f, "%.2f"); track("Edit Anisotropy");
    ImGui::SliderFloat("IOR", &mat.sssIOR, 1.0f, 2.5f, "%.2f"); track("Edit IOR");
    ImGui::EndDisabled();
    ImGui::PopItemWidth();
    if (mat.sssWeight > 0.0f && shading != ShadingMode::Rendered)
        ImGui::TextDisabled("Switch to Rendered to see it.");
    Hint("The viewport approximates scattering with wrapped diffuse and back-lit transmission "
         "tinted by the radius. The real per-channel diffusion profile needs the path tracer.");

    ImGui::Spacing();
    if (ImGui::Button("Load Texture Set...  (Shift+T)", ImVec2(-1, 0))) RequestTextureSetImport();
    Hint("Pick one or more images. The map each one goes into is read from its file name, "
         "the way Node Wrangler does it: _BaseColor, _Normal, _Roughness, _Metallic, _AO, _Height, _Emissive.");

    ImGui::Spacing(); ImGui::TextColored(kBrass, "MAPS"); ImGui::Separator();
    for (int i = 0; i < (int)MapSlot::Count; ++i) {
        MapSlot slot = (MapSlot)i;
        ImGui::PushID(i);
        bool has = mat.Has(slot);
        if (has) {
            ImGui::Image(mat.maps[i].Id(), ImVec2(44, 44));
            ImGui::SameLine();
        } else {
            ImGui::Dummy(ImVec2(44, 44)); ImGui::SameLine();
        }
        ImGui::BeginGroup();
        ImGui::TextColored(has ? kBrass : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), "%s", MapSlotName(slot));
        if (has) {
            std::string p = mat.paths[i];
            size_t slash = p.find_last_of("/\\");
            ImGui::TextDisabled("%s  (%d x %d)", (slash == std::string::npos ? p : p.substr(slash + 1)).c_str(),
                                mat.maps[i].width, mat.maps[i].height);
        } else {
            ImGui::TextDisabled("empty");
        }
        ImGui::EndGroup();
        // Drop a painted map from the Maps panel straight onto this slot.
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("LEORIX_MAP")) {
                int mapIndex = *(const int*)payload->Data;
                if (mapIndex >= 0 && mapIndex < (int)paint.maps.size()) {
                    MapDoc& src = paint.maps[mapIndex];
                    src.Composite();
                    bool ok = false;
                    PushMaterialTextureUndo(std::string("Assign ") + MapSlotName(slot), [&] {
                        ok = SetMaterialMap(vk, mat, slot, src.composite.data(), src.width, src.height,
                                            src.name + " (painted)");
                    }, (int)slot);
                    if (ok)
                        ShowStatus("Assigned " + src.name + " to " + MapSlotName(slot));
                    else ShowStatus("Could not assign that map");
                    o->mesh.EnsureUVs();
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::PopID();
    }
    ImGui::Spacing();
    Hint("Drag a map from the Maps panel onto a slot to assign it. Base Color shows in Material Preview and Rendered now. Normal, Roughness, Metallic and the "
         "rest are stored and exported, but shading them needs the GPU material pass (next milestone).");
    ImGui::End();
}

static void SDLCALL OnTextureFilesChosen(void* userdata, const char* const* filelist, int) {
    auto* app = static_cast<App*>(userdata);
    if (!filelist || !filelist[0]) return;
    std::vector<std::string> files;
    for (int i = 0; filelist[i]; ++i) files.push_back(filelist[i]);
    app->QueueTextureFiles(files);
}

void App::RequestTextureSetImport() {
    if (!scene.Active()) { ShowStatus("Select an object first."); return; }
    static const SDL_DialogFileFilter filters[] = {
        {"Images", "png;jpg;jpeg;tga;bmp;tif;tiff"},
        {"All files", "*"},
    };
    SDL_ShowOpenFileDialog(OnTextureFilesChosen, this, window, filters, 2, nullptr, true); // allow many
}

void App::QueueTextureFiles(const std::vector<std::string>& files) {
    std::lock_guard<std::mutex> lock(importMutex);
    pendingTextureFiles = files;
}

// Node Wrangler style: one call assigns every file to the slot its name implies.
void App::ApplyTextureFiles(const std::vector<std::string>& files) {
    MeshObject* o = scene.Active();
    if (!o || files.empty()) return;
    int loaded = 0, skipped = 0;
    std::string firstError;
    PushMaterialTextureUndo("Load Texture Set", [&] {
    for (const std::string& f : files) {
        MapSlot slot = GuessMapSlot(f);
        if (slot == MapSlot::Count) {
            // A single unnamed file is almost always the base colour.
            if (files.size() == 1) slot = MapSlot::BaseColor;
            else { ++skipped; continue; }
        }
        std::string error;
        if (LoadMaterialMap(vk, o->material, slot, f, error)) ++loaded;
        else if (firstError.empty()) firstError = error;
    }
    });
    o->mesh.EnsureUVs();
    // Jump to Material Preview so the maps are visible, unless a mode was chosen explicitly.
    if (loaded && shading == ShadingMode::Solid && !shadingChosen) shading = ShadingMode::Material;
    char msg[160];
    std::snprintf(msg, sizeof(msg), "Loaded %d map%s%s", loaded, loaded == 1 ? "" : "s",
                  skipped ? " (some names didn't match a map type)" : "");
    ShowStatus(loaded ? msg : (firstError.empty() ? "No textures matched a map slot" : firstError));
}

// ---------------------------------------------------------------- camera
void App::SyncViewportToCamera() {
    const SceneCamera& c = scene.Camera();
    viewport.SetOrbit(c.yaw, c.pitch);
    viewport.orthographic = c.orthographic;
    viewport.orthoScale = c.orthoScale;
    viewport.viewLabel = "Camera";
}

void App::AlignCameraToView() {
    SceneCamera& c = scene.Camera();
    c.yaw = viewport.Yaw();
    c.pitch = viewport.Pitch();
    c.distance = viewport.Distance();
    Vec3 t = viewport.Target();
    c.target = {t.x, t.y, t.z};
    c.orthographic = viewport.orthographic;
    c.orthoScale = viewport.orthoScale;
    ShowStatus("Camera aligned to view");
}

void App::CameraPanel() {
    if (!ImGui::Begin("Camera", &showCamera)) { ImGui::End(); return; }
    // Edits apply to the selected camera, or to the active one when none is selected.
    int editing = CameraSelected() ? selectedCamera : scene.activeCamera;
    editing = std::clamp(editing, 0, (int)scene.cameras.size() - 1);
    SceneCamera& c = scene.cameras[editing];
    ImGui::TextColored(kBrass, "%s", scene.cameraNames[editing].c_str());
    ImGui::SameLine();
    if (editing == scene.activeCamera) ImGui::TextDisabled("(active)");
    else if (ImGui::SmallButton("Make Active")) { scene.activeCamera = editing; cameraDirty = true; }
    ImGui::Separator();
    ImGui::TextColored(kBrass, "LENS");
    ImGui::PushItemWidth(-120.0f);
    int type = c.orthographic ? 1 : 0;
    static const char* types[] = {"Perspective", "Orthographic"};
    if (ImGui::Combo("Type", &type, types, 2)) { c.orthographic = type == 1; cameraDirty = true; }
    if (c.orthographic) {
        if (ImGui::SliderFloat("Ortho Scale", &c.orthoScale, 0.1f, 100.0f, "%.2f m", ImGuiSliderFlags_Logarithmic)) cameraDirty = true;
    } else {
        if (ImGui::SliderFloat("Focal Length", &c.focalLengthMm, 8.0f, 300.0f, "%.0f mm")) cameraDirty = true;
        if (ImGui::SliderFloat("Sensor Width", &c.sensorWidthMm, 4.0f, 70.0f, "%.1f mm")) cameraDirty = true;
        ImGui::TextDisabled("Field of view: %.1f deg", c.FovYDegrees(16.0f / 9.0f));
    }
    ImGui::SliderFloat("Clip Start", &c.clipStart, 0.001f, 10.0f, "%.3f m", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("Clip End", &c.clipEnd, 1.0f, 10000.0f, "%.0f m", ImGuiSliderFlags_Logarithmic);

    ImGui::Spacing(); ImGui::TextColored(kBrass, "DEPTH OF FIELD"); ImGui::Separator();
    ImGui::Checkbox("Enable##dof", &c.dof);
    ImGui::BeginDisabled(!c.dof);
    ImGui::SliderFloat("Focus Distance", &c.focusDistance, 0.05f, 200.0f, "%.2f m", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("F-Stop", &c.fStop, 0.95f, 22.0f, "f/%.1f", ImGuiSliderFlags_Logarithmic);
    ImGui::EndDisabled();

    ImGui::Spacing(); ImGui::TextColored(kBrass, "VIEWPORT DISPLAY"); ImGui::Separator();
    ImGui::Checkbox("Passepartout", &c.showPassepartout);
    ImGui::BeginDisabled(!c.showPassepartout);
    ImGui::SliderFloat("Dim", &c.passepartoutAlpha, 0.0f, 1.0f, "%.2f");
    ImGui::EndDisabled();
    ImGui::PopItemWidth();

    ImGui::Spacing(); ImGui::TextColored(kBrass, "TRANSFORM"); ImGui::Separator();
    ImGui::PushItemWidth(-120.0f);
    {
        Float3 ut = ToUser(c.target);
        if (ImGui::DragFloat3("Look At", &ut.x, 0.02f, 0, 0, "%.2f m")) { c.target = FromUser(ut); cameraDirty = true; }
    }
    float yawDeg = c.yaw * 57.2957795f, pitchDeg = c.pitch * 57.2957795f;
    if (ImGui::DragFloat("Yaw", &yawDeg, 0.5f, -360.0f, 360.0f, "%.1f deg")) { c.yaw = yawDeg / 57.2957795f; cameraDirty = true; }
    if (ImGui::DragFloat("Pitch", &pitchDeg, 0.5f, -89.0f, 89.0f, "%.1f deg")) { c.pitch = pitchDeg / 57.2957795f; cameraDirty = true; }
    if (ImGui::DragFloat("Distance", &c.distance, 0.05f, 0.1f, 500.0f, "%.2f m")) cameraDirty = true;
    ImGui::PopItemWidth();
    {
        bool sel = CameraSelected();
        if (ImGui::Checkbox("Select camera (G moves it, R aims it)", &sel)) selectedCamera = sel ? scene.activeCamera : -1;
    }
    if (CameraSelected()) ImGui::TextDisabled("G then move the mouse, R to aim. Esc cancels.");

    ImGui::Spacing();
    if (ImGui::Button(cameraView ? "Leave Camera View  (Numpad 0)" : "View from Camera  (Numpad 0)", ImVec2(-1, 0))) {
        cameraView = !cameraView;
        if (cameraView) SyncViewportToCamera(); else viewport.viewLabel = "User";
    }
    if (ImGui::Button("Align Camera to View  (Ctrl+Alt+Numpad 0)", ImVec2(-1, 0))) AlignCameraToView();
    Hint("Depth of field and the exact frame apply once the ray tracer renders; the viewport uses "
         "the focal length and type now.");
    ImGui::End();
}

// ---------------------------------------------------------------- Blender numpad view shortcuts
void App::HandleViewShortcuts(bool viewportHovered) {
    if (!viewportHovered || ImGui::GetIO().WantTextInput || showNewProject) return;
    ImGuiIO& io = ImGui::GetIO();
    bool ctrl = io.KeyCtrl;

    struct Pad { ImGuiKey key; ImGuiKey alt; };
    auto pressed = [&](ImGuiKey pad, ImGuiKey row) {
        return ImGui::IsKeyPressed(pad, false) || ImGui::IsKeyPressed(row, false);
    };

    if (pressed(ImGuiKey_Keypad1, ImGuiKey_None)) { viewport.SetAxisView(1, ctrl); cameraView = false; }
    if (pressed(ImGuiKey_Keypad3, ImGuiKey_None)) { viewport.SetAxisView(3, ctrl); cameraView = false; }
    if (pressed(ImGuiKey_Keypad7, ImGuiKey_None)) { viewport.SetAxisView(7, ctrl); cameraView = false; }
    if (pressed(ImGuiKey_Keypad9, ImGuiKey_None)) { viewport.OrbitSteps(180.0f, 0.0f); cameraView = false; }
    if (pressed(ImGuiKey_Keypad5, ImGuiKey_None)) {
        viewport.ToggleOrtho();
        ShowStatus(viewport.orthographic ? "Orthographic" : "Perspective");
    }
    if (pressed(ImGuiKey_Keypad4, ImGuiKey_None)) viewport.OrbitSteps(15.0f, 0.0f);
    if (pressed(ImGuiKey_Keypad6, ImGuiKey_None)) viewport.OrbitSteps(-15.0f, 0.0f);
    if (pressed(ImGuiKey_Keypad8, ImGuiKey_None)) viewport.OrbitSteps(0.0f, 15.0f);
    if (pressed(ImGuiKey_Keypad2, ImGuiKey_None)) viewport.OrbitSteps(0.0f, -15.0f);
    if (ImGui::IsKeyPressed(ImGuiKey_Keypad0, false) && ctrl && !io.KeyAlt && CameraSelected()) {
        scene.activeCamera = selectedCamera;
        cameraView = true;
        cameraDirty = true;
        SyncViewportToCamera();
        ShowStatus("Active camera: " + scene.cameraNames[scene.activeCamera]);
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Keypad0, false)) {
        if (ctrl && io.KeyAlt) { AlignCameraToView(); cameraView = true; SyncViewportToCamera(); }
        else { cameraView = !cameraView; if (cameraView) SyncViewportToCamera(); else viewport.viewLabel = "User"; }
        ShowStatus(cameraView ? "Camera view" : "User view");
    }
    // Numpad . frames the selection, Home frames everything
    if (ImGui::IsKeyPressed(ImGuiKey_KeypadDecimal, false) || ImGui::IsKeyPressed(ImGuiKey_Period, false)) {
        if (MeshObject* o = scene.Active()) {
            o->mesh.ComputeBounds();
            Float3 mn = o->transform.Apply(o->mesh.bmin), mx = o->transform.Apply(o->mesh.bmax);
            Vec3 c{(mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f, (mn.z + mx.z) * 0.5f};
            float r = std::sqrt((mx.x - mn.x) * (mx.x - mn.x) + (mx.y - mn.y) * (mx.y - mn.y) + (mx.z - mn.z) * (mx.z - mn.z)) * 0.5f;
            viewport.FrameBounds(c, r);
            ShowStatus("Framed " + o->name);
        }
    }
}

// ---------------------------------------------------------------- armature
void App::UpdateSkinning(MeshObject& o) { SkinWithLinks(scene, o); }   // also meshes linked to o's bones

void App::ArmaturePanel() {
    if (!ImGui::Begin("Armature", &showArmature)) { ImGui::End(); return; }
    MeshObject* o = scene.Active();
    if (!o) { ImGui::TextDisabled("No object selected."); ImGui::End(); return; }
    Armature& arm = o->armature;
    // Structural edits snapshot the whole armature; pose drags only the bone being dragged.
    auto pushArm = [&](const char* label, Armature before) {
        PushObjectUndo(std::string(label), std::move(before), arm, [this](MeshObject& ob, Armature& a) {
            ob.armature = a;
            if (activeBone >= (int)a.bones.size()) activeBone = -1;
            UpdateSkinning(ob);
        });
    };

    ImGui::TextColored(kBrass, "%s", o->name.c_str());
    ImGui::TextDisabled("%zu bone%s  |  %s", arm.bones.size(), arm.bones.size() == 1 ? "" : "s",
                        arm.Bound() ? "skinned" : "not parented");
    ImGui::Separator();

    if (ImGui::Button("Add Bone", ImVec2(-1, 0))) {
        Armature before = arm;
        o->mesh.ComputeBounds();
        Float3 mn = o->mesh.bmin, mx = o->mesh.bmax;
        Float3 head{(mn.x + mx.x) * 0.5f, mn.y, (mn.z + mx.z) * 0.5f};
        Float3 tail{head.x, mn.y + (mx.y - mn.y) * 0.5f, head.z};
        if (activeBone >= 0 && activeBone < (int)arm.bones.size()) {
            // chain from the selected bone's tail, the way Blender extrudes a bone
            const Bone& p = arm.bones[activeBone];
            head = p.tail;
            Float3 dir{p.tail.x - p.head.x, p.tail.y - p.head.y, p.tail.z - p.head.z};
            tail = {head.x + dir.x, head.y + dir.y, head.z + dir.z};
        }
        activeBone = arm.AddBone("Bone." + std::to_string(arm.bones.size() + 1), head, tail,
                                 activeBone >= 0 ? activeBone : -1);
        pushArm("Add Bone", std::move(before));
        ShowStatus("Added bone (re-parent with automatic weights to bind it)");
    }
    ImGui::BeginDisabled(arm.bones.empty());
    if (ImGui::Button("Parent to Mesh with Automatic Weights", ImVec2(-1, 0))) {
        Armature before = arm;
        arm.BindAutomaticWeights(o->mesh);
        UpdateSkinning(*o);
        pushArm("Automatic Weights", std::move(before));
        ShowStatus("Bound " + std::to_string(arm.bones.size()) + " bones to " + std::to_string(o->mesh.VertexCount()) + " vertices");
    }
    ImGui::EndDisabled();
    if (ImGui::Checkbox("Enable deformation", &arm.enabled)) {
        Armature before = arm;
        before.enabled = !arm.enabled;
        pushArm("Toggle Deformation", std::move(before));
    }
    std::string rigMsg = RigPresetUI(arm, o->mesh, [&](const char* label, const std::function<void(Armature&)>& fn) {
        Armature before = arm;
        fn(arm);
        UpdateSkinning(*o);
        pushArm(label, std::move(before));
    });
    if (!rigMsg.empty()) ShowStatus(rigMsg);
    ArmatureLinkUI(*o);

    ImGui::Spacing(); ImGui::TextColored(kBrass, "BONES"); ImGui::Separator();
    int removeBone = -1;
    for (int i = 0; i < (int)arm.bones.size(); ++i) {
        Bone& b = arm.bones[i];
        ImGui::PushID(i);
        int depth = 0;
        for (int p = b.parent; p >= 0 && depth < 8; p = arm.bones[p].parent) ++depth;
        ImGui::Dummy(ImVec2(depth * 12.0f, 0)); ImGui::SameLine();
        char label[128];
        std::snprintf(label, sizeof(label), "%s%s##bone", b.name.c_str(), b.parent >= 0 ? "" : "  (root)");
        if (ImGui::Selectable(label, activeBone == i)) activeBone = i;
        if (ImGui::BeginPopupContextItem("##bonectx")) {
            if (ImGui::MenuItem("Delete")) removeBone = i;
            if (ImGui::MenuItem("Deform", nullptr, &b.deform)) {
                Armature before = arm;
                before.bones[i].deform = !b.deform;
                pushArm("Toggle Bone Deform", std::move(before));
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    if (removeBone >= 0) {
        Armature before = arm;
        arm.Remove(removeBone); activeBone = -1; UpdateSkinning(*o);
        pushArm("Delete Bone", std::move(before));
    }

    if (activeBone >= 0 && activeBone < (int)arm.bones.size()) {
        Bone& b = arm.bones[activeBone];
        static std::shared_ptr<Bone> dragBefore;
        const Bone frameStart = b;     // state before this frame's edit (sliders change on the first click)
        auto pushBone = [&](const char* label, Bone before) {
            int bi = activeBone;
            PushObjectUndo(std::string(label), std::move(before), b, [this, bi](MeshObject& ob, Bone& v) {
                if (bi < 0 || bi >= (int)ob.armature.bones.size()) return;
                ob.armature.bones[bi] = v;
                UpdateSkinning(ob);
            });
        };
        auto track = [&](const char* label) {
            if (ImGui::IsItemActivated()) dragBefore = std::make_shared<Bone>(frameStart);
            if (ImGui::IsItemDeactivatedAfterEdit() && dragBefore) { pushBone(label, *dragBefore); dragBefore.reset(); }
        };
        ImGui::Spacing(); ImGui::TextColored(kBrass, "POSE  -  %s", b.name.c_str()); ImGui::Separator();
        ImGui::PushItemWidth(-90.0f);
        bool changed = false;
        Float3 deg{b.poseRotation.x * 57.2957795f, b.poseRotation.y * 57.2957795f, b.poseRotation.z * 57.2957795f};
        if (ImGui::DragFloat3("Rotation", &deg.x, 0.5f, 0, 0, "%.1f deg")) {
            b.poseRotation = {deg.x / 57.2957795f, deg.y / 57.2957795f, deg.z / 57.2957795f};
            changed = true;
        }
        track("Rotate Bone");
        changed |= ImGui::DragFloat3("Location", &b.poseLocation.x, 0.01f, 0, 0, "%.3f m");
        track("Move Bone");
        changed |= ImGui::DragFloat3("Scale", &b.poseScale.x, 0.01f, 0.01f, 100.0f, "%.3f");
        track("Scale Bone");
        changed |= ImGui::SliderFloat("Envelope", &b.envelope, 0.05f, 8.0f, "%.2f");
        track("Edit Envelope");
        ImGui::PopItemWidth();
        if (changed) UpdateSkinning(*o);      // deformation updates while you drag
        if (ImGui::Button("Reset Pose")) {
            b.poseRotation = {0, 0, 0}; b.poseLocation = {0, 0, 0}; b.poseScale = {1, 1, 1};
            UpdateSkinning(*o);
            pushBone("Reset Pose", frameStart);
        }
        ImGui::SameLine();
        if (ImGui::Button("Insert Keyframe  (I)")) InsertKeyframes(true, true, true);
        std::vector<std::pair<uint32_t, std::string>> targets;   // objects a constraint can follow
        for (const auto& up : scene.objects) if (up.get() != o && up->id) targets.push_back({up->id, up->name});
        int con = BoneConstraintsUI(arm, activeBone, b, track, targets);
        if (con) UpdateSkinning(*o);
        if (con == 2) pushBone("Edit Constraint", frameStart);
    }
    ImGui::Spacing();
    Hint("Automatic weights use distance falloff (envelope), not Blender's heat solver: instant and "
         "predictable, but it ignores topology, so limbs close to the body can pick up the wrong bone.");
    ImGui::End();
}

// ---------------------------------------------------------------- animation
void App::ApplyAnimation(float frame) {
    for (auto& up : scene.objects) {
        MeshObject& o = *up;
        if (o.action.Empty()) continue;
        bool poseChanged = false;
        for (const Channel& c : o.action.channels) {
            float v;
            if (!c.Evaluate(frame, v)) continue;
            if (c.target == ChannelTarget::Object) {
                Float3* t = c.prop == ChannelProp::Location ? &o.transform.position
                          : c.prop == ChannelProp::Rotation ? &o.transform.rotation : &o.transform.scale;
                (&t->x)[std::clamp(c.axis, 0, 2)] = v;
            } else if (c.boneIndex >= 0 && c.boneIndex < (int)o.armature.bones.size()) {
                Bone& b = o.armature.bones[c.boneIndex];
                Float3* t = c.prop == ChannelProp::Location ? &b.poseLocation
                          : c.prop == ChannelProp::Rotation ? &b.poseRotation : &b.poseScale;
                (&t->x)[std::clamp(c.axis, 0, 2)] = v;
                poseChanged = true;
            }
        }
        if (poseChanged) UpdateSkinning(o);
    }
    ApplyRigidBodyCache(scene, frame);   // baked physics wins over keyframes, as in Blender
}

void App::InsertKeyframes(bool location, bool rotation, bool scale) {
    MeshObject* o = scene.Active();
    if (!o) return;
    int f = (int)std::lround(playback.frame);
    bool onBone = mode3D == Mode3D::Pose && activeBone >= 0 && activeBone < (int)o->armature.bones.size();
    ChannelTarget target = onBone ? ChannelTarget::Bone : ChannelTarget::Object;
    int bone = onBone ? activeBone : -1;
    Action before = o->action;

    auto key = [&](ChannelProp prop, const Float3& v) {
        for (int axis = 0; axis < 3; ++axis)
            o->action.Find(target, bone, prop, axis).Insert(f, (&v.x)[axis]);
    };
    if (onBone) {
        const Bone& b = o->armature.bones[bone];
        if (location) key(ChannelProp::Location, b.poseLocation);
        if (rotation) key(ChannelProp::Rotation, b.poseRotation);
        if (scale)    key(ChannelProp::Scale, b.poseScale);
    } else {
        if (location) key(ChannelProp::Location, o->transform.position);
        if (rotation) key(ChannelProp::Rotation, o->transform.rotation);
        if (scale)    key(ChannelProp::Scale, o->transform.scale);
    }
    PushObjectUndo("Insert Keyframe", std::move(before), o->action, [](MeshObject& ob, Action& a) { ob.action = a; });
    showTimeline = true;
    char msg[96];
    std::snprintf(msg, sizeof(msg), "Keyframed %s at frame %d", onBone ? o->armature.bones[bone].name.c_str() : o->name.c_str(), f);
    ShowStatus(msg);
}

void App::BakeAnimation() {
    MeshObject* o = scene.Active();
    if (!o || o->action.Empty()) { ShowStatus("Nothing to bake"); return; }
    Action baked;
    baked.name = o->action.name + " (baked)";
    baked.frameStart = o->action.frameStart; baked.frameEnd = o->action.frameEnd;
    float saved = playback.frame;
    for (int f = baked.frameStart; f <= baked.frameEnd; ++f) {
        ApplyAnimation((float)f);
        for (const Channel& c : o->action.channels) {
            float v = 0;
            if (c.target == ChannelTarget::Object) {
                const Float3& t = c.prop == ChannelProp::Location ? o->transform.position
                                : c.prop == ChannelProp::Rotation ? o->transform.rotation : o->transform.scale;
                v = (&t.x)[std::clamp(c.axis, 0, 2)];
            } else if (c.boneIndex >= 0 && c.boneIndex < (int)o->armature.bones.size()) {
                const Bone& b = o->armature.bones[c.boneIndex];
                const Float3& t = c.prop == ChannelProp::Location ? b.poseLocation
                                : c.prop == ChannelProp::Rotation ? b.poseRotation : b.poseScale;
                v = (&t.x)[std::clamp(c.axis, 0, 2)];
            }
            baked.Find(c.target, c.boneIndex, c.prop, c.axis).Insert(f, v, Interp::Linear);
        }
    }
    PushObjectUndo("Bake Animation", o->action, baked, [](MeshObject& ob, Action& a) { ob.action = a; });
    o->action = baked;
    playback.frame = saved;
    ApplyAnimation(playback.frame);
    char msg[96];
    std::snprintf(msg, sizeof(msg), "Baked %d frames into %zu channels", baked.frameEnd - baked.frameStart + 1, baked.channels.size());
    ShowStatus(msg);
}

void App::TimelinePanel() {
    if (!ImGui::Begin("Timeline", &showTimeline)) { ImGui::End(); return; }
    MeshObject* o = scene.Active();
    Action* act = o ? &o->action : nullptr;

    // transport
    if (ImGui::Button(playback.playing ? "Pause" : "Play", ImVec2(70, 0))) {
        playback.playing = !playback.playing;
        lastPlayTime = ImGui::GetTime();
    }
    ImGui::SameLine();
    if (ImGui::Button("|<")) { playback.frame = act ? (float)act->frameStart : 1.0f; ApplyAnimation(playback.frame); }
    ImGui::SameLine();
    if (ImGui::Button("<")) { playback.frame = std::max(1.0f, playback.frame - 1); ApplyAnimation(playback.frame); }
    ImGui::SameLine();
    if (ImGui::Button(">")) { playback.frame += 1; ApplyAnimation(playback.frame); }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    int f = (int)std::lround(playback.frame);
    if (ImGui::InputInt("Frame", &f, 0)) { playback.frame = (float)std::max(1, f); ApplyAnimation(playback.frame); }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70);
    ImGui::InputInt("FPS", &playback.fps, 0);
    playback.fps = std::clamp(playback.fps, 1, 240);
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &playback.loop);

    if (act) {
        ImGui::SetNextItemWidth(80); ImGui::InputInt("Start", &act->frameStart, 0); ImGui::SameLine();
        ImGui::SetNextItemWidth(80); ImGui::InputInt("End", &act->frameEnd, 0); ImGui::SameLine();
        if (ImGui::Button("Insert Key (I)")) InsertKeyframes(true, true, true);
        ImGui::SameLine();
        if (ImGui::Button("Delete Key")) {
            int cur = (int)std::lround(playback.frame);
            Action before = *act;
            for (auto& c : act->channels) c.RemoveAt(cur);
            PushObjectUndo("Delete Keyframe", std::move(before), *act, [](MeshObject& ob, Action& a) { ob.action = a; });
            ShowStatus("Deleted keys at frame " + std::to_string(cur));
        }
        ImGui::SameLine();
        if (ImGui::Button("Bake")) BakeAnimation();

        static int interpSel = (int)Interp::Bezier;
        static int easeSel = (int)Easing::Out;
        const char* interps[(int)Interp::Count];
        for (int i = 0; i < (int)Interp::Count; ++i) interps[i] = InterpName((Interp)i);
        ImGui::SetNextItemWidth(130);
        ImGui::Combo("##interp", &interpSel, interps, (int)Interp::Count);
        ImGui::SameLine();
        bool needsEasing = interpSel > (int)Interp::Bezier;
        ImGui::BeginDisabled(!needsEasing);
        static const char* eases[] = {"Ease In", "Ease Out", "Ease In-Out"};
        ImGui::SetNextItemWidth(110);
        ImGui::Combo("##ease", &easeSel, eases, 3);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Set Interpolation")) {
            int cur = (int)std::lround(playback.frame);
            int touched = 0;
            Action before = *act;
            for (auto& c : act->channels)
                for (auto& k : c.keys)
                    if (k.frame == cur) { k.interp = (Interp)interpSel; k.easing = (Easing)easeSel; ++touched; }
            if (touched == 0)      // nothing on this frame: apply to the whole action
                for (auto& c : act->channels) c.SetInterp((Interp)interpSel, (Easing)easeSel);
            PushObjectUndo("Set Interpolation", std::move(before), *act, [](MeshObject& ob, Action& a) { ob.action = a; });
            char msg[128];
            std::snprintf(msg, sizeof(msg), "%s%s%s applied to %s", InterpName((Interp)interpSel),
                          needsEasing ? " / " : "", needsEasing ? EasingName((Easing)easeSel) : "",
                          touched ? "the keys on this frame" : "every key");
            ShowStatus(msg);
        }
    }
    ImGui::Separator();

    if (!act) { ImGui::TextDisabled("Select an object to see its channels."); ImGui::End(); return; }

    // channel rows with key diamonds
    std::vector<std::string> boneNames;
    if (o) for (const auto& b : o->armature.bones) boneNames.push_back(b.name);

    const float labelW = 190.0f;
    ImVec2 area = ImGui::GetContentRegionAvail();
    float trackW = std::max(80.0f, area.x - labelW - 12.0f);
    int f0 = act->frameStart, f1 = std::max(act->frameStart + 1, act->frameEnd);
    auto frameToX = [&](float fr, float x0) { return x0 + (fr - f0) / float(f1 - f0) * trackW; };

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 topLeft = ImGui::GetCursorScreenPos();

    // ruler + scrub strip
    ImGui::InvisibleButton("##scrub", ImVec2(area.x, 22));
    if (ImGui::IsItemActive()) {
        float x = ImGui::GetIO().MousePos.x - (topLeft.x + labelW);
        playback.frame = std::clamp(f0 + x / trackW * (f1 - f0), (float)f0, (float)f1);
        ApplyAnimation(playback.frame);
    }
    float rulerY = topLeft.y;
    dl->AddRectFilled(ImVec2(topLeft.x + labelW, rulerY), ImVec2(topLeft.x + labelW + trackW, rulerY + 20), IM_COL32(26, 30, 38, 255), 3.0f);
    int step = std::max(1, (f1 - f0) / 12);
    for (int fr = f0; fr <= f1; fr += step) {
        float x = frameToX((float)fr, topLeft.x + labelW);
        dl->AddLine(ImVec2(x, rulerY + 12), ImVec2(x, rulerY + 20), IM_COL32(120, 128, 145, 255));
        char buf[16]; std::snprintf(buf, sizeof(buf), "%d", fr);
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.8f, ImVec2(x + 2, rulerY), IM_COL32(150, 158, 175, 255), buf);
    }

    if (act->channels.empty()) ImGui::TextDisabled("No keyframes yet. Pose something and press I.");
    float rowTop = rulerY + 24;
    for (size_t i = 0; i < act->channels.size(); ++i) {
        const Channel& c = act->channels[i];
        if (c.keys.empty()) continue;
        float y = rowTop + i * 18.0f;
        if (y > topLeft.y + area.y - 10) break;
        dl->AddText(ImVec2(topLeft.x + 4, y), IM_COL32(200, 205, 215, 255), c.Label(boneNames).c_str());
        dl->AddLine(ImVec2(topLeft.x + labelW, y + 8), ImVec2(topLeft.x + labelW + trackW, y + 8), IM_COL32(60, 66, 78, 255));
        for (const Keyframe& k : c.keys) {
            float x = frameToX((float)k.frame, topLeft.x + labelW);
            ImVec2 p(x, y + 8);
            ImU32 col = k.frame == (int)std::lround(playback.frame) ? IM_COL32(255, 200, 90, 255) : IM_COL32(210, 170, 90, 255);
            dl->AddQuadFilled(ImVec2(p.x, p.y - 5), ImVec2(p.x + 5, p.y), ImVec2(p.x, p.y + 5), ImVec2(p.x - 5, p.y), col);
        }
    }
    // playhead
    float px = frameToX(playback.frame, topLeft.x + labelW);
    dl->AddLine(ImVec2(px, rulerY), ImVec2(px, topLeft.y + area.y), IM_COL32(230, 150, 60, 220), 1.6f);
    ImGui::End();
}

// ---------------------------------------------------------------- 2D maps and painting
void App::SyncMapTexture(MapDoc& m) {
    m.Composite();
    if (!m.textureDirty || m.composite.empty()) return;
    if (m.texture.Valid() && m.texture.width == m.width && m.texture.height == m.height)
        m.texture.Update(vk, m.composite.data(), m.width, m.height);
    else
        m.texture.Create(vk, m.composite.data(), m.width, m.height);
    m.textureDirty = false;
}

// Painted maps feed the 3D material, so texturing a model in 2D shows up in Material Preview.
void App::ApplyMapsToMaterial() {
    MeshObject* o = scene.Active();
    if (!o) return;
    for (MapDoc& m : paint.maps) {
        if (m.kind != MapKind::Albedo || !m.texture.Valid()) continue;
        // SetMaterialMap also fills the CPU copy, which undo restores from and the path tracer samples.
        PushMaterialTextureUndo("Apply Base Color", [&] {
            SetMaterialMap(vk, o->material, MapSlot::BaseColor, m.composite.data(), m.width, m.height, m.name + " (painted)");
        }, (int)MapSlot::BaseColor);
        o->mesh.EnsureUVs();
        ShowStatus("Applied " + m.name + " to " + o->name);
        return;
    }
    ShowStatus("No Base Color map to apply");
}

void App::PaintStroke(ImVec2 canvasPos, bool begin, float sizePx) {
    MapDoc* m = paint.Active();
    if (!m) return;
    Layer* l = m->Active();
    if (!l) { ShowStatus("No layer selected"); return; }
    if (l->locked) { ShowStatus("Layer is locked"); return; }

    const auto& t = kTools2D[tool2D];
    const bool fromViewport = sizePx > 0.0f;
    bool erase = t.icon == Icon::Eraser;
    bool pencil = t.icon == Icon::Pencil && !fromViewport;
    if (!fromViewport && !erase && !pencil && t.icon != Icon::Brush && t.icon != Icon::Fill) return;

    uint32_t rgba = IM_COL32((int)(color[0] * 255), (int)(color[1] * 255), (int)(color[2] * 255), 255);
    if (t.icon == Icon::Fill && !fromViewport) {
        if (!begin) return;
        auto snap = std::make_shared<std::vector<uint8_t>>(l->pixels);
        FillLayer(*l, m->width, m->height, rgba, m->dirty);
        auto filled = std::make_shared<std::vector<uint8_t>>(l->pixels);
        int mi = paint.activeMap, li = m->activeLayer;
        auto restore = [this, mi, li](std::shared_ptr<std::vector<uint8_t>> px) {
            if (mi < 0 || mi >= (int)paint.maps.size()) return;
            MapDoc& md = paint.maps[mi];
            if (li < 0 || li >= (int)md.layers.size()) return;
            md.layers[li].pixels = *px;
            md.dirty.Whole(md.width, md.height);
        };
        undoStack.Push("Fill", [restore, snap] { restore(snap); }, [restore, filled] { restore(filled); },
                       snap->size() + filled->size());
        ShowStatus("Filled " + l->name);
        return;
    }

    BrushSettings b = brush2D;
    b.size = fromViewport ? sizePx : brushSize;
    b.opacity = brushOpacity;
    b.hardness = pencil ? 100.0f : brushHardness;   // the pencil is a hard round brush

    if (begin) {
        activeStroke = std::make_shared<StrokeUndo>();
        activeStroke->Begin(paint.activeMap, m->activeLayer, m->width, m->height);
    }
    // From the model, a jump in UV space means the brush crossed a seam: stamp, don't draw a line
    // across the texture between the two islands.
    const float jx = canvasPos.x - lastPaintPos.x, jy = canvasPos.y - lastPaintPos.y;
    const bool stamp = begin || (fromViewport && jx * jx + jy * jy > b.size * b.size * 4.0f);
    // save the tiles this dab or segment will touch, before it touches them
    if (activeStroke) {
        float r = b.size * 0.5f + 2.0f;
        float ax = stamp ? canvasPos.x : lastPaintPos.x, ay = stamp ? canvasPos.y : lastPaintPos.y;
        activeStroke->BackupRect(*l, (int)std::floor(std::min(ax, canvasPos.x) - r),
                                     (int)std::floor(std::min(ay, canvasPos.y) - r),
                                     (int)std::ceil(std::max(ax, canvasPos.x) + r),
                                     (int)std::ceil(std::max(ay, canvasPos.y) + r));
    }

    if (stamp) {
        strokeCarry = 0.0f;
        StampBrush(*l, m->width, m->height, canvasPos.x, canvasPos.y, b, rgba, erase, m->dirty);
    } else {
        StrokeSegment(*l, m->width, m->height, lastPaintPos.x, lastPaintPos.y,
                      canvasPos.x, canvasPos.y, b, rgba, erase, m->dirty, strokeCarry);
    }
    lastPaintPos = canvasPos;
}

void App::MapsPanel() {
    if (!ImGui::Begin("Maps", &showMaps)) { ImGui::End(); return; }
    ImGui::TextColored(kBrass, "TEXTURE MAPS");
    ImGui::TextDisabled("%d x %d", paint.width, paint.height);
    ImGui::Separator();

    int removeIndex = -1;
    for (int i = 0; i < (int)paint.maps.size(); ++i) {
        MapDoc& m = paint.maps[i];
        ImGui::PushID(i);
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImU32 dot = MapKindDot(m.kind);
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + 8, p.y + ImGui::GetTextLineHeight() * 0.5f), 5.0f, dot);
        ImGui::Dummy(ImVec2(18, 0)); ImGui::SameLine();
        char label[160];
        std::snprintf(label, sizeof(label), "%s##map", m.name.c_str());
        if (ImGui::Selectable(label, paint.activeMap == i)) paint.activeMap = i;
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload("LEORIX_MAP", &i, sizeof(int));
            ImGui::Text("%s", m.name.c_str());
            ImGui::TextDisabled("drop on a material slot");
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginPopupContextItem("##mapctx")) {
            if (ImGui::MenuItem("Delete", nullptr, false, paint.maps.size() > 1)) removeIndex = i;
            ImGui::EndPopup();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%zu layer%s", MapKindHint(m.kind),
                                                      m.layers.size(), m.layers.size() == 1 ? "" : "s");
        ImGui::PopID();
    }
    if (removeIndex >= 0) {
        // The snapshot drops the GPU handle (RemoveMap destroys it); SyncMapTexture recreates it.
        auto snap = std::make_shared<MapDoc>(paint.maps[removeIndex]);
        snap->texture = GpuTexture{};
        snap->textureDirty = true;
        size_t bytes = snap->composite.size();
        for (const auto& l : snap->layers) bytes += l.pixels.size();
        paint.RemoveMap(removeIndex, vk);
        undoStack.Push("Delete " + snap->name + " map",
            [this, snap, removeIndex] {
                int at = std::min(removeIndex, (int)paint.maps.size());
                paint.maps.insert(paint.maps.begin() + at, *snap);
                paint.activeMap = at;
            },
            [this, removeIndex] { paint.RemoveMap(removeIndex, vk); }, bytes);
    }

    ImGui::Spacing();
    static int newKind = 1;
    ImGui::SetNextItemWidth(-90.0f);
    const char* kindNames[(int)MapKind::Count];
    for (int i = 0; i < (int)MapKind::Count; ++i) kindNames[i] = MapKindLabel((MapKind)i);
    ImGui::Combo("##kind", &newKind, kindNames, (int)MapKind::Count);
    ImGui::SameLine();
    if (ImGui::Button("Add Map", ImVec2(-1, 0))) {
        paint.AddMap((MapKind)newKind);
        int added = paint.activeMap;
        undoStack.Push(std::string("Add ") + MapKindLabel((MapKind)newKind) + " map",
            [this, added] { if (added < (int)paint.maps.size()) paint.RemoveMap(added, vk); },
            [this, k = (MapKind)newKind] { paint.AddMap(k); });
        ShowStatus(std::string("Added ") + MapKindLabel((MapKind)newKind) + " map");
    }
    ImGui::Spacing();
    if (ImGui::Button("Apply Base Color to 3D Material", ImVec2(-1, 0))) ApplyMapsToMaterial();
    ImGui::End();
}


// Layer structure changes store a snapshot of the stack, the same approach Leorix used for its
// "layers-snapshot" undo entries: simple, and correct for reorder, delete and duplicate alike.
void App::PushLayerStructureUndo(const std::string& label, MapDoc& m, std::function<void()> apply) {
    int mapIndex = paint.activeMap;
    auto before = std::make_shared<std::vector<Layer>>(m.layers);
    int beforeActive = m.activeLayer;
    apply();
    auto after = std::make_shared<std::vector<Layer>>(m.layers);
    int afterActive = m.activeLayer;
    size_t bytes = 0;
    for (const auto& l : *before) bytes += l.pixels.size();
    for (const auto& l : *after) bytes += l.pixels.size();
    auto restore = [this, mapIndex](std::shared_ptr<std::vector<Layer>> layers, int active) {
        if (mapIndex < 0 || mapIndex >= (int)paint.maps.size()) return;
        MapDoc& md = paint.maps[mapIndex];
        md.layers = *layers;
        md.activeLayer = std::clamp(active, 0, (int)md.layers.size() - 1);
        md.dirty.Whole(md.width, md.height);
    };
    undoStack.Push(label, [restore, before, beforeActive] { restore(before, beforeActive); },
                          [restore, after, afterActive] { restore(after, afterActive); }, bytes);
}

void App::HistoryPanel() {
    if (!ImGui::Begin("History", &showHistory)) { ImGui::End(); return; }
    ImGui::TextColored(kBrass, "%d step%s  |  %.1f MB", undoStack.Count(), undoStack.Count() == 1 ? "" : "s",
                       undoStack.Bytes() / (1024.0 * 1024.0));
    ImGui::Separator();
    const auto& entries = undoStack.Entries();
    for (int i = 0; i < (int)entries.size(); ++i) {
        bool applied = i < undoStack.Position();
        ImGui::PushID(i);
        ImGui::TextColored(applied ? ImGui::GetStyleColorVec4(ImGuiCol_Text)
                                   : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),
                           "%s%s", applied ? "" : "(undone) ", entries[i].label.c_str());
        ImGui::PopID();
    }
    if (entries.empty()) ImGui::TextDisabled("Nothing yet.");
    ImGui::End();
}


// ---------------------------------------------------------------- UV editor
void App::RebuildUVLayout() {
    MeshObject* o = scene.Active();
    if (!o) { uvLayout.Clear(); uvSourceObject = -1; return; }
    o->mesh.EnsureUVs();
    uvLayout.Build(o->mesh);
    uvSourceObject = scene.activeIndex;
}

void App::UVPanel() {
    if (!ImGui::Begin("UV Editor", &showUV)) { ImGui::End(); return; }
    MeshObject* o = scene.Active();
    ImGui::TextColored(kBrass, "UV MAP");
    if (!o) {
        ImGui::TextDisabled("No model loaded.");
        ImGui::End();
        return;
    }
    // rebuild when the selection changes or the mesh was edited
    if (uvSourceObject != scene.activeIndex || !uvLayout.valid) RebuildUVLayout();

    ImGui::TextDisabled("%s", o->name.c_str());
    ImGui::TextDisabled("%zu UV edges  |  %zu seam edges  |  %zu island%s",
                        uvLayout.wire.size(), uvLayout.seams.size(), uvLayout.islands,
                        uvLayout.islands == 1 ? "" : "s");
    ImGui::Separator();

    ImGui::Checkbox("Show UV wireframe on canvas", &uvShowWire);
    ImGui::PushItemWidth(-90.0f);
    ImGui::SliderFloat("Opacity", &uvOpacity, 0.0f, 100.0f, "%.0f%%");
    ImGui::ColorEdit3("Color", uvWireColor, ImGuiColorEditFlags_NoInputs);
    ImGui::Separator();
    ImGui::Checkbox("Show UV seams & open borders", &uvShowSeams);
    ImGui::ColorEdit3("Seam color", uvSeamColor, ImGuiColorEditFlags_NoInputs);
    ImGui::SliderFloat("Seam width", &uvSeamWidth, 1.0f, 6.0f, "%.1f px");
    ImGui::PopItemWidth();

    ImGui::Spacing();
    ImGui::TextColored(kBrass, "SEAMS");
    bool can = EdgeOpsAvailable();
    ImGui::BeginDisabled(!can);
    if (ImGui::Button("Mark Seam")) MarkEdges(true, true);
    ImGui::SameLine();
    if (ImGui::Button("Clear Seam")) MarkEdges(true, false);
    if (ImGui::Button("Mark Sharp")) MarkEdges(false, true);
    ImGui::SameLine();
    if (ImGui::Button("Clear Sharp")) MarkEdges(false, false);
    ImGui::EndDisabled();
    ImGui::Checkbox("Live Unwrap", &liveUnwrap);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Unwrap again every time seams are marked or cleared");
    if (!can) ImGui::TextDisabled("Enter Edit Mode, switch to Edge select, then pick edges.");
    ImGui::TextDisabled("%zu seam edge%s, %zu sharp edge%s marked on the mesh",
                        o->mesh.seamEdges.size(), o->mesh.seamEdges.size() == 1 ? "" : "s",
                        o->mesh.sharpEdges.size(), o->mesh.sharpEdges.size() == 1 ? "" : "s");

    ImGui::Spacing();
    if (ImGui::Button("Unwrap  (U)", ImVec2(-1, 0))) UnwrapSelected();
    if (ImGui::Button("Smart UV Project", ImVec2(-1, 0))) UnwrapSelected(true);
    ImGui::SetNextItemWidth(-90.0f);
    ImGui::SliderFloat("Angle limit", &smartAngle, 1.0f, 89.0f, "%.0f deg");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Smart UV Project keeps neighbouring faces together while they bend less than this");

    ImGui::Spacing();
    ImGui::TextColored(kBrass, "EDIT UVS");
    ImGui::Checkbox("Edit UVs on the canvas", &uvEditMode);
    if (uvEditMode) {
        if (ImGui::RadioButton("Islands", uvSelectIslands)) uvSelectIslands = true;
        ImGui::SameLine();
        if (ImGui::RadioButton("Vertices", !uvSelectIslands)) uvSelectIslands = false;
        // the canvas may be hidden, so drop a selection left over from another object or older topology here too
        if (uvSelOwner != o || uvSel.size() != o->mesh.loopUVs.size()) uvSel.clear();
        Float2 c;
        const bool any = UVSelectionCenter(o->mesh, uvSel, c);
        ImGui::BeginDisabled(!any);
        ImGui::SetNextItemWidth(-90.0f);
        if (ImGui::DragFloat("Rotate", &uvRotateDeg, 0.5f, -360.0f, 360.0f, "%.1f deg")) {
            if (uvGestureBase.empty()) BeginUVGesture();
            Float2 pivot; UVSelectionCenter(o->mesh, uvSel, pivot);
            TransformUVSelection(o->mesh, uvGestureBase, uvSel, {0, 0}, uvRotateDeg * 3.14159265f / 180.0f, {1, 1}, pivot);
            uvLayout.valid = false;
        }
        if (ImGui::IsItemDeactivated()) { EndUVGesture("Rotate UVs"); uvRotateDeg = 0.0f; }
        ImGui::SetNextItemWidth(-90.0f);
        if (ImGui::DragFloat("Scale", &uvScale, 0.005f, 0.01f, 20.0f, "%.3f x")) {
            if (uvGestureBase.empty()) BeginUVGesture();
            Float2 pivot; UVSelectionCenter(o->mesh, uvSel, pivot);
            TransformUVSelection(o->mesh, uvGestureBase, uvSel, {0, 0}, 0.0f, {uvScale, uvScale}, pivot);
            uvLayout.valid = false;
        }
        if (ImGui::IsItemDeactivated()) { EndUVGesture("Scale UVs"); uvScale = 1.0f; }
        ImGui::EndDisabled();
        if (ImGui::Button("Select All")) { uvSel.assign(o->mesh.loopUVs.size(), 1); uvSelOwner = o; }
        ImGui::SameLine();
        if (ImGui::Button("Select None")) uvSel.clear();
        Hint("Click an island (or a vertex) on the canvas to select it, Shift+click to add, then drag "
             "it to move. Rotate and Scale turn around the selection's centre.");
    }
    if (ImGui::Button("Rebuild layout from mesh", ImVec2(-1, 0))) { RebuildUVLayout(); ShowStatus("UV layout rebuilt"); }

    // Bake, as the original did: draw the layout into a new layer of the active map.
    MapDoc* m = paint.Active();
    ImGui::BeginDisabled(!m || !uvLayout.valid);
    if (ImGui::Button("Bake UV Layout to New Layer", ImVec2(-1, 0))) { BakeUVToLayer(false); }
    if (ImGui::Button("Bake Seams to New Layer", ImVec2(-1, 0))) { BakeUVToLayer(true); }
    ImGui::EndDisabled();
    ImGui::Spacing();
    Hint("A seam is a 3D edge whose two sides land in different places in UV space, or an edge "
         "with only one adjoining face (an open border).");
    ImGui::End();
}

// Draws the layout over the canvas rect. UV (0,0) is bottom-left, so v is flipped for screen.
void App::DrawUVOverlay(ImVec2 a, ImVec2 b) {
    if (!uvLayout.valid || (!uvShowWire && !uvShowSeams)) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float w = b.x - a.x, h = b.y - a.y;
    auto toScreen = [&](const Float2& uv) {
        return ImVec2(a.x + uv.u * w, a.y + (1.0f - uv.v) * h);
    };
    if (uvShowWire) {
        ImU32 col = IM_COL32((int)(uvWireColor[0] * 255), (int)(uvWireColor[1] * 255),
                             (int)(uvWireColor[2] * 255), (int)(uvOpacity * 2.55f));
        for (const UVEdge& e : uvLayout.wire) dl->AddLine(toScreen(e.a), toScreen(e.b), col, 1.0f);
    }
    if (uvShowSeams) {
        ImU32 col = IM_COL32((int)(uvSeamColor[0] * 255), (int)(uvSeamColor[1] * 255),
                             (int)(uvSeamColor[2] * 255), 255);
        for (const UVEdge& e : uvLayout.seams) dl->AddLine(toScreen(e.a), toScreen(e.b), col, uvSeamWidth);
    }
    const MeshObject* o = scene.Active();
    if (uvEditMode && o && o->mesh.HasLoopUVs() && uvSel.size() == o->mesh.loopUVs.size()) {
        const auto& uv = o->mesh.loopUVs;
        for (size_t ti = 0; ti * 3 + 2 < uv.size(); ++ti)
            for (int e = 0; e < 3; ++e) {
                size_t c0 = ti * 3 + e, c1 = ti * 3 + (e + 1) % 3;
                if (uvSel[c0] && uvSel[c1]) dl->AddLine(toScreen(uv[c0]), toScreen(uv[c1]), IM_COL32(240, 150, 40, 255), 1.5f);
            }
        for (size_t c = 0; c < uv.size(); ++c)
            if (uvSel[c]) dl->AddCircleFilled(toScreen(uv[c]), 2.5f, IM_COL32(240, 150, 40, 255));
    }
}

// Rasterises the UV lines into a fresh layer so they can be painted over or exported.
void App::BakeUVToLayer(bool seamsOnly) {
    MapDoc* m = paint.Active();
    if (!m || !uvLayout.valid) return;
    PushLayerStructureUndo(seamsOnly ? "Bake Seams" : "Bake UV Layout", *m, [&] {
        m->AddLayer(seamsOnly ? "UV Seams" : "UV Layout");
    });
    Layer* l = m->Active();
    if (!l) return;

    const float* c = seamsOnly ? uvSeamColor : uvWireColor;
    uint32_t rgba = IM_COL32((int)(c[0] * 255), (int)(c[1] * 255), (int)(c[2] * 255), 255);
    const auto& list = seamsOnly ? uvLayout.seams : uvLayout.wire;

    // Bresenham-ish line walk in map pixels.
    auto plot = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= m->width || y >= m->height) return;
        size_t i = ((size_t)y * m->width + x) * 4;
        l->pixels[i] = rgba & 0xFF; l->pixels[i + 1] = (rgba >> 8) & 0xFF;
        l->pixels[i + 2] = (rgba >> 16) & 0xFF; l->pixels[i + 3] = 255;
    };
    for (const UVEdge& e : list) {
        float x0 = e.a.u * m->width, y0 = (1.0f - e.a.v) * m->height;
        float x1 = e.b.u * m->width, y1 = (1.0f - e.b.v) * m->height;
        int steps = (int)std::max(std::fabs(x1 - x0), std::fabs(y1 - y0)) + 1;
        for (int s = 0; s <= steps; ++s) {
            float t = steps ? s / (float)steps : 0.0f;
            int px = (int)std::lround(x0 + (x1 - x0) * t), py = (int)std::lround(y0 + (y1 - y0) * t);
            plot(px, py);
            if (seamsOnly) { plot(px + 1, py); plot(px, py + 1); }   // seams drawn thicker
        }
    }
    m->dirty.Whole(m->width, m->height);
    ShowStatus(seamsOnly ? "Baked seams to a new layer" : "Baked UV layout to a new layer");
}

// ---------------------------------------------------------------- seam / sharp edge marking
bool App::EdgeOpsAvailable() const {
    const MeshObject* o = scene.Active();
    return o && mode3D == Mode3D::Edit && (edit.selectMode & (int)SelectMode::Edge) && !o->selEdges.empty();
}

void App::MarkEdges(bool seam, bool set) {
    MeshObject* o = scene.Active();
    if (!o) return;
    if (mode3D != Mode3D::Edit || !(edit.selectMode & (int)SelectMode::Edge)) {
        ShowStatus("Switch to Edit Mode and Edge select first");
        return;
    }
    if (o->selEdges.empty()) { ShowStatus("No edges selected"); return; }

    auto& target = seam ? o->mesh.seamEdges : o->mesh.sharpEdges;
    auto before = std::make_shared<std::unordered_set<uint64_t>>(target);
    auto beforeUV = std::make_shared<std::vector<Float2>>(o->mesh.loopUVs);
    bool unwrapped = false;
    if (seam) unwrapped = MarkSeams(o->mesh, o->selEdges, set, liveUnwrap).ok;
    else for (uint64_t e : o->selEdges) { if (set) target.insert(e); else target.erase(e); }
    auto after = std::make_shared<std::unordered_set<uint64_t>>(target);
    auto afterUV = std::make_shared<std::vector<Float2>>(o->mesh.loopUVs);

    int objIndex = scene.activeIndex;
    bool isSeam = seam;
    auto apply = [this, objIndex, isSeam, unwrapped](std::shared_ptr<std::unordered_set<uint64_t>> v, std::shared_ptr<std::vector<Float2>> uv) {
        if (objIndex < 0 || objIndex >= (int)scene.objects.size()) return;
        MeshObject& obj = *scene.objects[objIndex];
        (isSeam ? obj.mesh.seamEdges : obj.mesh.sharpEdges) = *v;
        if (unwrapped) { obj.mesh.loopUVs = *uv; obj.stackDirty = true; }
        if (isSeam) uvLayout.valid = false;
    };
    const char* label = seam ? (set ? "Mark Seam" : "Clear Seam") : (set ? "Mark Sharp" : "Clear Sharp");
    undoStack.Push(label, [apply, before, beforeUV] { apply(before, beforeUV); }, [apply, after, afterUV] { apply(after, afterUV); },
                   (before->size() + after->size()) * sizeof(uint64_t) +
                   (unwrapped ? (beforeUV->size() + afterUV->size()) * sizeof(Float2) : 0));
    if (unwrapped) o->stackDirty = true;

    if (seam) uvLayout.valid = false;   // the layout changes with the cuts
    char msg[96];
    std::snprintf(msg, sizeof(msg), "%s on %zu edge%s", label, o->selEdges.size(), o->selEdges.size() == 1 ? "" : "s");
    ShowStatus(msg);
}

void App::UnwrapSelected(bool smart) {
    MeshObject* o = scene.Active();
    if (!o) { ShowStatus("No object selected"); return; }
    auto beforeUV = std::make_shared<std::vector<Float2>>(o->mesh.loopUVs);

    UnwrapOptions opts;
    UnwrapResult r = smart ? SmartUVProject(o->mesh, smartAngle, opts) : UnwrapMesh(o->mesh, opts);
    if (!r.ok) { ShowStatus(r.message); return; }
    auto afterUV = std::make_shared<std::vector<Float2>>(o->mesh.loopUVs);

    int objIndex = scene.activeIndex;
    auto apply = [this, objIndex](std::shared_ptr<std::vector<Float2>> uv) {
        if (objIndex < 0 || objIndex >= (int)scene.objects.size()) return;
        scene.objects[objIndex]->mesh.loopUVs = *uv;
        uvLayout.valid = false;
    };
    undoStack.Push(smart ? "Smart UV Project" : "Unwrap", [apply, beforeUV] { apply(beforeUV); }, [apply, afterUV] { apply(afterUV); },
                   (beforeUV->size() + afterUV->size()) * sizeof(Float2));

    uvLayout.valid = false;
    RebuildUVLayout();
    showUV = true;
    if (o->mesh.seamEdges.empty() && !smart)
        ShowStatus(r.message + " - no seams marked, so the whole mesh was flattened in one piece");
    else
        ShowStatus(r.message);
}

// Topology-changing Edit Mode operators. The whole mesh is snapshotted for undo, since vertex and
// triangle counts change.
void App::RunEditOp(char op) {
    MeshObject* o = scene.Active();
    if (!o || o->isCurve) { ShowStatus("No mesh selected"); return; }
    // weights and multires detail are indexed by vertex, so a new vertex count would break them
    if (o->armature.Bound() || o->multires.valid) { ShowStatus("Apply or clear the armature/multires first"); return; }
    EditSelection sel{o->selVerts, o->selEdges, o->selFaces};
    auto before = std::make_shared<Mesh>(o->mesh);
    EditOpResult r = op == 'E' ? ExtrudeFaces(o->mesh, sel) : op == 'X' ? DeleteSelected(o->mesh, sel)
                   : op == 'D' ? DuplicateFaces(o->mesh, sel) : MergeAtCenter(o->mesh, sel);
    ShowStatus(r.message);
    if (!r.ok) return;
    o->selVerts = r.sel.verts; o->selEdges = r.sel.edges; o->selFaces = r.sel.faces;
    o->MarkTopoDirty();
    o->stackDirty = true;
    uvLayout.valid = false;
    auto after = std::make_shared<Mesh>(o->mesh);
    int idx = scene.activeIndex;
    auto apply = [this, idx](std::shared_ptr<Mesh> m) {
        if (idx < 0 || idx >= (int)scene.objects.size()) return;
        MeshObject& ob = *scene.objects[idx];
        ob.mesh = *m;
        ob.selVerts.clear(); ob.selEdges.clear(); ob.selFaces.clear();   // old indices may not exist
        ob.MarkTopoDirty();
        ob.stackDirty = true;
        uvLayout.valid = false;
    };
    undoStack.Push(op == 'E' ? "Extrude" : op == 'X' ? "Delete" : op == 'D' ? "Duplicate Faces" : "Merge at Center",
                   [apply, before] { apply(before); }, [apply, after] { apply(after); },
                   (before->positions.size() + after->positions.size()) * sizeof(Float3) +
                   (before->tris.size() + after->tris.size()) * sizeof(Tri));
    if (op == 'E' || op == 'D') BeginTransform(0);   // as in Blender, the new geometry follows the mouse
}

// P: the selected faces become a new object with the same transform and material.
void App::SeparateSelected() {
    MeshObject* o = scene.Active();
    if (!o || o->isCurve) { ShowStatus("No mesh selected"); return; }
    if (o->armature.Bound() || o->multires.valid) { ShowStatus("Apply or clear the armature/multires first"); return; }
    EditSelection sel{o->selVerts, o->selEdges, o->selFaces};
    auto before = std::make_shared<Mesh>(o->mesh);
    auto part = std::make_shared<Mesh>();
    EditOpResult r = SeparateFaces(o->mesh, sel, *part);
    ShowStatus(r.message);
    if (!r.ok) return;
    auto after = std::make_shared<Mesh>(o->mesh);
    const int idx = scene.activeIndex;
    const std::string name = o->name;
    auto setMesh = [this, idx](const Mesh& m) {
        if (idx < 0 || idx >= (int)scene.objects.size()) return;
        MeshObject& ob = *scene.objects[idx];
        ob.mesh = m;
        ob.selVerts.clear(); ob.selEdges.clear(); ob.selFaces.clear();
        ob.MarkTopoDirty();
        ob.stackDirty = true;
        uvLayout.valid = false;
    };
    auto partObj = std::make_shared<MeshObject*>(nullptr);   // the exact object undo must remove
    auto addPart = [this, idx, name, part, partObj] {
        if (idx < 0 || idx >= (int)scene.objects.size()) return;
        Transform xf = scene.objects[idx]->transform;
        Material mat = scene.objects[idx]->material;
        bool smooth = scene.objects[idx]->shadeSmooth;
        MeshObject* n = scene.AddMesh(*part, name);
        n->transform = xf; n->material = mat; n->shadeSmooth = smooth;
        *partObj = n;
        scene.SelectOnly(idx);   // stay editing the original, as in Blender
    };
    setMesh(*after);
    addPart();
    undoStack.Push("Separate",
        [this, setMesh, before, partObj] { int i = scene.IndexOf(*partObj); if (i >= 0) scene.Remove(i); setMesh(*before); },
        [setMesh, addPart, after] { setMesh(*after); addPart(); },
        (before->positions.size() * 2 + part->positions.size()) * sizeof(Float3) +
        (before->tris.size() * 2 + part->tris.size()) * sizeof(Tri));
}

// ---------------------------------------------------------------- .leorix projects
static void SDLCALL OnProjectSaveChosen(void* userdata, const char* const* filelist, int) {
    if (!filelist || !filelist[0]) return;
    static_cast<App*>(userdata)->QueueProjectPath(filelist[0], true);
}
static void SDLCALL OnProjectOpenChosen(void* userdata, const char* const* filelist, int) {
    if (!filelist || !filelist[0]) return;
    static_cast<App*>(userdata)->QueueProjectPath(filelist[0], false);
}

void App::RequestProjectSave() {
    static const SDL_DialogFileFilter filters[] = {{"Leorix Project", "leorix"}};
    SDL_ShowSaveFileDialog(OnProjectSaveChosen, this, window, filters, 1,
                           projectPath.empty() ? nullptr : projectPath.c_str());
}
void App::RequestProjectOpen() {
    static const SDL_DialogFileFilter filters[] = {{"Leorix Project", "leorix"}};
    SDL_ShowOpenFileDialog(OnProjectOpenChosen, this, window, filters, 1, nullptr, false);
}
void App::QueueProjectPath(const std::string& path, bool saving) {
    std::lock_guard<std::mutex> lock(importMutex);
    if (saving) pendingProjectSave = path; else pendingProjectOpen = path;
}

// ---------------------------------------------------------------- Join (Ctrl+J)
void App::JoinMeshes() {
    MeshObject* target = scene.Active();
    if (!target) { ShowStatus("Nothing selected"); return; }
    // joined geometry would be wiped the next time the curve rebuilds its tube
    if (target->isCurve) { ShowStatus("Convert the curve to a mesh before joining into it"); return; }
    if (target->isControl) { ShowStatus("Rig controls can't be joined; make a mesh the active object"); return; }

    // everything selected except the active object, which everything merges into
    std::vector<int> sources;
    for (int i = 0; i < (int)scene.objects.size(); ++i)
        if (i != scene.activeIndex && scene.objects[i]->selected && !scene.objects[i]->mesh.positions.empty() &&
            !scene.objects[i]->isControl)   // a control is a wire with no faces, and its IK would lose its target
            sources.push_back(i);
    if (sources.empty()) { ShowStatus("Select at least two objects to join (Shift+click in the hierarchy)"); return; }

    // snapshot for undo: the target's mesh plus every source object, whole
    auto beforeMesh = std::make_shared<Mesh>(target->mesh);
    auto removed = std::make_shared<std::vector<std::pair<int, std::shared_ptr<MeshObject>>>>();
    size_t bytes = target->mesh.positions.size() * sizeof(Float3);

    Mesh& dst = target->mesh;
    bool dstHasUV = dst.HasLoopUVs();
    size_t joinedVerts = 0;

    for (int si : sources) {
        MeshObject& src = *scene.objects[si];
        const uint32_t vertexOffset = (uint32_t)dst.positions.size();
        const uint32_t polyOffset = dst.polyCount;

        // Source vertices are in the source's local space: push them to world, then into the
        // target's local space, so the joined mesh keeps its shape wherever either object sits.
        for (const Float3& p : src.mesh.positions)
            dst.positions.push_back(target->transform.InverseApply(src.transform.Apply(p)));
        joinedVerts += src.mesh.positions.size();

        for (const Tri& t : src.mesh.tris)
            dst.tris.push_back({t.a + vertexOffset, t.b + vertexOffset, t.c + vertexOffset, t.poly + polyOffset});
        dst.polyCount += src.mesh.polyCount;

        // UVs stay per corner. If either side lacks them, the joined mesh gets a full set so the
        // arrays never fall out of step with the triangles.
        if (dstHasUV || src.mesh.HasLoopUVs()) {
            if (!dstHasUV) { dst.loopUVs.assign((dst.tris.size() - src.mesh.tris.size()) * 3, Float2{}); dstHasUV = true; }
            for (size_t i = 0; i < src.mesh.tris.size(); ++i)
                for (int j = 0; j < 3; ++j)
                    dst.loopUVs.push_back(src.mesh.HasLoopUVs() ? src.mesh.loopUVs[i * 3 + j] : Float2{});
        }

        // edge flags carry over with their vertices renumbered
        for (uint64_t key : src.mesh.seamEdges)
            dst.seamEdges.insert(Mesh::EdgeKey(uint32_t(key >> 32) + vertexOffset, uint32_t(key & 0xFFFFFFFF) + vertexOffset));
        for (uint64_t key : src.mesh.sharpEdges)
            dst.sharpEdges.insert(Mesh::EdgeKey(uint32_t(key >> 32) + vertexOffset, uint32_t(key & 0xFFFFFFFF) + vertexOffset));

        bytes += src.mesh.positions.size() * sizeof(Float3);
    }

    // remove the sources, highest index first so the earlier indices stay valid
    std::sort(sources.begin(), sources.end(), std::greater<int>());
    for (int si : sources) {
        removed->push_back({si, std::shared_ptr<MeshObject>(scene.objects[si].release())});
        scene.objects.erase(scene.objects.begin() + si);
        if (si < scene.activeIndex) --scene.activeIndex;
    }

    dst.ComputeNormals();
    dst.ComputeBounds();
    target->MarkTopoDirty();
    target->armature.weights.clear();      // the old binding no longer matches the vertex list
    target->skinned.clear();
    uvLayout.valid = false;

    auto afterMesh = std::make_shared<Mesh>(dst);
    int targetIndex = scene.activeIndex;
    undoStack.Push("Join Meshes",
        [this, targetIndex, beforeMesh, removed]() {
            if (targetIndex >= 0 && targetIndex < (int)scene.objects.size()) {
                scene.objects[targetIndex]->mesh = *beforeMesh;
                scene.objects[targetIndex]->MarkTopoDirty();
            }
            // put the sources back at their original indices, lowest first
            for (auto it = removed->rbegin(); it != removed->rend(); ++it) {
                int at = std::clamp(it->first, 0, (int)scene.objects.size());
                scene.objects.insert(scene.objects.begin() + at, std::make_unique<MeshObject>(*it->second));
            }
            uvLayout.valid = false;
        },
        [this, targetIndex, afterMesh, removed]() {
            // remove them again, highest index first
            std::vector<int> idx;
            for (auto& pr : *removed) idx.push_back(pr.first);
            std::sort(idx.begin(), idx.end(), std::greater<int>());
            for (int i : idx) if (i >= 0 && i < (int)scene.objects.size()) scene.objects.erase(scene.objects.begin() + i);
            if (targetIndex >= 0 && targetIndex < (int)scene.objects.size()) {
                scene.objects[targetIndex]->mesh = *afterMesh;
                scene.objects[targetIndex]->MarkTopoDirty();
            }
            uvLayout.valid = false;
        }, bytes);

    if (scene.activeIndex >= 0) scene.SelectOnly(scene.activeIndex);
    char msg[128];
    std::snprintf(msg, sizeof(msg), "Joined %zu object%s into %s (+%zu verts)",
                  sources.size(), sources.size() == 1 ? "" : "s", target->name.c_str(), joinedVerts);
    ShowStatus(msg);
}

// ---------------------------------------------------------------- Add > Armature
void App::AddArmatureObject() {
    MeshObject* active = scene.Active();
    // With a mesh selected, the bone is added to it (so it can be weighted straight away);
    // otherwise a standalone armature object is created, like Blender's Add > Armature.
    if (active && !active->mesh.positions.empty()) {
        active->mesh.ComputeBounds();
        Float3 mn = active->mesh.bmin, mx = active->mesh.bmax;
        Float3 head{(mn.x + mx.x) * 0.5f, mn.y, (mn.z + mx.z) * 0.5f};
        Float3 tail{head.x, mn.y + (mx.y - mn.y) * 0.5f, head.z};
        int added = active->armature.AddBone("Bone." + std::to_string(active->armature.bones.size() + 1), head, tail,
                                             activeBone >= 0 ? activeBone : -1);
        activeBone = added;
        int objIndex = scene.activeIndex;
        undoStack.Push("Add Bone",
            [this, objIndex]() {
                if (objIndex < 0 || objIndex >= (int)scene.objects.size()) return;
                Armature& a = scene.objects[objIndex]->armature;
                if (!a.bones.empty()) a.Remove((int)a.bones.size() - 1);
                UpdateSkinning(*scene.objects[objIndex]);
            },
            [this, objIndex, head, tail]() {
                if (objIndex < 0 || objIndex >= (int)scene.objects.size()) return;
                Armature& a = scene.objects[objIndex]->armature;
                a.AddBone("Bone." + std::to_string(a.bones.size() + 1), head, tail, -1);
            });
        showArmature = true;
        SetMode3D(Mode3D::Pose);
        ShowStatus("Added a bone to " + active->name + " - use Parent with Automatic Weights to bind it");
        return;
    }

    MeshObject* obj = scene.AddMesh(Mesh{}, "Armature");
    obj->armature.AddBone("Bone.1", {0, 0, 0}, {0, 1, 0}, -1);
    activeBone = 0;
    showArmature = true;
    showViewport3D = true;
    SetMode3D(Mode3D::Pose);
    PushObjectListUndo("Add Armature", obj, scene.activeIndex);
    ShowStatus("Added an armature");
}

// ---------------------------------------------------------------- modifier stack
void App::RefreshModifiers(MeshObject& o) {
    bool any = false;
    for (const Modifier& m : o.modifiers) if (m.enabled) { any = true; break; }
    if (!any) { o.hasDisplay = false; o.displayMesh.Clear(); o.stackDirty = false; return; }

    std::vector<const Mesh*> targets;
    std::vector<const CurvePath*> curves;
    for (auto& other : scene.objects) {
        targets.push_back(&other->mesh);
        curves.push_back(other->isCurve ? &other->curve : nullptr);
    }
    o.displayMesh = EvaluateModifiers(o.mesh, scene.StackWithIndices(o.modifiers), targets, curves);
    o.hasDisplay = true;
    o.stackDirty = false;
}

void App::ModifierPanel() {
    if (!ImGui::Begin("Modifiers", &showModifiers)) { ImGui::End(); return; }
    MeshObject* o = scene.Active();
    if (!o) { ImGui::TextDisabled("No object selected."); ImGui::End(); return; }

    ImGui::TextColored(kBrass, "%s", o->name.c_str());
    if (o->hasDisplay)
        ImGui::TextDisabled("base %zu verts  ->  %zu verts after the stack",
                            o->mesh.VertexCount(), o->displayMesh.VertexCount());
    else
        ImGui::TextDisabled("base %zu verts, no active modifiers", o->mesh.VertexCount());
    ImGui::Separator();

    static int addType = 0;
    const char* names[(int)ModifierType::Count];
    for (int i = 0; i < (int)ModifierType::Count; ++i) names[i] = ModifierName((ModifierType)i);
    ImGui::SetNextItemWidth(-90.0f);
    ImGui::Combo("##addmod", &addType, names, (int)ModifierType::Count);
    ImGui::SameLine();
    if (ImGui::Button("Add", ImVec2(-1, 0))) {
        Modifier m;
        m.type = (ModifierType)addType;
        o->modifiers.push_back(m);
        o->stackDirty = true;
        ShowStatus(std::string("Added ") + ModifierName(m.type));
    }
    ImGui::TextDisabled("%s", ModifierHint((ModifierType)addType));
    ImGui::Separator();

    int remove = -1, moveUp = -1, moveDown = -1, apply = -1;
    for (int i = 0; i < (int)o->modifiers.size(); ++i) {
        Modifier& m = o->modifiers[i];
        ImGui::PushID(i);
        bool open = ImGui::CollapsingHeader(ModifierName(m.type), ImGuiTreeNodeFlags_DefaultOpen);
        ImGui::SameLine(ImGui::GetWindowWidth() - 110);
        if (ImGui::Checkbox("##on", &m.enabled)) o->stackDirty = true;
        ImGui::SameLine();
        if (ImGui::SmallButton("^")) moveUp = i;
        ImGui::SameLine();
        if (ImGui::SmallButton("v")) moveDown = i;
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) remove = i;

        if (open) {
            ImGui::PushItemWidth(-110.0f);
            bool ch = false;
            switch (m.type) {
                case ModifierType::Array:
                    ch |= ImGui::SliderInt("Count", &m.arrayCount, 1, 64);
                    {
                        Float3 uo = ToUser({m.arrayOffset[0], m.arrayOffset[1], m.arrayOffset[2]});
                        if (ImGui::SliderFloat3("Relative Offset", &uo.x, -3.0f, 3.0f, "%.2f")) {
                            Float3 io = FromUser(uo);
                            m.arrayOffset[0] = io.x; m.arrayOffset[1] = io.y; m.arrayOffset[2] = io.z;
                            ch = true;
                        }
                    }
                    break;
                case ModifierType::Mirror:
                    // user Y mirrors front/back (internal Z), user Z mirrors up/down (internal Y)
                    ch |= ImGui::Checkbox("X", &m.mirrorX); ImGui::SameLine();
                    ch |= ImGui::Checkbox("Y", &m.mirrorZ); ImGui::SameLine();
                    ch |= ImGui::Checkbox("Z", &m.mirrorY);
                    ch |= ImGui::Checkbox("Merge at centre", &m.mirrorMerge);
                    ch |= ImGui::SliderFloat("Threshold", &m.mirrorThreshold, 0.0f, 0.1f, "%.4f");
                    break;
                case ModifierType::Solidify:
                    ch |= ImGui::SliderFloat("Thickness", &m.solidifyThickness, 0.001f, 1.0f, "%.3f m", ImGuiSliderFlags_Logarithmic);
                    ch |= ImGui::Checkbox("Rim", &m.solidifyRim);
                    break;
                case ModifierType::Subdivide:
                    ch |= ImGui::SliderInt("Levels", &m.subdivLevels, 1, 4);
                    ch |= ImGui::Checkbox("Smooth (Catmull-Clark)", &m.subdivSmooth);
                    break;
                case ModifierType::Smooth:
                    ch |= ImGui::SliderFloat("Factor", &m.smoothFactor, 0.0f, 1.0f, "%.2f");
                    ch |= ImGui::SliderInt("Iterations", &m.smoothIterations, 1, 30);
                    break;
                case ModifierType::Weld:
                    ch |= ImGui::SliderFloat("Distance", &m.weldDistance, 0.0001f, 1.0f, "%.4f m", ImGuiSliderFlags_Logarithmic);
                    break;
                case ModifierType::Shrinkwrap: {
                    std::string preview = "(none)";
                    if (scene.IndexOfId(m.shrinkTarget) >= 0)
                        preview = scene.objects[scene.IndexOfId(m.shrinkTarget)]->name;
                    if (ImGui::BeginCombo("Target", preview.c_str())) {
                        for (int k = 0; k < (int)scene.objects.size(); ++k) {
                            if (scene.objects[k].get() == o) continue;       // never itself
                            if (ImGui::Selectable(scene.objects[k]->name.c_str(), m.shrinkTarget == (int)scene.objects[k]->id)) {
                                m.shrinkTarget = (int)scene.objects[k]->id; ch = true;
                            }
                        }
                        ImGui::EndCombo();
                    }
                    ch |= ImGui::SliderFloat("Offset", &m.shrinkOffset, -0.5f, 0.5f, "%.3f m");
                    break;
                }
                case ModifierType::Screw:
                    ch |= ImGui::SliderInt("Steps", &m.screwSteps, 2, 256);
                    ch |= ImGui::SliderFloat("Angle", &m.screwAngle, -720.0f, 720.0f, "%.0f deg");
                    ch |= ImGui::SliderFloat("Screw Offset", &m.screwOffset, -5.0f, 5.0f, "%.2f m");
                    {
                        static const char* ax[] = {"X", "Y", "Z"};
                        int ua = UserAxis(m.screwAxis);
                        if (ImGui::Combo("Axis", &ua, ax, 3)) { m.screwAxis = InternalAxis(ua); ch = true; }
                    }
                    break;
                case ModifierType::Remesh:
                    ch |= ImGui::SliderFloat("Voxel Size", &m.remeshVoxel, 0.01f, 1.0f, "%.3f m", ImGuiSliderFlags_Logarithmic);
                    ch |= ImGui::SliderInt("Relax", &m.remeshSmooth, 0, 8);
                    ImGui::TextDisabled("Rebuilds topology from scratch - UVs do not survive.");
                    break;
                case ModifierType::Displace:
                    ch |= ImGui::SliderFloat("Strength", &m.displaceStrength, -1.0f, 1.0f, "%.3f");
                    ch |= ImGui::SliderFloat("Mid Level", &m.displaceMidLevel, 0.0f, 1.0f, "%.2f");
                    ch |= ImGui::Checkbox("Procedural noise", &m.displaceUseNoise);
                    ch |= ImGui::SliderFloat("Noise Scale", &m.displaceNoiseScale, 0.1f, 20.0f, "%.2f");
                    break;
                case ModifierType::Decimate:
                    ch |= ImGui::SliderFloat("Ratio", &m.decimateRatio, 0.02f, 1.0f, "%.2f");
                    break;
                case ModifierType::CurveDeform: {
                    std::string preview = "(none)";
                    if (scene.IndexOfId(m.curveTarget) >= 0)
                        preview = scene.objects[scene.IndexOfId(m.curveTarget)]->name;
                    if (ImGui::BeginCombo("Curve", preview.c_str())) {
                        bool any = false;
                        for (int k = 0; k < (int)scene.objects.size(); ++k) {
                            if (!scene.objects[k]->isCurve) continue;
                            any = true;
                            if (ImGui::Selectable(scene.objects[k]->name.c_str(), m.curveTarget == (int)scene.objects[k]->id)) { m.curveTarget = (int)scene.objects[k]->id; ch = true; }
                        }
                        if (!any) ImGui::TextDisabled("No curves in the scene");
                        ImGui::EndCombo();
                    }
                    static const char* ax[] = {"X", "Y", "Z"};
                    int ua = UserAxis(m.curveAxis);
                    if (ImGui::Combo("Deform Axis", &ua, ax, 3)) { m.curveAxis = InternalAxis(ua); ch = true; }
                    ch |= ImGui::SliderFloat("Stretch", &m.curveStretch, 0.1f, 3.0f, "%.2f");
                    break;
                }
                default: break;
            }
            ImGui::PopItemWidth();
            if (ImGui::Button("Apply")) apply = i;
            ImGui::SameLine();
            ImGui::TextDisabled("%s", ModifierHint(m.type));
            if (ch) o->stackDirty = true;
        }
        ImGui::PopID();
    }
    if (o->modifiers.empty()) ImGui::TextDisabled("Stack is empty. Modifiers preview live and never change the base mesh until Apply.");

    if (moveUp > 0) { std::swap(o->modifiers[moveUp], o->modifiers[moveUp - 1]); o->stackDirty = true; }
    if (moveDown >= 0 && moveDown + 1 < (int)o->modifiers.size()) { std::swap(o->modifiers[moveDown], o->modifiers[moveDown + 1]); o->stackDirty = true; }
    if (remove >= 0) { o->modifiers.erase(o->modifiers.begin() + remove); o->stackDirty = true; }
    if (apply >= 0 && apply < (int)o->modifiers.size()) {
        auto before = std::make_shared<Mesh>(o->mesh);
        auto beforeStack = std::make_shared<std::vector<Modifier>>(o->modifiers);
        std::vector<const Mesh*> targets;
        std::vector<const CurvePath*> curves;
        for (auto& other : scene.objects) {
            targets.push_back(&other->mesh);
            curves.push_back(other->isCurve ? &other->curve : nullptr);
        }
        o->mesh = ApplyModifier(o->mesh, scene.StackWithIndices({o->modifiers[apply]})[0], targets, curves);
        o->modifiers.erase(o->modifiers.begin() + apply);
        o->MarkTopoDirty();
        o->selVerts.clear(); o->selEdges.clear(); o->selFaces.clear();   // old indices mean nothing in the new mesh
        o->armature.weights.clear();
        o->skinned.clear();
        o->stackDirty = true;
        uvLayout.valid = false;
        auto after = std::make_shared<Mesh>(o->mesh);
        auto afterStack = std::make_shared<std::vector<Modifier>>(o->modifiers);
        int idx = scene.activeIndex;
        undoStack.Push("Apply Modifier",
            [this, idx, before, beforeStack] {
                if (idx < 0 || idx >= (int)scene.objects.size()) return;
                scene.objects[idx]->mesh = *before;
                scene.objects[idx]->modifiers = *beforeStack;
                scene.objects[idx]->MarkTopoDirty();
                scene.objects[idx]->stackDirty = true;
            },
            [this, idx, after, afterStack] {
                if (idx < 0 || idx >= (int)scene.objects.size()) return;
                scene.objects[idx]->mesh = *after;
                scene.objects[idx]->modifiers = *afterStack;
                scene.objects[idx]->MarkTopoDirty();
                scene.objects[idx]->stackDirty = true;
            },
            before->positions.size() * sizeof(Float3) * 2);
        ShowStatus("Applied modifier");
    }
    ImGui::End();
}

// ---------------------------------------------------------------- curves
void App::AddCurve(int kind) {
    static const char* names[] = {"Bezier Curve", "Circle Curve", "Spiral", "Path", "NURBS Curve", "NURBS Circle"};
    MeshObject* obj = scene.AddMesh(Mesh{}, names[std::clamp(kind, 0, 5)]);
    obj->isCurve = true;
    obj->curve.points.clear();
    obj->curve.resolution = 12;

    const float kPi = 3.14159265f;
    if (kind == 1) {                       // circle
        obj->curve.cyclic = true;
        for (int i = 0; i < 8; ++i) {
            float a = 2 * kPi * i / 8;
            obj->curve.points.push_back({std::cos(a), 0.0f, std::sin(a)});
        }
    } else if (kind == 2) {                // spiral
        obj->curve.cyclic = false;
        for (int i = 0; i < 24; ++i) {
            float t = i / 23.0f, a = t * 4 * kPi;
            obj->curve.points.push_back({std::cos(a) * (0.4f + t), t * 3.0f, std::sin(a) * (0.4f + t)});
        }
    } else if (kind == 3) {                // path: a straight line of points, for Curve Deform
        obj->curve.cyclic = false;
        for (int i = 0; i < 5; ++i) obj->curve.points.push_back({-2.0f + i, 0.0f, 0.0f});
    } else if (kind == 4) {                // NURBS curve: order 4, the curve stays inside its points
        obj->curve.nurbs = true;
        obj->curve.points = {{-2, 0, 0}, {-1, 0, 1}, {1, 0, -1}, {2, 0, 0}};
    } else if (kind == 5) {                // NURBS circle: exact, from rational weights
        SetNurbsCircle(obj->curve, 1.0f);
    } else {                               // gentle S, the default Bezier line
        obj->curve.cyclic = false;
        obj->curve.points = {{-2, 0, 0}, {-0.7f, 0, 1.2f}, {0.7f, 0, -1.2f}, {2, 0, 0}};
    }
    RebuildCurveMesh(*obj);
    PushObjectListUndo("Add " + obj->name, obj, scene.activeIndex);
    showModifiers = true;
    ShowStatus("Added " + obj->name + " - add a Curve Deform modifier to a mesh to bend it along this");
}

// ---------------------------------------------------------------- ray tracing
void App::StartRender() {
    if (!scene.HasMesh()) { ShowStatus("Nothing to render"); return; }
    RayTraceWorld w;
    ViewLighting lt = BuildLighting();
    w.sunDir = lt.sunDir;
    for (int i = 0; i < 3; ++i) {
        w.sunColor[i] = world.sunColor[i];
        w.skyColor[i] = world.color[i];
        w.horizonColor[i] = world.horizon[i];
    }
    w.sunStrength = world.sunStrength;
    w.skyStrength = world.strength;
    w.background = world.background;
    if (hdri.Loaded()) { w.hdri = &hdri; w.hdriRotation = hdriRotation; w.hdriStrength = hdriStrength; }
    rt.exposure = world.exposure;
    if (std::getenv("LEORIX_NO_SHADOW")) rt.shadows = false;

    Vec3 t = viewport.Target();
    if (tracer.Start(scene, scene.Camera(), viewport.Yaw(), viewport.Pitch(), viewport.Distance(),
                     Float3{t.x, t.y, t.z}, cameraView, rt, w)) {
        renderResultDirty = true;
        showRayTracing = true;
        ShowStatus("Rendering...");
    } else {
        ShowStatus("A render is already running");
    }
}

void App::RayTracingPanel() {
    if (!ImGui::Begin("Ray Tracing", &showRayTracing)) { ImGui::End(); return; }

    const ImVec4 ok(0.44f, 0.74f, 0.52f, 1);
    ImGui::TextColored(kBrass, "DEVICE");
    ImGui::TextWrapped("%s", vk.gpu.name.c_str());
    ImGui::Text("Hardware ray tracing:"); ImGui::SameLine();
    ImGui::TextColored(vk.gpu.HardwareRayTracing() ? ok : ImVec4(0.55f, 0.57f, 0.64f, 1),
                       vk.gpu.HardwareRayTracing() ? "available" : "not available");
    ImGui::TextDisabled("This render runs on the CPU. The Vulkan RT backend will use the GPU.");

    ImGui::Spacing(); ImGui::TextColored(kBrass, "OUTPUT"); ImGui::Separator();
    ImGui::PushItemWidth(-130.0f);
    static int resPreset = 1;
    static const char* presets[] = {"640 x 360", "960 x 540", "1280 x 720", "1920 x 1080", "Custom"};
    static const int presetSizes[][2] = {{640, 360}, {960, 540}, {1280, 720}, {1920, 1080}};
    if (ImGui::Combo("Resolution", &resPreset, presets, 5) && resPreset < 4) {
        rt.width = presetSizes[resPreset][0]; rt.height = presetSizes[resPreset][1];
    }
    if (resPreset == 4) { ImGui::InputInt("Width", &rt.width, 0); ImGui::InputInt("Height", &rt.height, 0); }
    ImGui::Checkbox("Transparent film", &rt.transparentFilm);

    ImGui::Spacing(); ImGui::TextColored(kBrass, "SAMPLING"); ImGui::Separator();
    ImGui::SliderInt("Samples", &rt.samples, 1, 2048, "%d", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderInt("Seed", &rt.seed, 0, 1024);

    ImGui::Spacing(); ImGui::TextColored(kBrass, "LIGHT PATHS"); ImGui::Separator();
    ImGui::SliderInt("Max Bounces", &rt.maxBounces, 0, 32);
    ImGui::SliderInt("Diffuse", &rt.diffuseBounces, 0, 32);
    ImGui::SliderInt("Glossy", &rt.glossyBounces, 0, 32);
    ImGui::SliderInt("Transmission", &rt.transmissionBounces, 0, 32);
    ImGui::Checkbox("Clamp indirect", &rt.clampEnabled);
    ImGui::BeginDisabled(!rt.clampEnabled);
    ImGui::SliderFloat("Clamp", &rt.clampIndirect, 0.5f, 32.0f, "%.1f");
    ImGui::EndDisabled();

    ImGui::Spacing(); ImGui::TextColored(kBrass, "DENOISE"); ImGui::Separator();
    ImGui::Checkbox("Denoise result", &rt.denoise);
    ImGui::BeginDisabled(!rt.denoise);
    ImGui::PushItemWidth(-130.0f);
    ImGui::SliderFloat("Strength##dn", &rt.denoiseStrength, 0.1f, 3.0f, "%.2f");
    ImGui::PopItemWidth();
    ImGui::EndDisabled();
    ImGui::Checkbox("Emissive surfaces light the scene", &rt.emissiveLights);

    ImGui::Spacing(); ImGui::TextColored(kBrass, "SHADOWS"); ImGui::Separator();
    ImGui::Checkbox("Trace shadows", &rt.shadows);
    ImGui::SliderFloat("Sun Angle", &rt.sunAngleDeg, 0.1f, 20.0f, "%.2f deg");
    ImGui::TextDisabled("Wider sun = softer shadow edges.");

    ImGui::Spacing(); ImGui::TextColored(kBrass, "PERFORMANCE"); ImGui::Separator();
    ImGui::SliderInt("Threads (0 = all)", &rt.threads, 0, 64);
    ImGui::PopItemWidth();

    ImGui::Spacing();
    if (tracer.Running()) {
        ImGui::ProgressBar(tracer.Progress(), ImVec2(-1, 0));
        if (ImGui::Button("Cancel", ImVec2(-1, 0))) { tracer.Cancel(); ShowStatus("Render cancelled"); }
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.54f, 0.37f, 0.22f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kBrass);
        if (ImGui::Button("Render Image  (F12)", ImVec2(-1, 34))) StartRender();
        ImGui::PopStyleColor(2);
    }

    if (tracer.HasResult()) {
        const RayTraceStats& st = tracer.Stats();
        ImGui::TextDisabled("%d x %d  |  %.2f s  |  %zu triangles  |  %d threads",
                            tracer.Width(), tracer.Height(), st.seconds, st.triangles, st.threadsUsed);
        if (renderResultDirty) {
            renderResult.Destroy(vk);
            renderResult.Create(vk, tracer.Pixels().data(), tracer.Width(), tracer.Height());
            renderResultDirty = false;
        }
        if (renderResult.Valid()) {
            float w = ImGui::GetContentRegionAvail().x;
            ImGui::Image(renderResult.Id(), ImVec2(w, w * tracer.Height() / std::max(1, tracer.Width())));
        }
        if (ImGui::Button("Save PNG...", ImVec2(-1, 0))) {
            std::string out = projectPath.empty() ? "render.png" : projectPath + ".render.png";
            if (tracer.SavePng(out)) ShowStatus("Saved " + out);
            else ShowStatus("Could not save the image");
        }
    }
    ImGui::End();
}

// ---------------------------------------------------------------- HDRI
static void SDLCALL OnHdriChosen(void* userdata, const char* const* filelist, int) {
    if (!filelist || !filelist[0]) return;
    static_cast<App*>(userdata)->QueueHdri(filelist[0]);
}

void App::RequestHdriImport() {
    static const SDL_DialogFileFilter filters[] = {
        {"Environment images (hdr, exr-as-png, jpg, png)", "hdr;png;jpg;jpeg;tga;bmp"},
        {"All files", "*"},
    };
    SDL_ShowOpenFileDialog(OnHdriChosen, this, window, filters, 2, nullptr, false);
}

// ---------------------------------------------------------------- sculpting
// Brushes write into the multires displacement when a level exists, so the base mesh keeps its
// clean topology and the detail can survive a level change.
void App::SculptStroke(bool begin) {
    MeshObject* o = scene.Active();
    if (!o || !lastPick.hit) return;
    // the tube is rebuilt from the curve on every curve change, which would wipe the strokes
    if (o->isCurve) { if (begin) ShowStatus("Convert the curve to a mesh before sculpting it"); return; }
    const auto& brush = kSculptBrushes[sculpt.brush];
    const bool smoothing = ImGui::GetIO().KeyShift || brush.icon == Icon::SculptSmooth;   // Shift smooths with any brush
    if (!smoothing && !SculptBrushSupported(brush.icon)) {
        if (begin) ShowStatus(std::string(brush.name) + " brush is not implemented yet");
        return;
    }

    const bool multi = o->multires.valid && o->multires.levels > 0;
    Mesh& target = multi ? o->multires.cage : o->mesh;
    std::vector<Float3>* disp = multi ? &o->multires.displacement : nullptr;
    if (target.positions.empty()) return;
    if (begin || sculptBefore.empty()) {   // also covers Shift pressed mid-stroke on an unsupported brush
        sculptUndoIndex = scene.activeIndex;
        sculptUndoMulti = multi;
        if (disp && disp->size() != target.positions.size()) disp->assign(target.positions.size(), Float3{0, 0, 0});
        sculptBefore = disp ? *disp : target.positions;
    }

    // The brush radius is in screen pixels, so convert it to world units at the hit depth.
    Vec3 eye = viewport.CameraEye();
    Float3 hp = lastPick.point;
    float depth = std::max(0.2f, (hp.x - eye.x) * viewport.CameraForward().x +
                                 (hp.y - eye.y) * viewport.CameraForward().y +
                                 (hp.z - eye.z) * viewport.CameraForward().z);
    float radius = sculpt.radiusInSceneUnits ? sculpt.radiusPx : sculpt.radiusPx * viewport.WorldPerPixel(depth);
    // shortcut: non-uniform object scale uses the largest axis for the radius; exact would need an ellipsoid test.
    const Float3& sc = o->transform.scale;
    float maxScale = std::max({std::fabs(sc.x), std::fabs(sc.y), std::fabs(sc.z), 1e-6f});

    SculptDab dab;
    dab.center = o->transform.InverseApply(hp);
    Float3 last = o->transform.InverseApply(lastSculptPoint);
    if (!begin) dab.drag = {dab.center.x - last.x, dab.center.y - last.y, dab.center.z - last.z};
    dab.radius = radius / maxScale;
    dab.strength = sculpt.strength * (sculpt.subtract || ImGui::GetIO().KeyCtrl ? -1.0f : 1.0f);
    dab.smooth = smoothing;
    const BrushImage* stamp = brush.icon == Icon::SculptCustomImage && brushImage.Loaded() ? &brushImage : nullptr;
    ApplySculptDab(target, disp, sculpt, brush.icon, dab, stamp);

    if (multi) {
        o->displayMesh = o->multires.Evaluate();
        o->hasDisplay = true;
    } else {
        o->mesh.ComputeNormals();
        o->MarkTopoDirty();
        // the stack feeds off the base mesh, so it has to re-evaluate after every stroke
        o->stackDirty = true;
    }
    uvLayout.valid = false;        // seams and islands move with the surface
    lastSculptPoint = hp;
}

// One undo step per stroke: the positions (or multires displacement) before and after.
void App::SculptStrokeEnd() {
    if (sculptBefore.empty() || sculptUndoIndex < 0 || sculptUndoIndex >= (int)scene.objects.size()) return;
    MeshObject* o = scene.objects[sculptUndoIndex].get();
    const bool multi = sculptUndoMulti;
    auto before = std::make_shared<std::vector<Float3>>(std::move(sculptBefore));
    auto after = std::make_shared<std::vector<Float3>>(multi ? o->multires.displacement : o->mesh.positions);
    sculptBefore.clear();
    if (before->size() != after->size()) return;
    int idx = sculptUndoIndex;
    auto apply = [this, idx, multi](const std::vector<Float3>& v) {
        if (idx >= (int)scene.objects.size()) return;
        MeshObject* t = scene.objects[idx].get();
        if (multi) {
            if (t->multires.displacement.size() != v.size()) return;
            t->multires.displacement = v;
            t->displayMesh = t->multires.Evaluate();
            t->hasDisplay = true;
        } else {
            if (t->mesh.positions.size() != v.size()) return;
            t->mesh.positions = v;
            t->mesh.ComputeNormals();
            t->MarkTopoDirty();
            t->stackDirty = true;
        }
        uvLayout.valid = false;
    };
    undoStack.Push(std::string("Sculpt ") + kSculptBrushes[sculpt.brush].name,
        [apply, before] { apply(*before); }, [apply, after] { apply(*after); },
        (before->size() + after->size()) * sizeof(Float3));
}

void App::MultiresPanel() {
    if (!ImGui::Begin("Multiresolution", &showMultires)) { ImGui::End(); return; }
    MeshObject* o = scene.Active();
    if (!o) { ImGui::TextDisabled("No object selected."); ImGui::End(); return; }

    ImGui::TextColored(kBrass, "%s", o->name.c_str());
    if (o->multires.valid) {
        ImGui::TextDisabled("base %zu verts  ->  level %d: %zu verts", o->mesh.VertexCount(),
                            o->multires.levels, o->multires.VertexCount());
        size_t touched = 0;
        for (const Float3& d : o->multires.displacement)
            if (std::fabs(d.x) + std::fabs(d.y) + std::fabs(d.z) > 1e-6f) ++touched;
        ImGui::TextDisabled("%zu displaced vertices", touched);
    } else {
        ImGui::TextDisabled("base %zu verts, no subdivision levels yet", o->mesh.VertexCount());
    }
    ImGui::Separator();

    static int levels = 2;
    ImGui::PushItemWidth(-120.0f);
    ImGui::SliderInt("Levels", &levels, 1, 5);
    ImGui::PopItemWidth();
    if (ImGui::Button("Subdivide / Rebuild", ImVec2(-1, 0))) {
        o->multires.Rebuild(o->mesh, levels);
        o->displayMesh = o->multires.Evaluate();
        o->hasDisplay = true;
        ShowStatus("Multires level " + std::to_string(levels) + ": " +
                   std::to_string(o->multires.VertexCount()) + " verts");
    }
    ImGui::BeginDisabled(!o->multires.valid);
    if (ImGui::Button("Apply to Base Mesh", ImVec2(-1, 0))) {
        auto before = std::make_shared<Mesh>(o->mesh);
        o->mesh = o->multires.Evaluate();
        o->multires.Clear();
        o->hasDisplay = false;
        o->MarkTopoDirty();
        o->stackDirty = true;
        uvLayout.valid = false;
        auto after = std::make_shared<Mesh>(o->mesh);
        int idx = scene.activeIndex;
        undoStack.Push("Apply Multires",
            [this, idx, before] { if (idx < (int)scene.objects.size()) { scene.objects[idx]->mesh = *before; scene.objects[idx]->MarkTopoDirty(); } },
            [this, idx, after] { if (idx < (int)scene.objects.size()) { scene.objects[idx]->mesh = *after; scene.objects[idx]->MarkTopoDirty(); } },
            before->positions.size() * sizeof(Float3) * 2);
        ShowStatus("Baked multires detail into the base mesh");
    }
    if (ImGui::Button("Clear Sculpted Detail", ImVec2(-1, 0))) {
        std::fill(o->multires.displacement.begin(), o->multires.displacement.end(), Float3{0, 0, 0});
        o->displayMesh = o->multires.Evaluate();
        ShowStatus("Detail cleared");
    }
    ImGui::EndDisabled();
    ImGui::Spacing();
    Hint("Sculpt in Sculpt Mode and the strokes go into this displacement layer, leaving the base "
         "mesh untouched. Draw, Clay, Inflate, Flatten, Grab and Smooth are wired up; Shift smooths "
         "with any brush and Ctrl inverts.");
    ImGui::End();
}

// ---------------------------------------------------------------- built-in environments
void App::BuildHdriThumbnails() {
    if (hdriThumbsBuilt) return;
    HdriImage tmp;
    std::vector<uint8_t> rgba;
    for (int i = 0; i < (int)HdriPreset::Count; ++i) {
        GenerateHdri((HdriPreset)i, tmp, 128);
        rgba.assign((size_t)tmp.width * tmp.height * 4, 255);
        for (size_t p = 0; p < (size_t)tmp.width * tmp.height; ++p) {
            for (int c = 0; c < 3; ++c) {
                // the previews are tonemapped so a 45x sun does not blow the whole strip white
                float v = tmp.pixels[p * 3 + c];
                v = 1.0f - std::exp(-v);
                rgba[p * 4 + c] = (uint8_t)std::clamp(std::pow(v, 1.0f / 2.2f) * 255.0f, 0.0f, 255.0f);
            }
        }
        hdriThumbs[i].Create(vk, rgba.data(), tmp.width, tmp.height);
    }
    hdriThumbsBuilt = true;
}

void App::ApplyHdriPreset(int preset) {
    if (preset < 0 || preset >= (int)HdriPreset::Count) return;
    GenerateHdri((HdriPreset)preset, hdri, 1024);
    hdriPreset = preset;
    hdriPath = std::string(HdriPresetName((HdriPreset)preset)) + " (built in)";
    hdriBgDirty = true;
    ShowStatus(std::string("Environment: ") + HdriPresetName((HdriPreset)preset));
}

// Converts the active HdriImage (linear float) into an RGBA8 texture the viewport can sample as
// its background. Re-uploaded only when the image, rotation baking, or clear happens - not every
// frame - because a 1024x512 conversion is not free.
void App::SyncHdriBackground() {
    if (!hdriBgDirty) return;
    hdriBgDirty = false;
    if (!hdri.Loaded()) { hdriBgTexture.Destroy(vk); return; }

    std::vector<unsigned char> rgba((size_t)hdri.width * hdri.height * 4);
    for (size_t p = 0; p < (size_t)hdri.width * hdri.height; ++p) {
        for (int c = 0; c < 3; ++c) {
            float v = hdri.pixels[p * 3 + c] * hdriStrength;
            v = 1.0f - std::exp(-std::max(0.0f, v));           // filmic-ish, matches the render tonemap
            rgba[p * 4 + c] = (unsigned char)std::clamp(std::pow(v, 1.0f / 2.2f) * 255.0f, 0.0f, 255.0f);
        }
        rgba[p * 4 + 3] = 255;
    }
    if (hdriBgTexture.Valid() && hdriBgTexture.width == hdri.width && hdriBgTexture.height == hdri.height)
        hdriBgTexture.Update(vk, rgba.data(), hdri.width, hdri.height);
    else {
        hdriBgTexture.Destroy(vk);
        hdriBgTexture.Create(vk, rgba.data(), hdri.width, hdri.height);
    }
}

// ---------------------------------------------------------------- camera gizmo
// Drawn whenever we are not looking through it: a body, a frustum and an up indicator, so the
// camera is a visible, clickable object in the scene rather than an invisible setting.
void App::DrawCameraGizmo() {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 vsize = viewport.ViewportSize();
    float aspect = vsize.y > 0 ? vsize.x / vsize.y : 1.77f;

    for (int ci = 0; ci < (int)scene.cameras.size(); ++ci) {
        // looking through this one: nothing to draw
        if (cameraView && ci == scene.activeCamera) continue;
        const SceneCamera& c = scene.cameras[ci];
        bool sel = selectedCamera == ci;
        bool isActive = scene.activeCamera == ci;

        float cp = std::cos(c.pitch), sp = std::sin(c.pitch);
        Float3 fwd{-std::sin(c.yaw) * cp, -sp, -std::cos(c.yaw) * cp};
        Float3 eye = c.Eye();
        Float3 right{-fwd.z, 0.0f, fwd.x};
        float rl = std::sqrt(right.x * right.x + right.z * right.z);
        if (rl > 1e-6f) right = {right.x / rl, 0.0f, right.z / rl};
        Float3 up{right.y * fwd.z - right.z * fwd.y, right.z * fwd.x - right.x * fwd.z,
                  right.x * fwd.y - right.y * fwd.x};

        float half = std::tan(c.FovYDegrees(aspect) * 0.5f * 3.14159265f / 180.0f);
        const float len = 0.9f;
        float hh = half * len, hw = hh * aspect;
        auto at = [&](float f, float r, float u) {
            return Float3{eye.x + fwd.x * f + right.x * r + up.x * u,
                          eye.y + fwd.y * f + right.y * r + up.y * u,
                          eye.z + fwd.z * f + right.z * r + up.z * u};
        };
        Float3 corners[4] = {at(len, -hw, -hh), at(len, hw, -hh), at(len, hw, hh), at(len, -hw, hh)};
        Float3 body[8] = {at(-0.28f, -0.14f, -0.10f), at(-0.28f, 0.14f, -0.10f), at(-0.28f, 0.14f, 0.10f), at(-0.28f, -0.14f, 0.10f),
                          at(0.0f, -0.14f, -0.10f), at(0.0f, 0.14f, -0.10f), at(0.0f, 0.14f, 0.10f), at(0.0f, -0.14f, 0.10f)};

        ImU32 col = sel ? IM_COL32(255, 170, 60, 255) : IM_COL32(190, 200, 220, 230);
        ImU32 fill = sel ? IM_COL32(255, 170, 60, 60) : IM_COL32(150, 160, 180, 45);
        float thick = sel ? 2.0f : 1.4f;
        auto proj = [&](const Float3& p, ImVec2& out) { float z; return viewport.ProjectPoint(p, z, out); };
        auto line = [&](const Float3& a, const Float3& b) {
            ImVec2 s1, s2;
            if (proj(a, s1) && proj(b, s2)) dl->AddLine(s1, s2, col, thick);
        };
        auto quadFill = [&](const Float3& a, const Float3& b, const Float3& c2, const Float3& d, ImU32 f) {
            ImVec2 pa, pb, pc, pd;
            if (proj(a, pa) && proj(b, pb) && proj(c2, pc) && proj(d, pd)) dl->AddQuadFilled(pa, pb, pc, pd, f);
        };

        // a solid-reading body box, so it reads as a camera and not just lines
        static const int faces[6][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}, {0, 1, 5, 4}, {3, 2, 6, 7}, {0, 3, 7, 4}, {1, 2, 6, 5}};
        for (auto& f : faces) quadFill(body[f[0]], body[f[1]], body[f[2]], body[f[3]], fill);
        for (int i = 0; i < 4; ++i) {
            line(body[i], body[(i + 1) % 4]);
            line(body[i + 4], body[(i + 1) % 4 + 4]);
            line(body[i], body[i + 4]);
        }
        // lens cone out to the frame
        for (int i = 0; i < 4; ++i) {
            line(eye, corners[i]);
            line(corners[i], corners[(i + 1) % 4]);
        }
        // up triangle: filled on the active camera, the way Blender marks it
        Float3 tip = at(len, 0.0f, hh * 1.7f);
        Float3 l = at(len, -hw * 0.5f, hh * 1.15f), r = at(len, hw * 0.5f, hh * 1.15f);
        ImVec2 st, sl, sr;
        if (proj(tip, st) && proj(l, sl) && proj(r, sr)) {
            if (isActive) dl->AddTriangleFilled(sl, sr, st, col);
            else dl->AddTriangle(sl, sr, st, col, thick);
        }

        ImVec2 se;
        if (proj(eye, se)) {
            dl->AddCircleFilled(se, sel ? 4.5f : 3.0f, col);
            dl->AddText(ImVec2(se.x + 8, se.y - 6), col, scene.cameraNames[ci].c_str());
        }
    }
}

// ---------------------------------------------------------------- Add > Camera
// Adds a camera aligned to the current view, so it frames what you are looking at, and makes it
// the selection. It becomes active only if it is the first one; switch with Ctrl+Numpad 0.
void App::AddCameraAtView() {
    SceneCamera c = scene.Camera();       // copy lens settings from the current active camera
    c.yaw = viewport.Yaw();
    c.pitch = viewport.Pitch();
    c.distance = viewport.Distance();
    Vec3 t = viewport.Target();
    c.target = {t.x, t.y, t.z};
    int idx = scene.AddCamera(c);
    selectedCamera = idx;
    for (auto& ob : scene.objects) ob->selected = false;
    showCamera = true;
    showHierarchy = true;
    undoStack.Push("Add Camera",
        [this, idx] {
            if (idx < (int)scene.cameras.size() && scene.cameras.size() > 1) {
                scene.cameras.erase(scene.cameras.begin() + idx);
                scene.cameraNames.erase(scene.cameraNames.begin() + idx);
                scene.activeCamera = std::clamp(scene.activeCamera, 0, (int)scene.cameras.size() - 1);
                selectedCamera = -1;
            }
        },
        [this, c] { scene.AddCamera(c); });
    ShowStatus("Added " + scene.cameraNames[idx] + " from the current view - Ctrl+Numpad 0 makes it active");
}

// ---------------------------------------------------------------- Shift+D duplicate
void App::DuplicateSelected() {
    if (mode3D == Mode3D::Edit && scene.Active() && scene.Active()->isCurve) { ShowStatus("Use E on an end point to add curve points"); return; }
    if (mode3D == Mode3D::Edit) { RunEditOp('D'); return; }
    if (mode3D != Mode3D::Object) { ShowStatus("Shift+D duplicates objects in Object Mode or faces in Edit Mode"); return; }
    std::vector<int> picked;
    for (int i = 0; i < (int)scene.objects.size(); ++i) if (scene.objects[i]->selected) picked.push_back(i);
    if (picked.empty() && scene.Active()) picked.push_back(scene.activeIndex);
    if (picked.empty()) { ShowStatus("Nothing selected to duplicate"); return; }

    size_t firstNew = scene.objects.size();
    for (int i : picked) {
        auto copy = std::make_unique<MeshObject>(*scene.objects[i]);
        copy->name = scene.UniqueName(scene.objects[i]->name);
        copy->rigidBody.cache.clear();   // a copy must not replay the original's baked path
        DropClothBake(*copy, false);     // nor its cloth bake: back to the rest shape
        copy->id = 0;
        scene.AssignId(*copy);
        copy->selected = true;
        copy->active = false;
        scene.objects.push_back(std::move(copy));
    }
    for (size_t i = 0; i < firstNew; ++i) scene.objects[i]->selected = false;
    scene.SetActive((int)scene.objects.size() - 1);
    scene.objects.back()->active = true;

    size_t count = scene.objects.size() - firstNew;
    // The copies move into the step on undo and back on redo, found by identity.
    std::vector<MeshObject*> copies;
    for (size_t i = firstNew; i < scene.objects.size(); ++i) copies.push_back(scene.objects[i].get());
    auto held = std::make_shared<std::vector<std::unique_ptr<MeshObject>>>();
    undoStack.Push("Duplicate",
        [this, copies, held] {
            for (MeshObject* c : copies) { int i = scene.IndexOf(c); if (i >= 0) held->push_back(scene.Take(i)); }
        },
        [this, held] {
            for (auto& c : *held) scene.Insert((int)scene.objects.size(), std::move(c));
            held->clear();
        });
    ShowStatus("Duplicated " + std::to_string(count) + " object" + (count == 1 ? "" : "s") + " - move the mouse, click to place");
    // Blender goes straight into moving the copy
    BeginTransform(0);
    if (std::getenv("LEORIX_DEBUG_KEYS")) {
        std::printf("[dup] objects now %zu, active=%d, grab active=%d\n", scene.objects.size(), scene.activeIndex, xform.active ? 1 : 0);
        std::fflush(stdout);
    }
}

// ---------------------------------------------------------------- LMB box select
void App::FinishBoxSelect(ImVec2 a, ImVec2 b, bool extend, bool subtract) {
    ImVec2 mn(std::min(a.x, b.x), std::min(a.y, b.y)), mx(std::max(a.x, b.x), std::max(a.y, b.y));
    auto inside = [&](const Float3& world) {
        float z; ImVec2 s;
        if (!viewport.ProjectPoint(world, z, s)) return false;
        return s.x >= mn.x && s.x <= mx.x && s.y >= mn.y && s.y <= mx.y;
    };

    if (mode3D == Mode3D::Edit) {
        MeshObject* o = scene.Active();
        if (!o) return;
        if (o->isCurve) {
            if (!extend && !subtract) o->curveSel.clear();
            std::vector<int> in = CurveElemsInside(o->curve, [&](const Float3& p) { return inside(o->transform.Apply(p)); });
            for (int e : in) { if (subtract) o->curveSel.erase(e); else o->curveSel.insert(e); }
            ShowStatus("Box select: " + std::to_string(in.size()) + " curve element" + (in.size() == 1 ? "" : "s"));
            return;
        }
        const Mesh& m = o->mesh;
        std::vector<char> in(m.positions.size());
        for (size_t i = 0; i < m.positions.size(); ++i) in[i] = inside(o->transform.Apply(m.positions[i])) ? 1 : 0;
        if (!extend && !subtract) { o->selVerts.clear(); o->selEdges.clear(); o->selFaces.clear(); }
        size_t hits = 0;
        if (edit.selectMode & 1) {
            for (size_t i = 0; i < in.size(); ++i) if (in[i]) { if (subtract) o->selVerts.erase((uint32_t)i); else o->selVerts.insert((uint32_t)i); ++hits; }
        }
        if (edit.selectMode & 2) {
            o->BuildTopology();
            for (uint64_t e : o->polyEdges) {
                uint32_t a2 = uint32_t(e >> 32), b2 = uint32_t(e & 0xFFFFFFFF);
                if (in[a2] && in[b2]) { if (subtract) o->selEdges.erase(e); else o->selEdges.insert(e); ++hits; }
            }
        }
        if (edit.selectMode & 4) {
            // a face is inside when every one of its corners is
            std::unordered_map<uint32_t, bool> faceIn;
            for (const Tri& t : m.tris) {
                bool all = in[t.a] && in[t.b] && in[t.c];
                auto it = faceIn.find(t.poly);
                faceIn[t.poly] = (it == faceIn.end()) ? all : (it->second && all);
            }
            for (auto& kv : faceIn) if (kv.second) { if (subtract) o->selFaces.erase(kv.first); else o->selFaces.insert(kv.first); ++hits; }
        }
        ShowStatus("Box select: " + std::to_string(hits) + " element" + (hits == 1 ? "" : "s"));
        return;
    }

    // Object mode: an object is caught when any of its vertices lands in the box
    if (!extend && !subtract) for (auto& ob : scene.objects) ob->selected = false;
    int last = -1, hits = 0;
    for (int i = 0; i < (int)scene.objects.size(); ++i) {
        MeshObject& ob = *scene.objects[i];
        if (!ob.visible) continue;
        const Mesh& mesh = ob.hasDisplay ? ob.displayMesh : ob.mesh;
        bool caught = false;
        size_t step = std::max<size_t>(1, mesh.positions.size() / 4000);   // sample dense meshes
        for (size_t v = 0; v < mesh.positions.size() && !caught; v += step) caught = inside(ob.transform.Apply(mesh.positions[v]));
        if (!caught) continue;
        ob.selected = !subtract;
        if (!subtract) last = i;
        ++hits;
    }
    if (last >= 0) scene.SetActive(last);
    selectedCamera = -1;
    ShowStatus("Box select: " + std::to_string(hits) + " object" + (hits == 1 ? "" : "s"));
}
