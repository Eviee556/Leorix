// UV editing on the 2D canvas and painting on the 3D model. Both work on the active object; the
// math lives in mesh/UVEdit so the self-test can check it without a window.
#include "app/App.h"
#include "mesh/UVEdit.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
// Puts face-corner UVs back and keeps the per-vertex copy in step, as Unwrap does.
void SetLoopUVs(Mesh& m, const std::vector<Float2>& uv) {
    if (uv.size() != m.loopUVs.size()) return;
    m.loopUVs = uv;
    if (m.uvs.size() != m.positions.size()) return;
    for (size_t ti = 0; ti < m.tris.size(); ++ti) {
        const Tri& t = m.tris[ti];
        m.uvs[t.a] = uv[ti * 3]; m.uvs[t.b] = uv[ti * 3 + 1]; m.uvs[t.c] = uv[ti * 3 + 2];
    }
}
}

// ---------------------------------------------------------------- UV editing
void App::BeginUVGesture() {
    MeshObject* o = scene.Active();
    uvGestureBase = o ? o->mesh.loopUVs : std::vector<Float2>{};
}

void App::EndUVGesture(const char* label) {
    MeshObject* o = scene.Active();
    std::vector<Float2> base = std::move(uvGestureBase);
    uvGestureBase.clear();
    if (!o || base.size() != o->mesh.loopUVs.size()) return;
    if (base.empty() || std::memcmp(base.data(), o->mesh.loopUVs.data(), base.size() * sizeof(Float2)) == 0) return;   // a click, not a move
    auto before = std::make_shared<std::vector<Float2>>(std::move(base));
    auto after = std::make_shared<std::vector<Float2>>(o->mesh.loopUVs);
    SetLoopUVs(o->mesh, *after);
    o->stackDirty = true;
    MeshObject* obj = o;
    auto apply = [this, obj](const std::vector<Float2>& uv) {
        if (scene.IndexOf(obj) < 0) return;
        SetLoopUVs(obj->mesh, uv);
        obj->stackDirty = true;
        uvLayout.valid = false;
    };
    undoStack.Push(label, [apply, before] { apply(*before); }, [apply, after] { apply(*after); },
                   (before->size() + after->size()) * sizeof(Float2));
    uvLayout.valid = false;
}

bool App::UVCanvasEdit(ImVec2 a, ImVec2 b) {
    MeshObject* o = scene.Active();
    if (!o || !o->mesh.HasLoopUVs()) return false;   // nothing to edit, so the brush keeps working
    Mesh& m = o->mesh;
    if (uvSelOwner != o || uvSel.size() != m.loopUVs.size()) { uvSel.assign(m.loopUVs.size(), 0); uvSelOwner = o; uvDragging = false; }
    const float w = b.x - a.x, h = b.y - a.y;
    if (w <= 0 || h <= 0) return true;
    ImGuiIO& io = ImGui::GetIO();
    const Float2 mu{(io.MousePos.x - a.x) / w, 1.0f - (io.MousePos.y - a.y) / h};

    if (uvDragging) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            TransformUVSelection(m, uvGestureBase, uvSel, {mu.u - uvDragStart.u, mu.v - uvDragStart.v}, 0.0f, {1, 1}, {0, 0});
            uvLayout.valid = false;
        } else {
            uvDragging = false;
            EndUVGesture("Move UVs");
        }
        return true;
    }
    if (!canvas2DHovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return true;

    const bool shift = io.KeyShift;
    const int tri = PickUVTriangle(m, mu);
    const int corner = uvSelectIslands ? -1 : PickUVCorner(m, mu, 8.0f / w);   // 8 screen pixels
    const bool hit = uvSelectIslands ? tri >= 0 : corner >= 0;
    const bool wasSelected = hit && uvSel[uvSelectIslands ? (size_t)tri * 3 : (size_t)corner];
    if (!shift && !wasSelected) std::fill(uvSel.begin(), uvSel.end(), 0);
    if (hit && !(wasSelected && !shift)) {
        const bool value = !(shift && wasSelected);   // Shift+click toggles
        if (uvSelectIslands) {
            std::vector<int> islands;
            UVIslands(m, islands);
            SelectUVIsland(m, islands, tri, uvSel, value);
        } else {
            SelectUVVertex(m, corner, uvSel, value);
        }
    }
    // pressing on something selected starts a move; a plain click without moving changes nothing
    if (hit && uvSel[uvSelectIslands ? (size_t)tri * 3 : (size_t)corner]) {
        BeginUVGesture();
        uvDragging = true;
        uvDragStart = mu;
    }
    return true;
}

