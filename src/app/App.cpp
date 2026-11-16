#include "app/App.h"
#include "tools/Snap.h"
#include "app/Icons.h"
#include "imgui.h"
#include "imgui_internal.h"   // DockBuilder + viewport side bars
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>

static const char* kToolbar = "Tools", *kCanvas = "2D / UV Canvas", *kViewport = "3D Viewport";
static const char* kToolOptions = "Tool Options", *kColor = "Color", *kLayers = "Layers", *kRender = "Render";
static const char* kRenderSettings = "Render Settings", *kTool3D = "3D Tool Settings";
static const char* kHierarchy = "Hierarchy", *kMaterial = "Material", *kWorld = "Environment";
static const char* kCamera = "Camera", *kArmature = "Armature", *kTimeline = "Timeline";
static const char* kMaps = "Maps", *kUV = "UV Editor", *kModifiers = "Modifiers";
static const char* kRayTracing = "Ray Tracing", *kMultires = "Multiresolution";
static const ImVec4 kBrass(0.79f, 0.54f, 0.29f, 1.0f);

const ToolDef2D kTools2D[] = {
    {Icon::Move2D, "Move", 'V', false}, {Icon::RectSelect, "Rectangle Select", 'M', false},
    {Icon::EllipseSelect, "Ellipse Select", 0, false}, {Icon::Lasso, "Lasso Select", 'Q', false},
    {Icon::MaskSelect, "Selection Mask (paint to select)", 'K', false}, {Icon::None, nullptr, 0, false},
    {Icon::Brush, "Brush", 'B', false}, {Icon::Pencil, "Pencil (hard edge)", 'N', false},
    {Icon::Eraser, "Eraser", 'E', false}, {Icon::Fill, "Fill Bucket", 'G', false},
    {Icon::Eyedropper, "Eyedropper", 'I', false}, {Icon::None, nullptr, 0, false},
    {Icon::Line, "Line", 'L', false}, {Icon::Rect, "Rectangle", 'U', false}, {Icon::Ellipse, "Ellipse", 0, false},
    {Icon::Pen, "Pen (vector path)", 'P', false}, {Icon::Text, "Text", 'T', false},
    {Icon::Balloon, "Speech Balloon", 0, true}, {Icon::None, nullptr, 0, false},
    {Icon::Pan, "Hand / Pan", 'H', false}, {Icon::Zoom, "Zoom", 'Z', false},
};
const int kTools2DCount = (int)(sizeof(kTools2D) / sizeof(kTools2D[0]));

static const unsigned kSwatches[] = {
    0xc98a4b, 0x8a5f37, 0x6f9a8d, 0x3f5e56, 0xe8e3d8, 0x14171c, 0xb25b52, 0x7a3b34,
    0xd8b96f, 0x4a4038, 0x2d3446, 0x9c6b8f, 0x5c7a8a, 0x232833, 0xf0e6d0, 0x000000,
};

// Same four page presets as Electron Leorix's COMIC_PRESETS.
struct ComicPreset { const char* name; const char* desc; int w, h; };
static const ComicPreset kComicPresets[] = {
    {"Vertical Scroll", "800 x 4000 - web strip", 800, 4000},
    {"Comic Page", "2048 x 3145 - print", 2048, 3145},
    {"Manga B5", "2149 x 3035 - tankobon", 2149, 3035},
    {"Landscape Strip", "2400 x 800 - 3-4 panels", 2400, 800},
};

static const char* WorkspaceName(Workspace ws) {
    switch (ws) {
        case Workspace::Texture2D:   return "2D Texture Set";
        case Workspace::Design3D:    return "3D Design";
        case Workspace::Creation3D:  return "3D Creation";
        case Workspace::Animation3D: return "3D Animation";
        case Workspace::Render3D:    return "3D Render";
        case Workspace::Webcomic:    return "Webcomic";
        case Workspace::Scene25D:    return "2.5D Scene";
    }
    return "";
}

App::App(VulkanContext& v, SDL_Window* w) : vk(v), window(w) {
    backends = DetectBackends(vk.gpu);
    paint.New(canvasW, canvasH, MapKind::Albedo, 0xFFFFFFFFu);
    resetLayout = true;
    if (FILE* f = std::fopen("imgui.ini", "rb")) { std::fclose(f); resetLayout = false; }
}

App::~App() {
    brushImage.Release(vk);
    renderResult.Destroy(vk);
    hdriBgTexture.Destroy(vk);
    for (auto& t : hdriThumbs) t.Destroy(vk);
}

void App::ShowStatus(const std::string& msg) { statusMessage = msg; statusUntil = ImGui::GetTime() + 4.0; }

