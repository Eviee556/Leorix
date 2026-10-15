#pragma once
#include "imgui.h"
#include <cmath>
#include <functional>
#include <string>
struct Float3;

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(float a, float b, float c) : x(a), y(b), z(c) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
};
inline float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 Cross(const Vec3& a, const Vec3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline Vec3 Normalize(const Vec3& a) { float l = std::sqrt(Dot(a, a)); return l > 0 ? a * (1.0f / l) : a; }

// Colors match Leorix's Prefs3D defaults so the native viewport looks like the Electron one.
struct ViewportPrefs {
    ImU32 background = IM_COL32(15, 18, 22, 255);    // #0f1216
    ImU32 gridFine   = IM_COL32(58, 65, 80, 255);    // #3a4150
    ImU32 gridMajor  = IM_COL32(86, 95, 112, 255);   // #565f70
    ImU32 axisX      = IM_COL32(201, 88, 78, 255);   // #c9584e
    ImU32 axisZ      = IM_COL32(94, 168, 107, 255);  // #5ea86b
    ImU32 gizmoPosX  = IM_COL32(201, 88, 78, 255),  gizmoNegX = IM_COL32(122, 64, 56, 255);
    ImU32 gizmoPosY  = IM_COL32(110, 178, 90, 255),  gizmoNegY = IM_COL32(64, 98, 56, 255);   // Y green
    ImU32 gizmoPosZ  = IM_COL32(86, 130, 214, 255),  gizmoNegZ = IM_COL32(52, 76, 120, 255);  // Z blue
};

class Viewport3D {
public:
    // header: extra widgets on the header row (mode menu, select modes...)
    // overlay: drawn over the viewport area (tool shelf). Returns true if the viewport is hovered.
    // depthPass runs before the grid so meshes can fill the depth buffer first.
    bool Draw(const char* title, bool* open, const char* gpuName,
              const std::function<void()>& header, const std::function<void(ImVec2, ImVec2)>& overlay,
              const std::function<void(ImVec2, ImVec2)>& depthPass = nullptr);
    void ResetCamera();
    ViewportPrefs prefs;
    bool showGrid = true;
    bool hovered = false;
    const char* modeLabel = "Object";
    bool  shadingPicked = false;     // set when the user clicks a shading button
    int   shadingIndex = 1;          // 0 wireframe, 1 solid, 2 material, 3 rendered
    bool  worldBackground = false;   // Rendered/Material draw the world instead of the flat backdrop
    ImU32 worldTop = IM_COL32(40, 46, 56, 255), worldBottom = IM_COL32(18, 20, 24, 255);
    // When an HDRI is loaded, its equirectangular texture is projected onto the background
    // instead of the flat gradient above - reprojected every frame so it pans as you orbit.
    ImTextureID hdriTexture = 0;
    float hdriRotationRad = 0.0f;
    float hdriExposure = 1.0f;      // simple tonemap so a bright sun does not just clip to white
    float hudLeftInset = 10.0f;
    float envRotateDelta = 0.0f;   // degrees, from Shift + right-drag (environment rotation)
    bool  suppressGrid = false;
    // navigation state (see Draw): kept here instead of relying on ImGui's active item
    enum NavMode { NavNone, NavOrbit, NavPan, NavZoom, NavEnv, NavGizmo };
    NavMode navMode = NavNone;
    NavMode prevNavMode = NavNone;
    int     navButton = 0;
    ImVec2  navStart{0, 0};
    bool    navigating = false;      // true while a drag is steering the view
    bool    overNavWidget = false;   // pointer is on the gizmo or the pan/zoom handles
    bool    gizmoSnap = false;
    ImVec2  panHandle{0, 0}, zoomHandle{0, 0};
    void    DrawNavHandles(ImDrawList* dl);
    void    DrawHdriBackground(ImDrawList* dl, ImVec2 origin, ImVec2 size);    // Rendered mode hides the grid without clearing showGrid
    bool  depthTestLines = true;   // grid and axes hide behind solid geometry
    bool ProjectPoint(const Vec3& p, float& z, ImVec2& out) const { return Project(p, z, out); }
    bool ProjectPoint(const Float3& p, float& z, ImVec2& out) const;
    Vec3 CameraRight() const { return right; }
    Vec3 CameraUp() const { return up; }
    Vec3 CameraForward() const { return forward; }
    Vec3 CameraEye() const { return eye; }
    // World units per screen pixel at a given depth, for 1:1 mouse-follow dragging.
    float WorldPerPixel(float depth) const {
        if (size.y <= 0) return 0.01f;
        return orthographic ? (2.0f * orthoScale) / size.y : (2.0f * tanHalf * depth) / size.y;
    }
    ImVec2 ViewportOrigin() const { return origin; }
    ImVec2 ViewportSize() const { return size; }
    // Camera exposed so the mesh layer shares exactly the same projection.
    float& Yaw() { return yaw; } float& Pitch() { return pitch; } float& Dist() { return distance; }
    Vec3& Target() { return target; }
    Vec3 Eye() const { return eye; } Vec3 Right() const { return right; } Vec3 Up() const { return up; } Vec3 Forward() const { return forward; }
    ImVec2 Origin() const { return origin; } ImVec2 Size() const { return size; }
    // sceneDraw: draw meshes into the viewport (called after grid, before HUD).
    std::function<void(ImDrawList*, ImVec2, ImVec2)> sceneDraw;

private:
    // Orbit camera. Mouse mapping follows Leorix/Blender: left = select (reserved),
    // middle = orbit, Shift+middle or right = pan, wheel = zoom.
    Vec3  target{0, 0.5f, 0};
    float yaw = 0.7f, pitch = 0.42f, distance = 7.0f, fovY = 45.0f;
public:
    bool  orthographic = false;
    float orthoScale = 3.0f;          // half-height of the view in world units
    const char* viewLabel = "User";
    // Blender's Auto Perspective: axis views go orthographic, and orbiting away returns to
    // perspective. Panning and zooming keep whatever the view is.
    bool autoOrtho = true;
    bool autoOrthoActive = false;       // true when the ortho came from an axis snap
    std::string ViewTitle() const {
        return std::string(viewLabel) + (orthographic ? " Orthographic" : " Perspective");
    }
    // Blender's numpad views.
    void SetAxisView(int which, bool opposite);   // 1 front, 3 right, 7 top
    void ToggleOrtho();
    void OrbitSteps(float yawDeg, float pitchDeg);
    void FrameBounds(const Vec3& center, float radius);
    float Distance() const { return distance; }
    float Yaw() const { return yaw; }
    float Pitch() const { return pitch; }
    Vec3  Target() const { return target; }
    void  SetFov(float degrees) { fovY = std::clamp(degrees, 5.0f, 150.0f); }
    void  SetTarget(float x, float y, float z) { target = Vec3{x, y, z}; }
    void  SetDistance(float d) { distance = std::max(0.05f, d); }
    void  SetOrbit(float yawRad, float pitchRad) { yaw = yawRad; pitch = pitchRad; }
private:

    // Per-frame projection state
    Vec3 eye, right, up, forward;
    ImVec2 origin, size;
    float tanHalf = 1, aspect = 1;

    void UpdateBasis();
    bool Project(const Vec3& p, float& z, ImVec2& out) const;
    void DrawLine3D(ImDrawList* dl, Vec3 a, Vec3 b, ImU32 col, float thickness) const;
    void DrawGrid(ImDrawList* dl);
    void DrawGizmo(ImDrawList* dl, bool clicked);
};