// ---------------------------------------------------------------- painting on the model
void App::PaintOnModel(bool begin) {
    MeshObject* o = scene.Active();
    MapDoc* md = paint.Active();
    if (!o || !md) { if (begin) ShowStatus("Make a texture map first (Maps panel), then paint on the model"); return; }
    if (!lastPick.hit || lastPickObject != scene.activeIndex) return;   // off the model: skip the dab, keep the stroke
    const Mesh& mesh = o->hasDisplay ? o->displayMesh : o->mesh;          // the mesh the pick ran on
    if (!mesh.HasLoopUVs()) { if (begin) ShowStatus("This model has no UVs yet: Unwrap or Smart UV Project it first"); return; }

    Float2 uv;
    if (!SurfaceUV(mesh, lastPick.tri, o->transform.InverseApply(lastPick.point), uv)) return;
    // screen radius -> surface units (as sculpting measures it) -> map pixels at this face
    Vec3 eye = viewport.CameraEye(), fwd = viewport.CameraForward();
    const Float3& hp = lastPick.point;
    float depth = std::max(0.2f, (hp.x - eye.x) * fwd.x + (hp.y - eye.y) * fwd.y + (hp.z - eye.z) * fwd.z);
    float worldR = sculpt.radiusInSceneUnits ? sculpt.radiusPx : sculpt.radiusPx * viewport.WorldPerPixel(depth);
    const Float3& sc = o->transform.scale;
    float localR = worldR / std::max({std::fabs(sc.x), std::fabs(sc.y), std::fabs(sc.z), 1e-6f});
    float tpu = TexelsPerUnit(mesh, lastPick.tri, md->width, md->height);
    if (tpu <= 0.0f) return;
    float sizePx = std::clamp(2.0f * localR * tpu, 1.0f, (float)std::max(md->width, md->height));

    if (begin) {
        paintingModel = true;
        bool linked = false;
        for (const std::string& p : o->material.paths) linked |= p == md->name + " (painted)";
        ShowStatus(linked ? "Painting " + md->name + " on " + o->name
                          : "Painting into " + md->name + ". Drag the map onto a material slot to see it on the model");
    }
    PaintStroke(ImVec2(uv.u * md->width, (1.0f - uv.v) * md->height), begin, sizePx);
    // keep the model's copy roughly live without uploading the map every frame
    double now = ImGui::GetTime();
    if (now >= modelPaintRefreshAt) { RefreshPaintedSlots(paint.activeMap); modelPaintRefreshAt = now + 0.15; }
}

void App::PaintStrokeEnd() {
    const bool fromModel = paintingModel;
    painting = paintingModel = false;
    std::shared_ptr<StrokeUndo> stroke = std::move(activeStroke);
    activeStroke.reset();
    if (!stroke || stroke->Empty()) return;
    if (stroke->mapIndex < 0 || stroke->mapIndex >= (int)paint.maps.size()) return;
    MapDoc& md = paint.maps[stroke->mapIndex];
    if (stroke->layerIndex < 0 || stroke->layerIndex >= (int)md.layers.size()) return;
    stroke->CaptureAfter(md.layers[stroke->layerIndex]);
    auto restore = [this, stroke](bool useBefore) {
        if (stroke->mapIndex < 0 || stroke->mapIndex >= (int)paint.maps.size()) return;
        MapDoc& d = paint.maps[stroke->mapIndex];
        if (stroke->layerIndex < 0 || stroke->layerIndex >= (int)d.layers.size()) return;
        stroke->Restore(d.layers[stroke->layerIndex], useBefore);
        d.dirty.Whole(d.width, d.height);
        RefreshPaintedSlots(stroke->mapIndex);
    };
    const char* name = fromModel ? "Paint on Model" : kTools2D[tool2D].name ? kTools2D[tool2D].name : "Stroke";
    undoStack.Push(name, [restore] { restore(true); }, [restore] { restore(false); }, stroke->Bytes());
    RefreshPaintedSlots(stroke->mapIndex);
}

// A map dropped on a slot is copied into the material under the label "<map> (painted)"; any slot
// still carrying that label follows the map, so painting shows up on the model.
void App::RefreshPaintedSlots(int mapIndex) {
    if (mapIndex < 0 || mapIndex >= (int)paint.maps.size()) return;
    MapDoc& md = paint.maps[mapIndex];
    const std::string label = md.name + " (painted)";
    bool composited = false;
    for (auto& obj : scene.objects)
        for (int i = 0; i < (int)MapSlot::Count; ++i) {
            if (obj->material.paths[i] != label) continue;
            if (!composited) { md.Composite(); composited = true; }
            if (md.composite.empty()) return;
            GpuTexture& g = obj->material.maps[i];
            // Update in place: destroying the texture mid-frame would free a descriptor ImGui may still draw.
            if (g.Valid() && g.width == md.width && g.height == md.height) {
                g.Update(vk, md.composite.data(), md.width, md.height);
                TextureData& cpu = obj->material.cpu[i];
                cpu.width = md.width; cpu.height = md.height;
                cpu.pixels.assign(md.composite.begin(), md.composite.end());
            } else {
                SetMaterialMap(vk, obj->material, (MapSlot)i, md.composite.data(), md.width, md.height, label);
            }
        }
}