// ---------------------------------------------------------------- frame
void App::Frame() {
    ProcessPendingImport();
    for (auto& obj : scene.objects) if (obj->stackDirty) RefreshModifiers(*obj);
    if (!batchRenderPath.empty() && !tracer.Running() && !tracer.HasResult()) StartRender();
    if (!batchRenderPath.empty() && tracer.HasResult()) {
        bool ok = tracer.SavePng(batchRenderPath);
        std::printf("[render] %s -> %s (%.2f s, %zu tris, %d threads)\n",
                    ok ? "saved" : "FAILED", batchRenderPath.c_str(),
                    tracer.Stats().seconds, tracer.Stats().triangles, tracer.Stats().threadsUsed);
        batchRenderPath.clear();
        quitRequested = true;
    }
    if (playback.playing) {
        double now = ImGui::GetTime();
        if (lastPlayTime <= 0) lastPlayTime = now;
        float advance = (float)((now - lastPlayTime) * playback.fps);
        if (advance >= 1.0f) {
            lastPlayTime = now;
            MeshObject* act = scene.Active();
            int end = act ? act->action.frameEnd : 120;
            int start = act ? act->action.frameStart : 1;
            playback.frame += advance;
            if (playback.frame > end) playback.frame = playback.loop ? (float)start : (float)end;
            if (!playback.loop && playback.frame >= end) playback.playing = false;
            ApplyAnimation(playback.frame);
        }
    }
    HandleShortcuts();
    MenuBar();
    StatusBar();

    ImGuiID dock = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
    if (resetLayout) { BuildDefaultLayout(dock); resetLayout = false; }

    if (!showCanvas2D) canvas2DHovered = false;
    if (showToolbar)        ToolbarPanel();
    if (showCanvas2D)       Canvas2DPanel();
    if (showViewport3D) {
        // keep the header buttons and the app's shading state in sync, and feed the world colours in
        viewport.shadingIndex = (int)shading;
        meshPrefs.xray = edit.xray;
        meshPrefs.faceOrientation = edit.faceOrientation;
        ViewLighting lt = BuildLighting();
        viewport.worldBackground = shading == ShadingMode::Material || shading == ShadingMode::Rendered;
        // An HDRI, when loaded and enabled, replaces the flat gradient in the background - this
        // is the fix for the HDRI not showing up in the viewport, only in F12 renders.
        SyncHdriBackground();
        if (hdri.Loaded() && hdriAsBackground && hdriBgTexture.Valid()) {
            viewport.hdriTexture = hdriBgTexture.Id();
            viewport.hdriRotationRad = hdriRotation;
        } else {
            viewport.hdriTexture = 0;
        }
        auto toCol = [](const float c[3], float k) {
            return IM_COL32((int)std::clamp(c[0] * k * 255.0f, 0.0f, 255.0f),
                            (int)std::clamp(c[1] * k * 255.0f, 0.0f, 255.0f),
                            (int)std::clamp(c[2] * k * 255.0f, 0.0f, 255.0f), 255);
        };
        float k = std::clamp(world.strength * world.exposure, 0.0f, 2.0f);
        viewport.worldTop = toCol(world.color, k);
        viewport.worldBottom = world.background == 1 ? toCol(world.horizon, k) : toCol(world.color, k);
        // Rendered hides the grid without touching the checkbox, so toggling it always sticks.
        viewport.suppressGrid = shading == ShadingMode::Rendered;
        if (cameraView) {
            // Panel edits push into the viewport; otherwise navigating the viewport drives the
            // camera, so flying around in camera view actually moves the camera.
            if (cameraDirty) {
                viewport.SetOrbit(scene.Camera().yaw, scene.Camera().pitch);
                viewport.SetTarget(scene.Camera().target.x, scene.Camera().target.y, scene.Camera().target.z);
                viewport.SetDistance(scene.Camera().distance);
                cameraDirty = false;
            }
            viewport.orthographic = scene.Camera().orthographic;
            viewport.orthoScale = scene.Camera().orthoScale;
            viewport.SetFov(scene.Camera().FovYDegrees(viewport.ViewportSize().x /
                            std::max(1.0f, viewport.ViewportSize().y)));
            viewport.viewLabel = "Camera";
        }
        viewport.modeLabel = Mode3DName(mode3D);
        bool hovered = viewport.Draw(kViewport, &showViewport3D, vk.gpu.name.c_str(),
            [this] { ViewportHeader3D(); },
            [this](ImVec2 o, ImVec2 s) { ViewportShelf(o, s); EditModePick(viewport.hovered); },
            [this](ImVec2, ImVec2) { for (auto& obj : scene.objects) RasterizeDepth(viewport, *obj); });
        if (viewport.envRotateDelta != 0.0f) {
            world.sunAzimuth += viewport.envRotateDelta;
            while (world.sunAzimuth > 180.0f) world.sunAzimuth -= 360.0f;
            while (world.sunAzimuth < -180.0f) world.sunAzimuth += 360.0f;
            char msg[64]; std::snprintf(msg, sizeof(msg), "Environment rotation %.0f deg", world.sunAzimuth);
            ShowStatus(msg);
        }
        HandleViewShortcuts(hovered);

        // Ctrl+E edge menu. Opened and drawn in the same place, so the popup always has a body -
        // an opened popup with nothing drawing it would sit there invisibly.
        if (openEdgeMenu) { ImGui::OpenPopup("Edge Menu"); openEdgeMenu = false; }
        if (ImGui::BeginPopup("Edge Menu")) {
            ImGui::TextDisabled("Edges");
            ImGui::Separator();
            bool can = EdgeOpsAvailable();
            if (ImGui::MenuItem("Mark Seam", nullptr, false, can)) MarkEdges(true, true);
            if (ImGui::MenuItem("Clear Seam", nullptr, false, can)) MarkEdges(true, false);
            ImGui::Separator();
            if (ImGui::MenuItem("Mark Sharp", nullptr, false, can)) MarkEdges(false, true);
            if (ImGui::MenuItem("Clear Sharp", nullptr, false, can)) MarkEdges(false, false);
            if (!can) ImGui::TextDisabled("Select edges in Edge select mode first");
            ImGui::EndPopup();
        }
        UpdateTransform(hovered);
        HandleShortcuts3D(hovered);
        if (cameraView) {
            // write the navigation back so the camera keeps what you framed
            scene.Camera().yaw = viewport.Yaw();
            scene.Camera().pitch = viewport.Pitch();
            scene.Camera().distance = viewport.Distance();
            Vec3 t = viewport.Target();
            scene.Camera().target = {t.x, t.y, t.z};
        }
        shading = (ShadingMode)std::clamp(viewport.shadingIndex, 0, 3);
        if (viewport.shadingPicked) { shadingChosen = true; viewport.shadingPicked = false; }
    }
    if (showMaps)           MapsPanel();
    if (showUV)             UVPanel();
    if (showHistory)        HistoryPanel();
    if (showHierarchy)      HierarchyPanel();
    if (showMaterial)       MaterialPanel();
    if (showWorld)          WorldPanel();
    if (showCamera)         CameraPanel();
    if (showRayTracing)     RayTracingPanel();
    if (showMultires)       MultiresPanel();
    if (showModifiers)      ModifierPanel();
    if (showArmature)       ArmaturePanel();
    if (showTimeline)       TimelinePanel();
    if (showTool3D)         Tool3DPanel();
    if (showToolOptions)    ToolOptionsPanel();
    if (showColor)          ColorPanel();
    if (showLayers)         LayersPanel();
    if (showRender)         RenderPanel();
    if (showRenderSettings) RenderSettingsPanel();
    if (showScripting)      ScriptingPanel();
    if (showVisualScripting) VisualScriptingPanel();
    if (showAbout)          AboutWindow();
    AddPrimitiveMenu();
    NewProjectModal();
}

// ---------------------------------------------------------------- layout & workspaces
void App::BuildDefaultLayout(unsigned dockspaceId) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, vp->WorkSize);

    bool renderWs = workspace == Workspace::Render3D;
    ImGuiID center = dockspaceId, left = 0;
    if (showToolbar) left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 64.0f / std::max(vp->WorkSize.x, 400.0f), nullptr, &center);
    ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, renderWs ? 0.27f : 0.25f, nullptr, &center);
    // Three stacked nodes on the right: Hierarchy (outliner) on top, tool settings in the
    // middle, layers/render at the bottom. Hierarchy gets its own node so it never shares a
    // tab bar with the Render panel.
    ImGuiID rightTop = ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, 0.28f, nullptr, &right);
    ImGuiID rightBottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, renderWs ? 0.30f : 0.45f, nullptr, &right);

    // The remaining `center` is the central node; only split it when both workspaces show,
    // otherwise the hidden one would leave an empty area.
    if (showCanvas2D && showViewport3D) {
        ImGuiID viewportNode = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.55f, nullptr, &center);
        ImGui::DockBuilderDockWindow(kCanvas, center);
        ImGui::DockBuilderDockWindow(kViewport, viewportNode);
    } else {
        ImGui::DockBuilderDockWindow(showViewport3D ? kViewport : kCanvas, center);
    }
    // timeline along the bottom of the central area, like every DCC
    ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.26f, nullptr, &center);
    ImGui::DockBuilderDockWindow(kTimeline, bottom);
    if (left) ImGui::DockBuilderDockWindow(kToolbar, left);
    ImGui::DockBuilderDockWindow(kRenderSettings, right);
    ImGui::DockBuilderDockWindow(kColor, right);
    ImGui::DockBuilderDockWindow(kToolOptions, right);
    ImGui::DockBuilderDockWindow(kMultires, right);
    ImGui::DockBuilderDockWindow(kRayTracing, right);
    ImGui::DockBuilderDockWindow(kModifiers, right);
    ImGui::DockBuilderDockWindow(kArmature, right);
    ImGui::DockBuilderDockWindow(kCamera, right);
    ImGui::DockBuilderDockWindow(kWorld, right);
    ImGui::DockBuilderDockWindow(kMaterial, right);
    ImGui::DockBuilderDockWindow(kTool3D, right);          // docked last = selected tab
    ImGui::DockBuilderDockWindow(kUV, rightTop);
    ImGui::DockBuilderDockWindow(kMaps, rightTop);
    ImGui::DockBuilderDockWindow(kHierarchy, rightTop);
    ImGui::DockBuilderDockWindow(kLayers, rightBottom);
    ImGui::DockBuilderDockWindow(kRender, rightBottom);
    ImGui::DockBuilderFinish(dockspaceId);
    if (left)
        if (ImGuiDockNode* n = ImGui::DockBuilderGetNode(left)) n->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;
}

