#pragma once
// Software-projected mesh drawing into the ImGui viewport, plus vertex/edge/face picking.
// This is the Milestone-2 renderer: it draws real geometry, shaded, with edit-mode overlays
// and the colored selection highlights. A GPU rasterizer replaces the fill pass later, but the
// projection/pick math here is what edit and sculpt mode rely on and stays.
#include "mesh/Scene.h"
#include "viewport/Viewport3D.h"
#include "imgui.h"
#include <vector>

// Blender's viewport shading modes.
enum class ShadingMode { Wireframe, Solid, Material, Rendered };

// Lighting the viewport shades with. Solid uses a fixed studio light; Material and Rendered use
// the world and sun from the Environment panel, so those settings visibly change the viewport.
struct ViewLighting {
    Float3 sunDir{0.4f, 0.75f, 0.5f};
    float  sunColor[3] = {1.0f, 0.96f, 0.9f};
    float  sunStrength = 1.0f;
    float  worldColor[3] = {0.16f, 0.18f, 0.22f};
    float  worldStrength = 0.4f;
    float  exposure = 1.0f;
    bool   studio = true;      // Solid mode: light follows the camera
};

struct MeshDrawPrefs {
    bool  xray = false;             // show overlays through the surface
    bool  wireframe = true;         // quad wireframe overlay in edit/retopo
    bool  shaded = true;
    ImU32 faceColor = IM_COL32(150, 150, 160, 255);
    ImU32 wireColor = IM_COL32(20, 23, 28, 150);
    // Selection highlight colors, exactly as requested
    ImU32 vertexColor    = IM_COL32(255, 255, 255, 255);
    ImU32 vertexSelected = IM_COL32(170, 90, 220, 255);   // purple selected vertex
    ImU32 edgeHover      = IM_COL32(240, 150, 40, 255);   // orange edge under cursor
    ImU32 edgeSelected   = IM_COL32(240, 150, 40, 255);
    // Wireframe mode: a neon trace over a darkened surface
    // Face Orientation overlay, Blender's colours: blue where the normal points at you, red
    // where it points away, which is what a flipped face looks like from outside.
    bool  faceOrientation = false;
    ImU32 faceFront      = IM_COL32(45, 110, 255, 165);
    ImU32 faceBack       = IM_COL32(255, 40, 40, 205);
    ImU32 wireNeon   = IM_COL32(70, 255, 130, 255);
    float wireGlow   = 1.0f;          // 0 turns the halo off, leaving flat lines
    ImU32 seamColor      = IM_COL32(210, 80, 60, 255);    // marked UV seams, Blender red
    ImU32 sharpColor     = IM_COL32(70, 210, 230, 255);   // marked sharp edges, Blender cyan
    ImU32 faceHover      = IM_COL32(235, 220, 60, 90);    // yellow face under cursor
    ImU32 faceSelected   = IM_COL32(235, 220, 60, 70);
};

// A coarse CPU depth buffer for the viewport. ImDrawList has no depth test, so without this the
// grid, axes and edit-mode overlays draw straight through solid geometry.
struct ViewportDepth {
    int   w = 0, h = 0;
    float scale = 0.5f;           // buffer resolution relative to the viewport
    ImVec2 origin{0, 0};
    std::vector<float> depth;

    void Begin(ImVec2 vpOrigin, ImVec2 vpSize);
    void Clear();
    float At(ImVec2 screen) const;
    // true when something solid is nearer to the camera than z at this screen position
    bool Occluded(ImVec2 screen, float z, float bias = 0.02f) const;
    void WriteTriangle(ImVec2 a, ImVec2 b, ImVec2 c, float za, float zb, float zc);
};
ViewportDepth& SharedViewportDepth();

// Fills the depth buffer with one object. Called before the grid so the grid can be occluded.
void RasterizeDepth(const Viewport3D& vp, MeshObject& obj);

// Draws an object's bones in the viewport.
void DrawArmature(ImDrawList* dl, const Viewport3D& vp, MeshObject& obj, int activeBone);

// Rendered mode: the object's shadow cast onto the ground plane, drawn before the meshes.
void DrawGroundShadow(ImDrawList* dl, const Viewport3D& vp, MeshObject& obj, const ViewLighting& light);

// Draws the mesh and, in edit mode, the vertex dots / edge lines / face fills.
// Returns the element currently under the cursor so edit mode can select on click.
PickResult DrawAndPickMesh(ImDrawList* dl, const Viewport3D& vp, MeshObject& obj,
                           int selectMode /*1 v,2 e,4 f*/, bool editMode, bool retopoMode,
                           const MeshDrawPrefs& prefs, ImVec2 mouse, bool mouseInside,
                           ShadingMode shading = ShadingMode::Solid,
                           const ViewLighting& light = ViewLighting{});
