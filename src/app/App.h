#pragma once
#include "gpu/VulkanContext.h"
#include "render/Backends.h"
#include "app/Icons.h"
#include "render/Material.h"
#include "render/PathTracer.h"
#include "render/HdriPresets.h"
#include "core/Undo.h"
#include "core/Undo.h"
#include "paint/Paint.h"
#include "paint/Project.h"
#include "mesh/Scene.h"
#include "physics/Physics.h"
#include "mesh/UVView.h"
#include "anim/WeightPaint.h"
#include "tools/BrushImage.h"
#include "tools/Tools3D.h"
#include "tools/TransformOp.h"
#include "tools/VisualScript.h"
#include "viewport/MeshView.h"
#include "viewport/Viewport3D.h"
#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct SDL_Window;

// The 2D toolbar entries, shared between the toolbar and the paint code.
struct ToolDef2D { Icon icon; const char* name; char key; bool comicOnly; };
extern const ToolDef2D kTools2D[];
extern const int kTools2DCount;

// Workspaces offered by the New Project dialog: Electron Leorix's startup picker plus 3D Render.
enum class Workspace { Texture2D, Design3D, Creation3D, Animation3D, Render3D, Webcomic, Scene25D };

// New Project dialog state (mirrors Electron Leorix's modal). Only NewProjectModal() reads these.
struct NewProjectDialog {
    int mode = 0;            // 0 2D, 1 3D, 2 Webcomic, 3 2.5D
    int workspace3D = 0;     // Design, Creation, Animation, Render
    int templateIndex = 2;   // 512, 1K, 2K, 4K, 8K, Custom
    int width = 2048, height = 2048;
    int background = 0;      // White, Transparent, Black
    int comicPreset = 0;
};

struct RenderSettings {
    int   engine = 0, resolutionPreset = 0, width = 1920, height = 1080;
    int   samples = 256, maxBounces = 8;
    bool  denoise = true, adaptiveSampling = true;
    float focalLengthMm = 50.0f, fStop = 5.6f;
    bool  depthOfField = false;
    int   outputFormat = 0, colorTransform = 0;
};

// The Leorix application shell. Each Electron feature gets ported into this structure.
class App {
public:
    App(VulkanContext& vk, SDL_Window* window);
    ~App();
    void Frame();
    void QueueBrushImport(const std::string& path); // also used by the --brush command-line option
    void QueueMeshImport(const std::string& path);
    void QueueMeshExport(const std::string& path);
    void QueueTextureFiles(const std::vector<std::string>& files);
    void QueueHdri(const std::string& path) { std::lock_guard<std::mutex> lock(importMutex); pendingHdri = path; }
    void QueueProjectPath(const std::string& path, bool saving);
    void QueueBlocksPath(const std::string& path, bool saving) { std::lock_guard<std::mutex> lock(importMutex); (saving ? pendingBlocksSave : pendingBlocksOpen) = path; }
    void SetSunElevation(float deg) { world.sunElevation = deg; }
    void SetExposure(float e) { world.exposure = e; }
    // Headless render: trace, write the PNG, then quit. Also handy for batch jobs.
    // The render starts from Frame() so any queued HDRI is loaded first.
    void RenderToFileAndQuit(const std::string& path) { batchRenderPath = path; }
    std::string batchRenderPath;
    void ApplyHdriPreset(int preset);
    void SetRenderSamples(int n) { rt.samples = std::clamp(n, 1, 4096); }
    void ShowUVEditor() { showUV = true; RebuildUVLayout(); }
    void SetShading(int index) { shading = (ShadingMode)std::clamp(index, 0, 3); shadingChosen = true; }
    void OpenDemo(const std::string& meshPath, int mode); // load a mesh + workspace for screenshots
    bool quitRequested = false;

private:
    VulkanContext& vk;
    SDL_Window* window;
    std::vector<BackendStatus> backends;
    Viewport3D viewport;
    Scene scene;
    MeshDrawPrefs meshPrefs;
    float wireColor[3] = {0.27f, 1.0f, 0.51f};   // neon green, used by Wireframe shading
    ShadingMode   shading = ShadingMode::Solid;
    bool          shadingChosen = false;   // don't auto-switch once the user picked a mode