// Panel sets follow Electron Leorix's setWorkspacePanels() for each startup mode.
void App::ApplyWorkspace(Workspace ws) {
    workspace = ws;
    showToolbar = showToolOptions = showColor = showLayers = showRender = true;
    showRenderSettings = showTool3D = showWorld = showMaterial = showCamera = false;
    showArmature = showTimeline = false;
    comicMode = ws == Workspace::Webcomic;
    switch (ws) {
        case Workspace::Texture2D:
        case Workspace::Webcomic:
            showCanvas2D = true; showViewport3D = false; break;
        case Workspace::Scene25D:
            showCanvas2D = true; showViewport3D = true; break;
        case Workspace::Design3D:
            showCanvas2D = true; showViewport3D = true; showTool3D = true; showMaterial = true;
            showUV = true;
            SetMode3D(Mode3D::Object); break;
        case Workspace::Creation3D:
            showCanvas2D = true; showViewport3D = true; showTool3D = true; SetMode3D(Mode3D::Sculpt); break;
        case Workspace::Animation3D:
            showCanvas2D = false; showViewport3D = true; showTool3D = true;
            showArmature = true; showTimeline = true; showMaterial = true;
            SetMode3D(Mode3D::Object);
            break;
        case Workspace::Render3D:
            showCanvas2D = false; showViewport3D = true;
            showToolbar = showToolOptions = showColor = showLayers = false;
            showRenderSettings = true; showWorld = true; showMaterial = true; showCamera = true;
            shading = ShadingMode::Rendered; SetMode3D(Mode3D::Object);
            break;
    }
    resetLayout = true;
}

