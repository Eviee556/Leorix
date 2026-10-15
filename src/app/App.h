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
#include "mesh/UVView.h"
#include "tools/BrushImage.h"
#include "tools/Tools3D.h"
#include "tools/TransformOp.h"
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
    ImportKind  queuedKind = ImportKind::None;
    std::string scriptCode, scriptOutput;
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
    void UnwrapSelected();
    bool EdgeOpsAvailable() const;           // edit mode + edge select + something selected
    void PaintStroke(ImVec2 canvasPos, bool begin);
    void SyncMapTexture(MapDoc& m);
    void ApplyMapsToMaterial();
    void HistoryPanel();
    void PushLayerStructureUndo(const std::string& label, MapDoc& m, std::function<void()> apply);
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
    void MeshStatsOverlay(ImVec2 origin, ImVec2 size);
    void EditModePick(bool viewportHovered);
    void HierarchyPanel();
    void ModifierPanel();
    void RefreshModifiers(MeshObject& o);
    void AddCurve(int kind);      // 0 Bezier line, 1 circle, 2 spiral
    void SculptStroke(bool begin);        // deforms multires displacement, or the mesh directly
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