    // World / Environment, the settings Rendered and Material Preview light with.
    struct World {
        int   background = 1;               // 0 flat colour, 1 sky gradient, 2 world colour only
        float color[3] = {0.16f, 0.18f, 0.22f};
        float horizon[3] = {0.07f, 0.08f, 0.10f};
        float strength = 0.45f;
        float sunElevation = 48.0f, sunAzimuth = 35.0f;   // degrees
        float sunColor[3] = {1.0f, 0.96f, 0.90f};
        float sunStrength = 1.2f;
        float exposure = 1.0f;
        bool  filmTransparent = false;
        bool  studioLightInSolid = true;
    } world;
    bool showWorld = false, showMaterial = false, showCamera = false;
    bool showArmature = false, showTimeline = false, showModifiers = false, showRayTracing = false;
    bool showPhysics = false;
    PhysicsSettings physics;
    void PhysicsPanel();
    PathTracer tracer;
    HdriImage  hdri;
    std::string hdriPath;
    float hdriRotation = 0.0f, hdriStrength = 1.0f;
    bool  hdriAsBackground = true;
    int   hdriPreset = -1;                       // which built-in is active, -1 for a loaded file
    GpuTexture hdriThumbs[(int)HdriPreset::Count];
    bool  hdriThumbsBuilt = false;
    // GPU copy of the active HDRI for the viewport background; kept separate from the thumbnails.
    GpuTexture hdriBgTexture;
    bool       hdriBgDirty = true;   // set whenever the HDRI data or preset changes
    void       SyncHdriBackground();
    void  BuildHdriThumbnails();
    RayTraceSettings rt;
    GpuTexture renderResult;
    bool renderResultDirty = false;
    int  activeBone = -1;
    PlaybackState playback;
    double lastPlayTime = 0.0;
    bool cameraView = false;      // Numpad 0: look through the scene camera
    int  selectedCamera = -1;     // which camera G / R act on, -1 for none
    bool CameraSelected() const { return selectedCamera >= 0; }
    SceneCamera& SelCam() { return scene.cameras[std::clamp(selectedCamera, 0, (int)scene.cameras.size() - 1)]; }
    void AddCameraAtView();
    bool cameraDirty = false;     // a panel edit that must be pushed to the viewport
    void DrawCameraGizmo();
    ViewLighting BuildLighting() const;
    PickResult lastPick;
    int        lastPickObject = -1;     // scene index lastPick belongs to

    TransformOp xform;
    bool  sculpting = false;
    bool  openEdgeMenu = false;   // Ctrl+E in Edit Mode; opened and drawn at the top level
    bool  showMultires = false;
    Float3 lastSculptPoint{0, 0, 0};
    bool  snapEnabled = false;
    int   snapMode = 0;           // 0 increment, 1 vertex, 2 grid
    float snapIncrement = 0.25f;

    // ---- panel visibility
    bool showToolbar = true, showCanvas2D = true, showViewport3D = true;
    bool showToolOptions = true, showColor = true, showLayers = true, showRender = true;
    bool showRenderSettings = false, showTool3D = false;
    bool showVisualScripting = false;
    bool showScripting = false, showAbout = false, resetLayout = false, showNewProject = true;
    bool showHierarchy = true;
    bool canvas2DHovered = false;   // set by Canvas2DPanel; 2D tool shortcuts require it
    bool showAddPrimitive = false;

    // ---- project
    Workspace workspace = Workspace::Texture2D;
    int canvasW = 2048, canvasH = 2048;
    unsigned canvasBackground = 0xFFFFFFFF; // ABGR for ImDrawList; 0 = transparent
    bool comicMode = false;

    NewProjectDialog np;

    // ---- 2D tools
    PaintProject paint;
    bool  painting = false;
    ImVec2 lastPaintPos{0, 0};
    float  strokeCarry = 0.0f;
    BrushSettings brush2D;
    UndoStack undoStack;
    std::shared_ptr<StrokeUndo> activeStroke;   // filled while the mouse is down
    bool showHistory = false;
    bool  showMaps = true, showUV = false;
    // UV editor, mirroring Leorix's uv-panel options
    UVLayout uvLayout;
    int   uvSourceObject = -1;         // which object the layout came from
    bool  uvShowWire = true, uvShowSeams = true;
    float uvOpacity = 55.0f, uvSeamWidth = 2.0f;
    float uvWireColor[3] = {0.55f, 0.60f, 0.70f};
    float uvSeamColor[3] = {0.79f, 0.35f, 0.31f};   // #c9584e, the original's default
    // UV editing on the 2D canvas: one selection flag per face corner of the active object's mesh
    bool  uvEditMode = false, uvSelectIslands = true;
    std::vector<uint8_t> uvSel;
    std::vector<Float2>  uvGestureBase;     // loopUVs when the current move/rotate/scale began
    bool  uvDragging = false; Float2 uvDragStart{};
    float uvRotateDeg = 0.0f, uvScale = 1.0f;   // panel drag values for the gesture in progress
    float smartAngle = 66.0f;
    // painting on the 3D model (Sculpt Mode, Paint brush) into the active map layer
    bool  paintingModel = false; double modelPaintRefreshAt = 0;
    const MeshObject* uvSelOwner = nullptr;   // the object uvSel was made on
    int   tool2D = 6;
    float brushSize = 28, brushOpacity = 100, brushHardness = 70;
    float color[3] = {201 / 255.0f, 138 / 255.0f, 75 / 255.0f};
    float canvasZoom = 0.25f;