// ---------------------------------------------------------------- menus & shortcuts
void App::MenuBar() {
    if (!ImGui::BeginMainMenuBar()) return;
    ImGui::TextColored(kBrass, "LEORIX");
    ImGui::Dummy(ImVec2(6, 0));
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New Project...", "Ctrl+N")) showNewProject = true;
        if (ImGui::MenuItem("Open Project...", "Ctrl+O")) RequestProjectOpen();
        if (ImGui::MenuItem("Save Project As...", "Ctrl+S")) RequestProjectSave();
        ImGui::Separator();
        if (ImGui::MenuItem("Import Mesh...", "Ctrl+I")) RequestMeshImport();
        if (ImGui::MenuItem("Export Mesh...", "Ctrl+E", false, scene.HasMesh())) RequestMeshExport();
        ImGui::TextDisabled("      OBJ, FBX, glTF/GLB, USD");
        ImGui::Separator();
        if (ImGui::MenuItem("Import Sculpt Brush Image...")) RequestBrushImageImport();
        ImGui::Separator();
        if (ImGui::MenuItem("Exit", "Alt+F4")) quitRequested = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        char ul[96], rl[96];
        std::snprintf(ul, sizeof(ul), "Undo %s", undoStack.UndoLabel());
        std::snprintf(rl, sizeof(rl), "Redo %s", undoStack.RedoLabel());
        if (ImGui::MenuItem(ul, "Ctrl+Z", false, undoStack.CanUndo())) undoStack.Undo();
        if (ImGui::MenuItem(rl, "Ctrl+Shift+Z", false, undoStack.CanRedo())) undoStack.Redo();
        ImGui::Separator();
        ImGui::MenuItem("History", nullptr, &showHistory);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem(kToolbar, nullptr, &showToolbar);
        ImGui::MenuItem(kCanvas, nullptr, &showCanvas2D);
        ImGui::MenuItem(kViewport, nullptr, &showViewport3D);
        ImGui::Separator();
        ImGui::MenuItem(kMaps, nullptr, &showMaps);
        ImGui::MenuItem(kUV, nullptr, &showUV);
        ImGui::MenuItem(kHierarchy, nullptr, &showHierarchy);
        ImGui::MenuItem(kMaterial, nullptr, &showMaterial);
        ImGui::MenuItem(kWorld, nullptr, &showWorld);
        ImGui::MenuItem(kCamera, nullptr, &showCamera);
        ImGui::MenuItem(kModifiers, nullptr, &showModifiers);
        ImGui::MenuItem(kMultires, nullptr, &showMultires);
        ImGui::MenuItem(kRayTracing, nullptr, &showRayTracing);
        ImGui::MenuItem(kArmature, nullptr, &showArmature);
        ImGui::MenuItem(kTimeline, nullptr, &showTimeline);
        ImGui::MenuItem(kTool3D, nullptr, &showTool3D);
        ImGui::MenuItem(kToolOptions, nullptr, &showToolOptions);
        ImGui::MenuItem(kColor, nullptr, &showColor);
        ImGui::MenuItem(kLayers, nullptr, &showLayers);
        ImGui::MenuItem(kRender, nullptr, &showRender);
        ImGui::MenuItem(kRenderSettings, nullptr, &showRenderSettings);
        ImGui::MenuItem("Leorix Scripting", nullptr, &showScripting);
        ImGui::MenuItem("Visual Scripting (blocks)", nullptr, &showVisualScripting);
        ImGui::Separator();
        if (ImGui::MenuItem("Show Both Workspaces", "F1")) { showCanvas2D = showViewport3D = true; resetLayout = true; }
        if (ImGui::MenuItem("Focus 2D Canvas", "F2"))      { showCanvas2D = true; showViewport3D = false; resetLayout = true; }
        if (ImGui::MenuItem("Focus 3D Viewport", "F3"))    { showViewport3D = true; showCanvas2D = false; resetLayout = true; }
        ImGui::Separator();
        if (ImGui::MenuItem("Reset Window Layout")) ApplyWorkspace(workspace);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("3D")) {
        for (Mode3D m : {Mode3D::Object, Mode3D::Sculpt, Mode3D::Edit, Mode3D::Retopology})
            if (ImGui::MenuItem(Mode3DName(m), m == Mode3D::Edit ? "Tab" : nullptr, mode3D == m)) { showViewport3D = true; SetMode3D(m); }
        ImGui::Separator();
        if (ImGui::MenuItem("Add Primitive...", "Shift+A")) showAddPrimitive = true;
        if (ImGui::MenuItem("Add Armature")) AddArmatureObject();
        if (ImGui::MenuItem("Add Camera")) AddCameraAtView();
        if (ImGui::BeginMenu("Add Curve")) {
            if (ImGui::MenuItem("Bezier Curve")) AddCurve(0);
            if (ImGui::MenuItem("Circle")) AddCurve(1);
            if (ImGui::MenuItem("Spiral")) AddCurve(2);
            if (ImGui::MenuItem("Path")) AddCurve(3);
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Join Meshes", "Ctrl+J", false, scene.objects.size() > 1)) JoinMeshes();
        if (ImGui::MenuItem("Load Texture Set...", "Shift+T")) RequestTextureSetImport();
        ImGui::Separator();
        if (ImGui::BeginMenu("Edges (Edit Mode, Edge select)")) {
            bool can = EdgeOpsAvailable();
            if (ImGui::MenuItem("Mark Seam", nullptr, false, can)) MarkEdges(true, true);
            if (ImGui::MenuItem("Clear Seam", nullptr, false, can)) MarkEdges(true, false);
            ImGui::Separator();
            if (ImGui::MenuItem("Mark Sharp", nullptr, false, can)) MarkEdges(false, true);
            if (ImGui::MenuItem("Clear Sharp", nullptr, false, can)) MarkEdges(false, false);
            if (!can) ImGui::TextDisabled("  Select edges in Edit Mode first");
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Unwrap", "U", false, scene.HasMesh())) UnwrapSelected();
        ImGui::Separator();
        if (ImGui::BeginMenu("Snapping")) {
            ImGui::MenuItem("Enabled", "Ctrl while dragging", &snapEnabled);
            ImGui::SetNextItemWidth(160);
            ImGui::Combo("Snap To", &snapMode, [](void*, int i) { return SnapToName((SnapTo)i); }, nullptr, (int)SnapTo::Count);
            ImGui::TextDisabled("Rotate snaps to 15 deg (Shift 5), scale to 0.1");
            ImGui::SetNextItemWidth(160);
            ImGui::SliderFloat("Increment", &snapIncrement, 0.01f, 2.0f, "%.2f m");
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (MeshObject* o = scene.Active()) {
            if (ImGui::MenuItem("Shade Smooth", nullptr, o->shadeSmooth)) o->shadeSmooth = true;
            if (ImGui::MenuItem("Shade Flat", nullptr, !o->shadeSmooth)) o->shadeSmooth = false;
            ImGui::Separator();
            if (ImGui::MenuItem("Flip Normals")) { o->mesh.FlipNormals(); o->MarkTopoDirty(); ShowStatus("Flipped normals"); }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Wireframe", nullptr, shading == ShadingMode::Wireframe)) shading = ShadingMode::Wireframe;
        if (ImGui::MenuItem("Solid", nullptr, shading == ShadingMode::Solid)) shading = ShadingMode::Solid;
        if (ImGui::MenuItem("Material Preview", nullptr, shading == ShadingMode::Material)) shading = ShadingMode::Material;
        if (ImGui::MenuItem("Rendered", nullptr, shading == ShadingMode::Rendered)) shading = ShadingMode::Rendered;
        if (ImGui::MenuItem("Leorix Scripting", nullptr, showScripting)) showScripting = !showScripting;
        if (ImGui::MenuItem("Visual Scripting (blocks)", nullptr, showVisualScripting)) showVisualScripting = !showVisualScripting;
        ImGui::Separator();
        if (ImGui::MenuItem("Import Mesh...")) RequestMeshImport();
        if (ImGui::MenuItem("Export Mesh...", nullptr, false, scene.HasMesh())) RequestMeshExport();
        if (ImGui::MenuItem("Import Sculpt Brush Image...")) RequestBrushImageImport();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Render")) {
        if (ImGui::MenuItem("Switch to 3D Render Workspace")) ApplyWorkspace(Workspace::Render3D);
        if (ImGui::MenuItem("Render Image", "F12")) StartRender();
        ImGui::MenuItem(kRayTracing, nullptr, &showRayTracing);
        ImGui::MenuItem(kRenderSettings, nullptr, &showRenderSettings);
        ImGui::MenuItem(kRender, nullptr, &showRender);
        ImGui::Separator();
        for (const auto& b : backends)
            ImGui::MenuItem((b.name + (b.runtimeFound ? "  (detected)" : "  (not available)")).c_str(), nullptr, false, false);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("About Leorix")) showAbout = true;
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

void App::HandleShortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (showNewProject) return;
    if (ImGui::IsKeyPressed(ImGuiKey_F1)) { showCanvas2D = showViewport3D = true; resetLayout = true; }
    if (ImGui::IsKeyPressed(ImGuiKey_F2)) { showCanvas2D = true; showViewport3D = false; resetLayout = true; }
    if (ImGui::IsKeyPressed(ImGuiKey_F3)) { showViewport3D = true; showCanvas2D = false; resetLayout = true; }
    if (ImGui::IsKeyPressed(ImGuiKey_F12, false)) { StartRender(); return; }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
        if (io.KeyShift) { undoStack.Redo(); ShowStatus("Redo"); }
        else { undoStack.Undo(); ShowStatus(std::string("Undo ") + undoStack.RedoLabel()); }
        return;
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) { undoStack.Redo(); ShowStatus("Redo"); return; }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N, false)) { showNewProject = true; return; }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) { RequestProjectSave(); return; }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) { RequestProjectOpen(); return; }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_I, false)) { RequestMeshImport(); return; }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_E, false) && scene.HasMesh()) { RequestMeshExport(); return; }
    if (!io.WantTextInput && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_A, false) && showViewport3D && viewport.hovered) showAddPrimitive = true;
}

void App::StatusBar() {
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
    if (ImGui::BeginViewportSideBar("##status", ImGui::GetMainViewport(), ImGuiDir_Down, ImGui::GetFrameHeight(), flags)) {
        if (ImGui::BeginMenuBar()) {
            ImGui::TextDisabled("%s", WorkspaceName(workspace)); ImGui::Separator();
            if (workspace == Workspace::Render3D) ImGui::TextDisabled("Output %d x %d", render.width, render.height);
            else ImGui::TextDisabled("%d x %d", canvasW, canvasH);
            ImGui::Separator();
            if (showViewport3D) { ImGui::TextDisabled("%s Mode", Mode3DName(mode3D)); ImGui::Separator(); }
            ImGui::TextDisabled("%s  |  %s", vk.gpu.name.c_str(), vk.gpu.HardwareRayTracing() ? "HW ray tracing" : "no HW ray tracing");
            ImGui::Separator();
            ImGui::TextDisabled("%.0f fps", ImGui::GetIO().Framerate);
            if (xform.active) {
                static const char* names[] = {"MOVE", "ROTATE", "SCALE"};
                const char* ax = xform.axis == 0 ? " X" : xform.axis == 1 ? " Y" : xform.axis == 2 ? " Z" : "";
                ImGui::Separator();
                ImGui::TextColored(kBrass, "%s%s", names[xform.mode], ax);
            }
            if (!statusMessage.empty() && ImGui::GetTime() < statusUntil) { ImGui::Separator(); ImGui::TextColored(kBrass, "%s", statusMessage.c_str()); }
            ImGui::EndMenuBar();
        }
    }
    ImGui::End();
}