    // ---- 3D tools
    Mode3D         mode3D = Mode3D::Object;
    int            objectTool = 0;
    SculptSettings sculpt;
    EditSettings   edit;
    RetopoSettings retopo;
    BrushImage     brushImage;
    RenderSettings render;

    // File dialog results arrive on SDL's dialog thread; handed to the UI thread here.
    // import kinds: brush image vs mesh, tracked so the shared file-dialog callback routes correctly
    enum class ImportKind { None, BrushImage, Mesh, TextureSet, Hdri };
    ImportKind  pendingImportKind = ImportKind::None;
    std::mutex  importMutex;
    std::string pendingImportPath, pendingImportError, pendingExportPath;
    std::vector<std::string> pendingTextureFiles;
    std::string pendingProjectSave, pendingProjectOpen, projectPath, pendingHdri;
    std::string pendingBlocksSave, pendingBlocksOpen, blocksPath;
    ImportKind  queuedKind = ImportKind::None;
    std::string scriptCode, scriptOutput;
    bool scriptLive = false; float scriptLastFrame = -1; MeshObject* scriptLiveObj = nullptr;   // "Update every frame"
    // Visual Scripting panel state (graph + canvas pan + the wire being dragged).
    VisualGraph visualGraph; bool visualStarted = false;
    ImVec2 visualPan{0, 0}; int visualDragFrom = -1;
    std::string visualMessage; bool visualMessageOk = true; std::vector<std::string> visualNotes;
    // shortcut: live objects are matched by pointer, so a deleted object whose memory is reused could be updated; store an object id if that ever matters.
    bool visualLive = false; float visualLastFrame = -1; MeshObject* visualLiveObj = nullptr;   // "Update every frame"
    bool LiveObjectExists(const MeshObject* o) const { for (auto& p : scene.objects) if (p.get() == o) return true; return false; }
    std::string statusMessage; double statusUntil = 0;

    // ---- App.cpp
    void MenuBar();
    void StatusBar();
    void BuildDefaultLayout(unsigned dockspaceId);
    void ApplyWorkspace(Workspace ws);
    void HandleShortcuts();
    void ToolbarPanel();
    void Canvas2DPanel();
    void MapsPanel();
    void RequestProjectSave();
    void RequestProjectOpen();
    void UVPanel();
    void DrawUVOverlay(ImVec2 a, ImVec2 b);   // over the 2D canvas
    void RebuildUVLayout();
    void BakeUVToLayer(bool seamsOnly);
    void MarkEdges(bool seam, bool set);     // seam/sharp, mark or clear, on the edge selection
    void UnwrapSelected(bool smart = false); // smart: Smart UV Project instead of seam-based Unwrap
    bool UVCanvasEdit(ImVec2 a, ImVec2 b);   // UV editing input on the canvas; true when it used the mouse
    void BeginUVGesture();
    void EndUVGesture(const char* label);  // pushes one undo step for the move/rotate/scale
    void PaintOnModel(bool begin);           // viewport brush -> UV -> active layer
    void PaintStrokeEnd();                   // shared by the canvas and the viewport
    void RefreshPaintedSlots(int mapIndex);  // re-copies a painted map into material slots that use it
    void RunEditOp(char op);                 // Edit Mode: 'E' extrude, 'X' delete, 'M' merge at center, 'D' duplicate faces
    void SeparateSelected();                 // Edit Mode P: move the selected faces into a new object
    bool EdgeOpsAvailable() const;           // edit mode + edge select + something selected
    // sizePx > 0: a stroke from the 3D viewport, always a round brush of that size in map pixels
    void PaintStroke(ImVec2 canvasPos, bool begin, float sizePx = 0.0f);
    void SyncMapTexture(MapDoc& m);
    void ApplyMapsToMaterial();
    void HistoryPanel();
    void PushLayerStructureUndo(const std::string& label, MapDoc& m, std::function<void()> apply);
    // Undo step for some state of the active object: apply(object, value) puts before/after back.
    template <class T, class F>
    void PushObjectUndo(const std::string& label, T before, T after, F apply, size_t bytes = 0);
    // onlySlot >= 0 when the caller knows which slot `change` touches, so only that one is copied.
    void PushMaterialTextureUndo(const std::string& label, std::function<void()> change, int onlySlot = -1);
    // Undo for an object entering (removed == null: obj was just added at index) or leaving the scene.
    void PushObjectListUndo(const std::string& label, MeshObject* obj, int index, std::unique_ptr<MeshObject> removed = nullptr);
    void ToolOptionsPanel();
    void ColorPanel();
    void LayersPanel();
    void RenderPanel();
    void RenderSettingsPanel();
    void NewProjectModal();
    void AboutWindow();
    void ShowStatus(const std::string& msg);