// ---------------------------------------------------------------- 2D panels
void App::ToolbarPanel() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 8));
    ImGui::Begin(kToolbar, &showToolbar, ImGuiWindowFlags_NoTitleBar);
    ImGui::PopStyleVar();
    ImGuiIO& io = ImGui::GetIO();
    for (int i = 0; i < kTools2DCount; ++i) {
        const auto& t = kTools2D[i];
        if (!t.name) { ImGui::Separator(); continue; }
        if (t.comicOnly && !comicMode) continue;
        char tip[96];
        if (t.key) std::snprintf(tip, sizeof(tip), "%s (%c)", t.name, t.key); else std::snprintf(tip, sizeof(tip), "%s", t.name);
        ImGui::PushID(i);
        if (IconButton("##t", t.icon, tool2D == i, tip, 38.0f)) tool2D = i;
        ImGui::PopID();
        // 2D shortcuts only when the 3D viewport isn't using the keyboard
        // Only fire when the pointer is actually over the 2D canvas: otherwise keys like G, S and R
        // pressed over the 3D viewport would switch 2D tools instead of running move/scale/rotate.
        if (t.key && !io.WantTextInput && !io.KeyCtrl && !io.KeyAlt && !showNewProject &&
            canvas2DHovered && !viewport.hovered && !xform.active &&
            ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_A + (t.key - 'A')), false))
            tool2D = i;
    }
    ImGui::End();
}