    // ---- App3D.cpp
    void ViewportHeader3D();
    void ViewportShelf(ImVec2 origin, ImVec2 size);
    void Tool3DPanel();
    void SculptSettingsUI();
    void EditSettingsUI();
    void RetopoSettingsUI();
    void ObjectSettingsUI();
    void HandleShortcuts3D(bool viewportHovered);
    void SetMode3D(Mode3D m);
    void RequestBrushImageImport();
    void RequestMeshImport();
    void RequestMeshExport();
    void ProcessPendingImport();
    void AddPrimitiveMenu();
    void ScriptingPanel();
    void VisualScriptingPanel();
    void MeshStatsOverlay(ImVec2 origin, ImVec2 size);
    void EditModePick(bool viewportHovered);
    void HierarchyPanel();
    void ModifierPanel();
    void RefreshModifiers(MeshObject& o);
    void AddCurve(int kind);      // 0 Bezier line, 1 circle, 2 spiral, 3 path
    // curve Edit Mode (CurveEdit.cpp)
    void RebuildCurveMesh(MeshObject& o);              // Sync handles, rebuild the bevel tube
    void PushCurveUndo(const std::string& label, MeshObject* o, const CurvePath& beforeCurve, const Mesh& beforeMesh, bool beforeIsCurve);
    void CurveEditOverlay(ImDrawList* dl, MeshObject& c);   // points, handles, selection
    void CurveEditClick();                             // click / Shift+click picks a point or handle
    void CurveEditOp(char op);                         // 'E' extrude end, 'X' delete points
    void CurveSettingsUI();                            // handle types, cyclic, bevel, taper, Convert to Mesh
    void SculptStroke(bool begin);        // deforms multires displacement, or the mesh directly
    void SculptStrokeEnd();               // pushes the stroke as one undo step
    std::vector<Float3> sculptBefore;     // positions or displacement at stroke start
    int   sculptUndoIndex = -1;
    bool  sculptUndoMulti = false;
    // Weight Paint mode: brushes the active bone's weights on the active object (WeightPaintPanel.cpp)
    struct WeightPaintSettings {
        SculptSettings brush;            // radius, strength, hardness and falloff curve
        int   mode = 0;                  // WeightBrush: Add, Subtract, Replace, Smooth
        float value = 1.0f;
        bool  mirrorX = true;
        WeightPaintSettings() { brush.radiusPx = 50.0f; brush.strength = 0.5f; }
    } weightPaint;
    bool  weightPainting = false;
    int   weightUndoIndex = -1;
    std::vector<VertexWeights> weightBefore;
    void WeightStroke(bool begin);
    void WeightStrokeEnd();
    void WeightSettingsUI();
    void PushWeightsUndo(const std::string& label, int objIndex, std::vector<VertexWeights> before);
    void MultiresPanel();
    void JoinMeshes();
    void DuplicateSelected();     // Shift+D: copy the selection and start moving it
    // LMB drag box select
    bool   boxSelecting = false;
    ImVec2 boxStart{0, 0};
    bool   boxArmed = false;      // mouse is down on empty space; becomes a box once it moves
    void   FinishBoxSelect(ImVec2 a, ImVec2 b, bool extend, bool subtract);            // Ctrl+J: merge selected objects into the active one
    void AddArmatureObject();     // Shift+A > Armature
    void WorldPanel();
    void CameraPanel();
    void RayTracingPanel();
    void RequestHdriImport();
    void StartRender();
    void ArmaturePanel();
    void TimelinePanel();
    void ApplyAnimation(float frame);     // evaluate every object's action at this frame
    void UpdateSkinning(MeshObject& o);
    void InsertKeyframes(bool location, bool rotation, bool scale);
    void BakeAnimation();
    void HandleViewShortcuts(bool viewportHovered);
    void SyncViewportToCamera();
    void AlignCameraToView();
    void MaterialPanel();
    void RequestTextureSetImport();
    void ApplyTextureFiles(const std::vector<std::string>& files);
    void BeginTransform(int mode);
    void UpdateTransform(bool viewportHovered);
    void CancelTransform();
    const char* Mode3DName(Mode3D m) const;
};