void App::Canvas2DPanel() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    bool visible = ImGui::Begin(kCanvas, &showCanvas2D, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    if (!visible) { ImGui::End(); return; }

    ImVec2 p0 = ImGui::GetCursorScreenPos(), avail = ImGui::GetContentRegionAvail();
    avail.x = std::max(avail.x, 1.0f); avail.y = std::max(avail.y, 1.0f);
    ImGui::InvisibleButton("##canvas", avail);
    ImGuiIO& io = ImGui::GetIO();
    canvas2DHovered = ImGui::IsItemHovered();
    if (canvas2DHovered) {
        const auto& t = kTools2D[tool2D];
        if (t.icon == Icon::Pan) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        else if (t.icon == Icon::Text) ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
        else if (t.icon == Icon::Move2D) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        else if (t.icon == Icon::Eyedropper || t.icon == Icon::Zoom || t.icon == Icon::Fill) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        else {
            // Brush / pencil / eraser / shapes: hide the OS cursor and draw a brush ring, like Leorix.
            ImGui::SetMouseCursor(ImGuiMouseCursor_None);
            bool ring = t.icon == Icon::Brush || t.icon == Icon::Pencil || t.icon == Icon::Eraser || t.icon == Icon::MaskSelect;
            ImDrawList* fg = ImGui::GetForegroundDrawList();
            ImVec2 m = io.MousePos;
            if (ring) {
                // The ring shows the true brush footprint, so it scales with canvas zoom. Zoomed
                // out that can shrink to a few pixels, and the OS cursor is hidden, so keep a
                // readable minimum and add a crosshair whenever the ring is too small to aim with.
                float trueR = brushSize * 0.5f * canvasZoom;
                float r = std::max(trueR, 5.0f);
                fg->AddCircle(m, r, IM_COL32(230, 180, 90, 220), 48, 1.4f);
                if (brushHardness < 99 && (t.icon == Icon::Brush || t.icon == Icon::Eraser) && trueR >= 8.0f)
                    fg->AddCircle(m, r * std::clamp(brushHardness / 100.0f, 0.05f, 1.0f), IM_COL32(230, 180, 90, 90), 48, 1.0f);
                if (trueR < 10.0f) { // crosshair for precise aiming at small sizes
                    fg->AddLine(ImVec2(m.x - r - 6, m.y), ImVec2(m.x - r - 1, m.y), IM_COL32(235, 235, 235, 200), 1.2f);
                    fg->AddLine(ImVec2(m.x + r + 1, m.y), ImVec2(m.x + r + 6, m.y), IM_COL32(235, 235, 235, 200), 1.2f);
                    fg->AddLine(ImVec2(m.x, m.y - r - 6), ImVec2(m.x, m.y - r - 1), IM_COL32(235, 235, 235, 200), 1.2f);
                    fg->AddLine(ImVec2(m.x, m.y + r + 1), ImVec2(m.x, m.y + r + 6), IM_COL32(235, 235, 235, 200), 1.2f);
                }
                fg->AddCircleFilled(m, 1.6f, IM_COL32(255, 255, 255, 210));
            } else { // shape tools: small crosshair
                fg->AddLine(ImVec2(m.x - 7, m.y), ImVec2(m.x + 7, m.y), IM_COL32(230, 230, 230, 220), 1.2f);
                fg->AddLine(ImVec2(m.x, m.y - 7), ImVec2(m.x, m.y + 7), IM_COL32(230, 230, 230, 220), 1.2f);
            }
        }
    }
    if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f && io.KeyCtrl)
        canvasZoom = std::clamp(canvasZoom * (io.MouseWheel > 0 ? 1.1f : 0.9f), 0.02f, 16.0f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p1(p0.x + avail.x, p0.y + avail.y);
    dl->PushClipRect(p0, p1, true);
    const float cell = 22.0f; // checkerboard, Leorix --check-a / --check-b
    dl->AddRectFilled(p0, p1, IM_COL32(38, 42, 52, 255));
    for (float y = p0.y; y < p1.y; y += cell)
        for (float x = p0.x; x < p1.x; x += cell)
            if ((static_cast<int>((x - p0.x) / cell) + static_cast<int>((y - p0.y) / cell)) % 2 == 0)
                dl->AddRectFilled(ImVec2(x, y), ImVec2(x + cell, y + cell), IM_COL32(32, 35, 43, 255));

    ImVec2 c((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
    float hw = canvasW * canvasZoom * 0.5f, hh = canvasH * canvasZoom * 0.5f;
    ImVec2 a(c.x - hw, c.y - hh), b(c.x + hw, c.y + hh);
    dl->AddRectFilled(ImVec2(a.x + 6, a.y + 10), ImVec2(b.x + 6, b.y + 10), IM_COL32(0, 0, 0, 90));

    // The painted map itself. Its composite is rebuilt only where it changed, then uploaded.
    MapDoc* activeMap = paint.Active();
    if (activeMap) {
        SyncMapTexture(*activeMap);
        if (activeMap->texture.Valid()) dl->AddImage(activeMap->texture.Id(), a, b);
    } else if (canvasBackground) {
        dl->AddRectFilled(a, b, canvasBackground);
    }
    dl->AddRect(a, b, IM_COL32(139, 146, 163, 120));
    if (showUV) DrawUVOverlay(a, b);
    // UV editing takes the mouse instead of the brush while it is switched on.
    const bool uvEditing = showUV && uvEditMode && UVCanvasEdit(a, b);

    // Painting: canvas space is the map's pixel space, so zoom does not change the brush size.
    if (activeMap && canvas2DHovered && hw > 0 && hh > 0 && !uvEditing) {
        ImVec2 mouse = io.MousePos;
        ImVec2 mapPos((mouse.x - a.x) / (b.x - a.x) * activeMap->width,
                      (mouse.y - a.y) / (b.y - a.y) * activeMap->height);
        bool inside = mapPos.x >= 0 && mapPos.y >= 0 && mapPos.x < activeMap->width && mapPos.y < activeMap->height;
        if (inside && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { painting = true; PaintStroke(mapPos, true); }
        else if (painting && ImGui::IsMouseDown(ImGuiMouseButton_Left)) PaintStroke(mapPos, false);
    }
    if (painting && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) PaintStrokeEnd();

    // Imported sculpt brush image, shown in the 2D viewport as requested.
    if (brushImage.Loaded() && sculpt.showImageIn2D) {
        float scale = std::min((b.x - a.x) / brushImage.width, (b.y - a.y) / brushImage.height);
        float iw = brushImage.width * scale, ih = brushImage.height * scale;
        ImVec2 ia(c.x - iw * 0.5f, c.y - ih * 0.5f), ib(c.x + iw * 0.5f, c.y + ih * 0.5f);
        dl->AddImage(brushImage.original.Id(), ia, ib);
        char label[256];
        std::snprintf(label, sizeof(label), "Sculpt brush: %s  (%d x %d)", brushImage.name.c_str(), brushImage.width, brushImage.height);
        ImVec2 ts = ImGui::CalcTextSize(label);
        dl->AddRectFilled(ImVec2(p0.x + 8, p0.y + 8), ImVec2(p0.x + ts.x + 20, p0.y + ts.y + 16), IM_COL32(15, 18, 22, 210), 4.0f);
        dl->AddText(ImVec2(p0.x + 14, p0.y + 12), ImGui::GetColorU32(kBrass), label);
    }

    char note[160];
    MapDoc* nm = paint.Active();
    std::snprintf(note, sizeof(note), "%s  |  %s  |  Ctrl+Wheel to zoom",
                  nm ? nm->name.c_str() : "No map",
                  nm && nm->Active() ? nm->Active()->name.c_str() : "No layer");
    dl->AddText(ImVec2(p0.x + 10, p1.y - ImGui::GetTextLineHeight() - 8), IM_COL32(139, 146, 163, 255), note);
    dl->PopClipRect();
    ImGui::End();
}

void App::ToolOptionsPanel() {
    if (ImGui::Begin(kToolOptions, &showToolOptions)) {
        const auto& t = kTools2D[tool2D];
        DrawIcon(ImGui::GetWindowDrawList(), t.icon, ImVec2(ImGui::GetCursorScreenPos().x + 9, ImGui::GetCursorScreenPos().y + 9), 18, ImGui::GetColorU32(kBrass));
        ImGui::Dummy(ImVec2(20, 18)); ImGui::SameLine();
        ImGui::TextColored(kBrass, "%s", t.name ? t.name : "");
        ImGui::SliderFloat("Size", &brushSize, 1, 300, "%.0f px");
        ImGui::SliderFloat("Opacity", &brushOpacity, 1, 100, "%.0f%%");
        ImGui::SliderFloat("Hardness", &brushHardness, 0, 100, "%.0f%%");
    }
    ImGui::End();
}

void App::ColorPanel() {
    if (ImGui::Begin(kColor, &showColor)) {
        ImGui::SetNextItemWidth(-1);
        ImGui::ColorPicker3("##picker", color, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_DisplayHex);
        for (int i = 0; i < IM_COUNTOF(kSwatches); ++i) {
            unsigned s = kSwatches[i];
            ImVec4 col(((s >> 16) & 0xFF) / 255.0f, ((s >> 8) & 0xFF) / 255.0f, (s & 0xFF) / 255.0f, 1);
            if (i % 8) ImGui::SameLine();
            ImGui::PushID(i);
            if (ImGui::ColorButton("##sw", col, ImGuiColorEditFlags_NoTooltip, ImVec2(24, 24))) { color[0] = col.x; color[1] = col.y; color[2] = col.z; }
            ImGui::PopID();
        }
    }
    ImGui::End();
}

void App::LayersPanel() {
    if (!ImGui::Begin(kLayers, &showLayers)) { ImGui::End(); return; }
    MapDoc* m = paint.Active();
    if (!m) { ImGui::TextDisabled("No map. Add one in the Maps panel."); ImGui::End(); return; }

    ImGui::TextColored(kBrass, "%s", m->name.c_str());
    ImGui::TextDisabled("%s", MapKindHint(m->kind));
    ImGui::Separator();

    if (ImGui::Button("New"))
        PushLayerStructureUndo("New Layer", *m, [&] { m->AddLayer("Layer " + std::to_string(m->layers.size() + 1)); });
    ImGui::SameLine();
    if (ImGui::Button("Duplicate")) PushLayerStructureUndo("Duplicate Layer", *m, [&] { m->DuplicateLayer(); });
    ImGui::SameLine();
    ImGui::BeginDisabled(m->layers.size() <= 1);
    if (ImGui::Button("Delete")) PushLayerStructureUndo("Delete Layer", *m, [&] { m->DeleteLayer(); });
    ImGui::EndDisabled();

    if (Layer* l = m->Active()) {
        ImGui::PushItemWidth(-80.0f);
        if (ImGui::SliderFloat("Opacity", &l->opacity, 0.0f, 100.0f, "%.0f%%")) m->dirty.Whole(m->width, m->height);
        const char* blends[(int)Blend::Count];
        for (int i = 0; i < (int)Blend::Count; ++i) blends[i] = BlendName((Blend)i);
        int bi = (int)l->blend;
        if (ImGui::Combo("Blend", &bi, blends, (int)Blend::Count)) { l->blend = (Blend)bi; m->dirty.Whole(m->width, m->height); }
        ImGui::PopItemWidth();
        ImGui::Checkbox("Lock", &l->locked);
    }
    ImGui::Separator();

    // top layer first, like every layer stack
    int moveFrom = -1, moveTo = -1;
    for (int i = (int)m->layers.size() - 1; i >= 0; --i) {
        Layer& l = m->layers[i];
        ImGui::PushID(i);
        if (ImGui::Checkbox("##vis", &l.visible)) m->dirty.Whole(m->width, m->height);
        ImGui::SameLine();
        char label[160];
        std::snprintf(label, sizeof(label), "%s%s##layer", l.name.c_str(), l.locked ? "  (locked)" : "");
        if (ImGui::Selectable(label, m->activeLayer == i)) m->activeLayer = i;
        if (ImGui::BeginPopupContextItem("##lctx")) {
            if (ImGui::MenuItem("Move Up", nullptr, false, i + 1 < (int)m->layers.size())) { moveFrom = i; moveTo = i + 1; }
            if (ImGui::MenuItem("Move Down", nullptr, false, i > 0)) { moveFrom = i; moveTo = i - 1; }
            static char rename[128];
            if (ImGui::IsWindowAppearing()) std::snprintf(rename, sizeof(rename), "%s", l.name.c_str());
            if (ImGui::InputText("Name", rename, sizeof(rename), ImGuiInputTextFlags_EnterReturnsTrue)) {
                l.name = rename; ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%.0f%%  %s", l.opacity, BlendName(l.blend));
        ImGui::PopID();
    }
    if (moveFrom >= 0) PushLayerStructureUndo("Reorder Layer", *m, [&] { m->MoveLayer(moveFrom, moveTo); });
    ImGui::End();
}

void App::RenderPanel() {
    if (!ImGui::Begin(kRender, &showRender)) { ImGui::End(); return; }
    const ImVec4 ok(0.44f, 0.74f, 0.52f, 1), no(0.55f, 0.57f, 0.64f, 1);
    ImGui::TextColored(kBrass, "SELECTED GPU");
    ImGui::TextWrapped("%s", vk.gpu.name.c_str());
    ImGui::TextDisabled("%s  |  %.1f GB VRAM  |  Vulkan %u.%u", vk.gpu.type.c_str(), vk.gpu.vramBytes / (1024.0 * 1024.0 * 1024.0),
                        VK_API_VERSION_MAJOR(vk.gpu.apiVersion), VK_API_VERSION_MINOR(vk.gpu.apiVersion));
    ImGui::Text("Hardware ray tracing:"); ImGui::SameLine();
    ImGui::TextColored(vk.gpu.HardwareRayTracing() ? ok : no, vk.gpu.HardwareRayTracing() ? "Yes" : "No");
    ImGui::Text("Ray query:"); ImGui::SameLine();
    ImGui::TextColored(vk.gpu.rayQuery ? ok : no, vk.gpu.rayQuery ? "Yes" : "No");
    ImGui::Spacing(); ImGui::TextColored(kBrass, "RENDER BACKENDS"); ImGui::Separator();
    for (const auto& b : backends) {
        ImGui::TextColored(b.runtimeFound ? ok : no, b.runtimeFound ? "[ok]" : "[--]");
        ImGui::SameLine(); ImGui::Text("%s", b.name.c_str());
        ImGui::TextWrapped("      %s", b.detail.c_str());
    }
    if (vk.allGpus.size() > 1) {
        ImGui::Spacing(); ImGui::TextColored(kBrass, "ALL GPUS"); ImGui::Separator();
        for (const auto& g : vk.allGpus)
            ImGui::BulletText("%s (%s)%s", g.name.c_str(), g.type.c_str(), g.name == vk.gpu.name ? "  <- in use" : "");
    }
    ImGui::End();
}

void App::AboutWindow() {
    ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Appearing);
    if (ImGui::Begin("About Leorix", &showAbout, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextColored(kBrass, "Leorix Native");
        ImGui::TextWrapped("C++20, SDL3, Dear ImGui (docking), Vulkan via volk, stb_image. "
                           "PBR texture painting, sculpting, retopology, rigging and a ray-traced renderer in one workspace.");
    }
    ImGui::End();
}

// ---------------------------------------------------------------- New Project (Electron Leorix's modal, same options)
void App::NewProjectModal() {
    if (showNewProject && !ImGui::IsPopupOpen("New Project")) ImGui::OpenPopup("New Project");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("New Project", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) return;

    const float cardW = 150.0f, cardH = 118.0f;
    const float contentW = cardW * 4 + ImGui::GetStyle().ItemSpacing.x * 3;
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + contentW);
    ImGui::TextDisabled("Pick how you want to work. You can add maps, layers and effects afterward in any mode.");
    ImGui::Spacing();

    struct Card { Icon icon; const char* name; const char* desc; };
    static const Card cards[] = {
        {Icon::Card2D, "2D Texture Set", "Paint PBR maps flat on a square canvas."},
        {Icon::Card3D, "3D Model Paint", "Load an OBJ or FBX and paint on the mesh."},
        {Icon::CardComic, "Webcomic", "Page presets, panel grids and balloons."},
        {Icon::Card25D, "2.5D Scene", "Depth layers, parallax and relightable 2D art."},
    };
    for (int i = 0; i < 4; ++i) {
        if (i) ImGui::SameLine();
        bool selected = np.mode == i;
        ImGui::PushID(i);
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.79f, 0.54f, 0.29f, 0.12f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.79f, 0.54f, 0.29f, 0.08f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.79f, 0.54f, 0.29f, 0.20f));
        if (ImGui::Selectable("##card", selected, 0, ImVec2(cardW, cardH))) {
            np.mode = i;
            if (i == 2) { np.width = kComicPresets[np.comicPreset].w; np.height = kComicPresets[np.comicPreset].h; np.templateIndex = 5; }
            else if (np.templateIndex == 5 && i != 2) { np.templateIndex = 2; np.width = np.height = 2048; }
        }
        ImGui::PopStyleColor(3);
        ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImU32 accent = ImGui::GetColorU32(kBrass);
        dl->AddRect(mn, mx, selected ? accent : ImGui::GetColorU32(ImGuiCol_Border), 8.0f, 0, selected ? 2.0f : 1.0f);
        DrawIcon(dl, cards[i].icon, ImVec2((mn.x + mx.x) * 0.5f, mn.y + 26), 32, accent);
        ImVec2 ns = ImGui::CalcTextSize(cards[i].name);
        dl->AddText(ImVec2((mn.x + mx.x - ns.x) * 0.5f, mn.y + 50), ImGui::GetColorU32(ImGuiCol_Text), cards[i].name);
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.82f, ImVec2(mn.x + 10, mn.y + 72), ImGui::GetColorU32(ImGuiCol_TextDisabled),
                    cards[i].desc, nullptr, cardW - 20);
        ImGui::PopID();
    }
    ImGui::Spacing();

    // Mode-specific sections, same wording as Electron Leorix
    if (np.mode == 0) {
        ImGui::TextDisabled("Creates a Base Color map. Add Normal, Roughness, Metallic, AO, Height, Emissive or ORM maps afterward from the Maps menu - they'll all share this resolution.");
    } else if (np.mode == 1) {
        static const char* items[] = {
            "3D Design - paint textures on a model",
            "3D Creation - primitives & sculpting",
            "3D Animation - rigging, weights, keyframes",
            "3D Render - lighting, camera & final renders",
        };
        ImGui::AlignTextToFramePadding(); ImGui::Text("Workspace"); ImGui::SameLine(110);
        ImGui::SetNextItemWidth(contentW - 110);
        ImGui::Combo("##ws3d", &np.workspace3D, items, IM_COUNTOF(items));
        static const char* hints[] = {
            "Design loads a model and paints straight onto it. Creation gives you primitives, sculpt brushes, mesh stats and HDRI lighting. Animation adds marker-based rigging, weight painting, cloth and a keyframe timeline.",
            "Creation opens in Sculpt Mode with the full brush set, plus Edit Mode and Retopology for building clean topology.",
            "Animation adds marker-based rigging, weight painting, cloth and a keyframe timeline.",
            "Render opens the 3D viewport with render settings: engine, samples, resolution, camera and output. Your texture maps stay available for materials.",
        };
        ImGui::TextDisabled("%s", hints[np.workspace3D]);
    } else if (np.mode == 2) {
        ImGui::TextDisabled("Sets up a comic page at the size below with bleed and safe-area guides, plus the panel and balloon tools in the sidebar.");
        for (int i = 0; i < 4; ++i) {
            if (i % 2) ImGui::SameLine();
            ImGui::PushID(100 + i);
            bool sel = np.comicPreset == i;
            if (sel) ImGui::PushStyleColor(ImGuiCol_Border, kBrass);
            char label[96]; std::snprintf(label, sizeof(label), "%s\n%s", kComicPresets[i].name, kComicPresets[i].desc);
            if (ImGui::Button(label, ImVec2((contentW - ImGui::GetStyle().ItemSpacing.x) * 0.5f, 44))) {
                np.comicPreset = i; np.width = kComicPresets[i].w; np.height = kComicPresets[i].h; np.templateIndex = 5;
            }
            if (sel) ImGui::PopStyleColor();
            ImGui::PopID();
        }
    } else {
        ImGui::TextDisabled("Each layer gets a depth value, so you can push a flat illustration in space - move the camera for parallax, and relight the art from a Normal map.");
    }
    ImGui::Spacing();

    bool renderOnly = np.mode == 1 && np.workspace3D == 3;
    ImGui::BeginDisabled(renderOnly);
    static const char* templates[] = {"512 x 512", "1024 x 1024 (1K)", "2048 x 2048 (2K)", "4096 x 4096 (4K)", "8192 x 8192 (8K)", "Custom..."};
    static const int templateSizes[] = {512, 1024, 2048, 4096, 8192};
    const float labelW = 110.0f;
    ImGui::AlignTextToFramePadding(); ImGui::Text("Template"); ImGui::SameLine(labelW);
    ImGui::SetNextItemWidth(contentW - labelW);
    if (ImGui::Combo("##tpl", &np.templateIndex, templates, IM_COUNTOF(templates)) && np.templateIndex < 5)
        np.width = np.height = templateSizes[np.templateIndex];
    ImGui::AlignTextToFramePadding(); ImGui::Text("Width (px)"); ImGui::SameLine(labelW);
    ImGui::SetNextItemWidth(contentW - labelW);
    bool dimsChanged = ImGui::InputInt("##w", &np.width, 0);
    ImGui::AlignTextToFramePadding(); ImGui::Text("Height (px)"); ImGui::SameLine(labelW);
    ImGui::SetNextItemWidth(contentW - labelW);
    dimsChanged |= ImGui::InputInt("##h", &np.height, 0);
    np.width = std::clamp(np.width, 1, 16384); np.height = std::clamp(np.height, 1, 16384);
    if (dimsChanged) { // same syncTemplateFromDims() rule as Leorix
        np.templateIndex = 5;
        for (int i = 0; i < 5; ++i) if (np.width == np.height && np.width == templateSizes[i]) np.templateIndex = i;
    }
    static const char* backgrounds[] = {"White", "Transparent", "Black"};
    ImGui::AlignTextToFramePadding(); ImGui::Text("Background"); ImGui::SameLine(labelW);
    ImGui::SetNextItemWidth(contentW - labelW);
    ImGui::Combo("##bg", &np.background, backgrounds, IM_COUNTOF(backgrounds));
    ImGui::EndDisabled();
    if (renderOnly) ImGui::TextDisabled("Render output size is set in Render Settings.");

    ImGui::PopTextWrapPos();
    ImGui::Separator();
    float bw = 100.0f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + contentW - bw * 2 - ImGui::GetStyle().ItemSpacing.x);
    if (ImGui::Button("Cancel", ImVec2(bw, 0))) { showNewProject = false; ImGui::CloseCurrentPopup(); }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.54f, 0.37f, 0.22f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kBrass);
    if (ImGui::Button("Create", ImVec2(bw, 0))) {
        canvasW = np.width; canvasH = np.height;
        {
            uint32_t bg = np.background == 0 ? 0xFFFFFFFFu : np.background == 2 ? 0xFF000000u : 0x00000000u;
            paint.New(np.width, np.height, MapKind::Albedo, bg);
        }
        canvasBackground = np.background == 0 ? IM_COL32(255, 255, 255, 255) : np.background == 2 ? IM_COL32(0, 0, 0, 255) : 0;
        canvasZoom = std::min(0.5f, 700.0f / std::max(canvasW, canvasH));
        static const Workspace ws3d[] = {Workspace::Design3D, Workspace::Creation3D, Workspace::Animation3D, Workspace::Render3D};
        switch (np.mode) {
            case 0: ApplyWorkspace(Workspace::Texture2D); break;
            case 1: ApplyWorkspace(ws3d[np.workspace3D]); break;
            case 2: ApplyWorkspace(Workspace::Webcomic); break;
            default: ApplyWorkspace(Workspace::Scene25D); break;
        }
        showNewProject = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::PopStyleColor(2);
    ImGui::EndPopup();
}

// ---------------------------------------------------------------- Render Settings
void App::RenderSettingsPanel() {
    if (!ImGui::Begin(kRenderSettings, &showRenderSettings)) { ImGui::End(); return; }
    ImGui::PushItemWidth(-120.0f);
    auto Section = [&](const char* t) { ImGui::Spacing(); ImGui::TextColored(kBrass, "%s", t); ImGui::Separator(); };
    auto Wrapped = [](const char* t) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", t); ImGui::PopStyleColor();
    };

    Section("ENGINE");
    render.engine = std::clamp(render.engine, 0, static_cast<int>(backends.size()) - 1);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##engine", backends[render.engine].name.c_str())) {
        for (int i = 0; i < static_cast<int>(backends.size()); ++i) {
            const auto& b = backends[i];
            bool usable = b.runtimeFound && b.id == BackendId::VulkanRT;
            std::string label = b.name + (b.runtimeFound ? (usable ? "" : "  (plugin not built yet)") : "  (not available)");
            if (ImGui::Selectable(label.c_str(), render.engine == i, usable ? 0 : ImGuiSelectableFlags_Disabled)) render.engine = i;
        }
        ImGui::EndCombo();
    }
    Wrapped(backends[render.engine].detail.c_str());

    Section("OUTPUT");
    static const char* presets[] = {"1920 x 1080 (HD)", "2560 x 1440 (QHD)", "3840 x 2160 (4K)", "4096 x 1716 (Cinema 2.39:1)", "Custom"};
    static const int presetSizes[][2] = {{1920, 1080}, {2560, 1440}, {3840, 2160}, {4096, 1716}};
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##res", &render.resolutionPreset, presets, IM_COUNTOF(presets)) && render.resolutionPreset < 4) {
        render.width = presetSizes[render.resolutionPreset][0]; render.height = presetSizes[render.resolutionPreset][1];
    }
    if (render.resolutionPreset == 4) {
        ImGui::InputInt("Width", &render.width, 0); ImGui::InputInt("Height", &render.height, 0);
        render.width = std::clamp(render.width, 16, 16384); render.height = std::clamp(render.height, 16, 16384);
    }
    static const char* formats[] = {"PNG (8-bit)", "OpenEXR (32-bit float)"};
    ImGui::Combo("Format", &render.outputFormat, formats, IM_COUNTOF(formats));
    static const char* transforms[] = {"AgX", "ACES", "Standard"};
    ImGui::Combo("View transform", &render.colorTransform, transforms, IM_COUNTOF(transforms));

    Section("SAMPLING");
    ImGui::SliderInt("Samples", &render.samples, 1, 8192, "%d", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderInt("Max bounces", &render.maxBounces, 1, 32);
    ImGui::Checkbox("Adaptive sampling", &render.adaptiveSampling);
    ImGui::Checkbox("Denoise (Open Image Denoise)", &render.denoise);

    Section("CAMERA");
    ImGui::SliderFloat("Focal length", &render.focalLengthMm, 10.0f, 300.0f, "%.0f mm");
    ImGui::Checkbox("Depth of field", &render.depthOfField);
    ImGui::BeginDisabled(!render.depthOfField);
    ImGui::SliderFloat("f-stop", &render.fStop, 0.95f, 22.0f, "f/%.1f", ImGuiSliderFlags_Logarithmic);
    ImGui::EndDisabled();

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    if (ImGui::Button("Render Image  (F12)", ImVec2(-1, 34))) StartRender();
    Wrapped("Renders on the CPU path tracer. Open the Ray Tracing panel for samples, bounces, "
            "clamping and shadow settings.");
    ImGui::PopItemWidth();
    ImGui::End();
}
