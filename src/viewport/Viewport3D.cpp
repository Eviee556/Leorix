#include "viewport/Viewport3D.h"
#include "mesh/Mesh.h"
#include "viewport/MeshView.h"
#include <algorithm>
#include <vector>
#include <algorithm>
#include <array>
#include "imgui_internal.h"
#include <cstdio>

static const float kNear = 0.05f;

void Viewport3D::ResetCamera() {
    target = {0, 0.5f, 0};
    yaw = 0.7f; pitch = 0.42f; distance = 7.0f;
}

void Viewport3D::UpdateBasis() {
    Vec3 dir{std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw)};
    eye = target + dir * distance;
    forward = Normalize(target - eye);
    right = Normalize(Cross(forward, Vec3{0, 1, 0}));
    up = Cross(right, forward);
    tanHalf = std::tan(fovY * 0.5f * 3.14159265f / 180.0f);
    aspect = size.y > 0 ? size.x / size.y : 1.0f;
}

// World point -> screen. Returns false if behind the near plane.
bool Viewport3D::Project(const Vec3& p, float& z, ImVec2& out) const {
    Vec3 d = p - eye;
    z = Dot(d, forward);
    if (z < kNear) return false;
    float x, y;
    if (orthographic) {
        // Parallel projection: screen position no longer depends on depth, but z is still the
        // distance along the view axis so the depth buffer and picking keep working.
        float halfH = std::max(0.01f, orthoScale);
        x = Dot(d, right) / (halfH * aspect);
        y = Dot(d, up) / halfH;
    } else {
        x = Dot(d, right) / (z * tanHalf * aspect);
        y = Dot(d, up) / (z * tanHalf);
    }
    out = ImVec2(origin.x + (x * 0.5f + 0.5f) * size.x, origin.y + (0.5f - y * 0.5f) * size.y);
    return true;
}

void Viewport3D::DrawLine3D(ImDrawList* dl, Vec3 a, Vec3 b, ImU32 col, float thickness) const {
    // Clip slightly in front of the near plane: clipping exactly onto it lets rounding push
    // the new endpoint behind it, which made Project() reject every line crossing the camera.
    const float clipZ = kNear * 2.0f;
    float za = Dot(a - eye, forward), zb = Dot(b - eye, forward);
    if (za < clipZ && zb < clipZ) return;
    if (za < clipZ) a = a + (b - a) * ((clipZ - za) / (zb - za));
    else if (zb < clipZ) b = b + (a - b) * ((clipZ - zb) / (za - zb));
    float za2, zb2; ImVec2 sa, sb;
    if (!Project(a, za2, sa) || !Project(b, zb2, sb)) return;

    // Depth-test against the mesh depth buffer so the grid and axes stop showing through solid
    // objects. The line is split into short pieces and each piece is tested at its midpoint.
    const ViewportDepth& zbuf = SharedViewportDepth();
    if (!depthTestLines || zbuf.depth.empty()) { dl->AddLine(sa, sb, col, thickness); return; }

    float len = std::sqrt((sb.x - sa.x) * (sb.x - sa.x) + (sb.y - sa.y) * (sb.y - sa.y));
    int steps = std::clamp((int)(len / 12.0f), 1, 96);
    for (int i = 0; i < steps; ++i) {
        float t0 = i / (float)steps, t1 = (i + 1) / (float)steps;
        ImVec2 p0(sa.x + (sb.x - sa.x) * t0, sa.y + (sb.y - sa.y) * t0);
        ImVec2 p1(sa.x + (sb.x - sa.x) * t1, sa.y + (sb.y - sa.y) * t1);
        ImVec2 mid((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
        float zm = za2 + (zb2 - za2) * (t0 + t1) * 0.5f;
        if (zbuf.Occluded(mid, zm, 0.03f)) continue;
        dl->AddLine(p0, p1, col, thickness);
    }
}

static ImU32 WithAlpha(ImU32 c, float a) {
    int alpha = static_cast<int>(((c >> IM_COL32_A_SHIFT) & 0xFF) * std::clamp(a, 0.0f, 1.0f));
    return (c & ~IM_COL32_A_MASK) | (static_cast<ImU32>(alpha) << IM_COL32_A_SHIFT);
}

void Viewport3D::DrawGrid(ImDrawList* dl) {
    const int half = 50; // 100 x 100 unit floor, like Leorix's grid
    // Fine 1-unit lines fade out as the camera pulls back, instead of aliasing into noise.
    float fineAlpha = std::clamp(1.0f - (distance - 10.0f) / 35.0f, 0.0f, 1.0f) * 0.55f;
    for (int i = -half; i <= half; ++i) {
        if (i == 0) continue; // axes drawn separately
        bool major = (i % 10) == 0;
        if (!major && fineAlpha <= 0.01f) continue;
        ImU32 col = major ? WithAlpha(prefs.gridMajor, 0.85f) : WithAlpha(prefs.gridFine, fineAlpha);
        float f = static_cast<float>(i), h = static_cast<float>(half);
        DrawLine3D(dl, {f, 0, -h}, {f, 0, h}, col, 1.0f);
        DrawLine3D(dl, {-h, 0, f}, {h, 0, f}, col, 1.0f);
    }
    float h = static_cast<float>(half);
    DrawLine3D(dl, {-h, 0, 0}, {h, 0, 0}, prefs.axisX, 1.6f); // X axis (red)
    DrawLine3D(dl, {0, 0, -h}, {0, 0, h}, prefs.axisZ, 1.6f); // Z axis (green)
}

void Viewport3D::DrawGizmo(ImDrawList* dl, bool clicked) {
    struct Ball { Vec3 axis; ImU32 color; const char* label; float depth; ImVec2 pos; };
    const float radius = 32.0f, ball = 9.0f;
    ImVec2 center(origin.x + size.x - 48.0f, origin.y + size.y - 48.0f);
    std::array<Ball, 6> balls = {{
        // Blender's layout on Leorix's Y-up storage: user Z is internal +Y, user Y is internal -Z.
        {{ 1, 0, 0}, prefs.gizmoPosX, "X"}, {{-1, 0, 0}, prefs.gizmoNegX, nullptr},
        {{ 0, 0,-1}, prefs.gizmoPosY, "Y"}, {{ 0, 0, 1}, prefs.gizmoNegY, nullptr},
        {{ 0, 1, 0}, prefs.gizmoPosZ, "Z"}, {{ 0,-1, 0}, prefs.gizmoNegZ, nullptr},
    }};
    for (auto& b : balls) {
        b.depth = Dot(b.axis, forward); // positive = pointing away from the viewer
        b.pos = ImVec2(center.x + Dot(b.axis, right) * radius, center.y - Dot(b.axis, up) * radius);
    }
    std::sort(balls.begin(), balls.end(), [](const Ball& a, const Ball& b) { return a.depth > b.depth; });

    dl->AddCircleFilled(center, radius + ball + 4.0f, IM_COL32(255, 255, 255, 14));
    for (const auto& b : balls) {
        if (b.label) dl->AddLine(center, b.pos, b.color, 2.0f);
        dl->AddCircleFilled(b.pos, ball, b.color);
        if (b.label) dl->AddText(ImVec2(b.pos.x - 3.5f, b.pos.y - 7.0f), IM_COL32(20, 23, 28, 255), b.label);
    }

    // Click a ball to snap the view down that axis (front-most ball wins).
    if (clicked) {
        ImVec2 m = ImGui::GetIO().MousePos;
        for (auto it = balls.rbegin(); it != balls.rend(); ++it) {
            float dx = m.x - it->pos.x, dy = m.y - it->pos.y;
            if (dx * dx + dy * dy <= ball * ball) {
                pitch = std::clamp(std::asin(it->axis.y), -1.55f, 1.55f);
                if (std::fabs(it->axis.y) < 0.5f) yaw = std::atan2(it->axis.x, it->axis.z);
                if (it->axis.y > 0.5f) viewLabel = "Top";
                else if (it->axis.y < -0.5f) viewLabel = "Bottom";
                else if (it->axis.x > 0.5f) viewLabel = "Right";
                else if (it->axis.x < -0.5f) viewLabel = "Left";
                else if (it->axis.z > 0.5f) viewLabel = "Front";
                else viewLabel = "Back";
                if (autoOrtho && !orthographic) { ToggleOrtho(); autoOrthoActive = true; }
                break;
            }
        }
    }
}

bool Viewport3D::Draw(const char* title, bool* open, const char* gpuName,
                      const std::function<void()>& header, const std::function<void(ImVec2, ImVec2)>& overlay,
                      const std::function<void(ImVec2, ImVec2)>& depthPass) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    bool visible = ImGui::Begin(title, open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    if (!visible) { ImGui::End(); hovered = false; return false; }

    // ---- header bar
    ImGui::SetCursorPos(ImVec2(8, ImGui::GetCursorPosY() + 4));
    if (header) { header(); ImGui::SameLine(); }
    // Blender's four shading modes.
    {
        struct Item { const char* label; const char* tip; };
        static const Item items[] = {
            {"Wire", "Wireframe: edges only"},
            {"Solid", "Solid: flat studio-lit preview"},
            {"Material", "Material Preview: base colour textures, lit by the Environment settings"},
            {"Rendered", "Rendered: environment lighting, overlays hidden (path tracer still to come)"},
        };
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 0));
        for (int i = 0; i < 4; ++i) {
            bool on = shadingIndex == i;
            if (on) { ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.54f, 0.37f, 0.22f, 1)); ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.98f, 0.9f, 0.8f, 1)); }
            if (ImGui::Button(items[i].label)) { shadingIndex = i; shadingPicked = true; }
            if (on) ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", items[i].tip);
            ImGui::SameLine();
        }
        ImGui::PopStyleVar();
    }
    if (ImGui::Button("Reset View")) ResetCamera();
    ImGui::SameLine();
    ImGui::Checkbox("Grid", &showGrid);

    // ---- viewport area
    origin = ImGui::GetCursorScreenPos();
    size = ImGui::GetContentRegionAvail();
    size.x = std::max(size.x, 1.0f); size.y = std::max(size.y, 1.0f);
    ImGui::InvisibleButton("##viewport", size,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    hovered = ImGui::IsItemHovered();
    bool leftClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    ImGuiIO& io = ImGui::GetIO();
    envRotateDelta = 0.0f;

    // Navigation keeps its own drag state rather than relying on this button staying ImGui's
    // active item: any overlay, popup or child window that grabbed the active item would
    // otherwise silently stop orbit and pan.
    ImVec2 m = io.MousePos;
    bool inViewport = m.x >= origin.x && m.y >= origin.y && m.x < origin.x + size.x && m.y < origin.y + size.y;
    ImVec2 gizmoCenter(origin.x + size.x - 48.0f, origin.y + size.y - 48.0f);
    // side by side above the gizmo, clear of the HUD text in the bottom-left
    panHandle  = ImVec2(gizmoCenter.x - 18.0f, gizmoCenter.y - 68.0f);
    zoomHandle = ImVec2(gizmoCenter.x + 18.0f, gizmoCenter.y - 68.0f);
    auto near = [&](ImVec2 c, float r) { float dx = m.x - c.x, dy = m.y - c.y; return dx * dx + dy * dy <= r * r; };
    overNavWidget = inViewport && (near(gizmoCenter, 44.0f) || near(panHandle, 15.0f) || near(zoomHandle, 15.0f));

    if (navMode == NavNone && hovered) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
            navMode = io.KeyShift ? NavPan : NavOrbit, navButton = ImGuiMouseButton_Middle;
        else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            navMode = io.KeyShift ? NavEnv : NavPan, navButton = ImGuiMouseButton_Right;
        else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            // Blender's "emulate 3 button mouse": Alt+LMB orbits, Alt+Shift pans, Alt+Ctrl zooms
            if (io.KeyAlt) navMode = io.KeyShift ? NavPan : io.KeyCtrl ? NavZoom : NavOrbit;
            else if (near(panHandle, 15.0f)) navMode = NavPan;
            else if (near(zoomHandle, 15.0f)) navMode = NavZoom;
            else if (near(gizmoCenter, 44.0f)) navMode = NavGizmo;
            if (navMode != NavNone) { navButton = ImGuiMouseButton_Left; navStart = m; }
        }
    }
    if (navMode != NavNone && !ImGui::IsMouseDown(navButton)) {
        // a click on the gizmo that never moved is a snap to that axis, not an orbit
        if (navMode == NavGizmo && std::fabs(m.x - navStart.x) + std::fabs(m.y - navStart.y) < 4.0f) gizmoSnap = true;
        navMode = NavNone;
    }
    navigating = navMode != NavNone;
    const bool dragJustStarted = navMode != NavNone && prevNavMode == NavNone;
    prevNavMode = navMode;
    const float dx = dragJustStarted ? 0.0f : io.MouseDelta.x;
    const float dy = dragJustStarted ? 0.0f : io.MouseDelta.y;
    if (navMode == NavEnv) {
        envRotateDelta = dx * 0.35f;      // degrees per pixel of horizontal drag
    } else if (navMode == NavPan) {
        float k = orthographic ? orthoScale * 0.0045f : distance * 0.0016f;
        target = target - right * (dx * k) + up * (dy * k);
    } else if (navMode == NavZoom) {
        float f = std::pow(0.992f, -dy);
        distance = std::clamp(distance * f, 0.3f, 500.0f);
        orthoScale = std::clamp(orthoScale * f, 0.05f, 500.0f);
    } else if (navMode == NavOrbit || navMode == NavGizmo) {
        yaw -= dx * 0.006f;
        pitch = std::clamp(pitch + dy * 0.006f, -1.55f, 1.55f);
        if ((dx != 0.0f || dy != 0.0f) && std::string(viewLabel) != "User" && std::string(viewLabel) != "Camera") {
            viewLabel = "User";
            if (autoOrthoActive && orthographic) { ToggleOrtho(); }
            autoOrthoActive = false;
        }
    }
    (void)leftClicked;
    if (hovered && io.MouseWheel != 0.0f) {
        float f = std::pow(0.88f, io.MouseWheel);
        distance = std::clamp(distance * f, 0.3f, 500.0f);
        orthoScale = std::clamp(orthoScale * f, 0.05f, 500.0f);
    }
    if (hovered && ImGui::IsKeyPressed(ImGuiKey_Home)) ResetCamera();

    UpdateBasis();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
    if (worldBackground && hdriTexture) {
        DrawHdriBackground(dl, origin, size);
    } else if (worldBackground) {
        dl->AddRectFilledMultiColor(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                                    worldTop, worldTop, worldBottom, worldBottom);
    } else {
        dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), prefs.background);
    }
    // depth pre-pass: meshes fill the depth buffer, then the grid tests against it
    SharedViewportDepth().Begin(origin, size);
    if (depthPass) depthPass(origin, size);
    if (showGrid && !suppressGrid) DrawGrid(dl);
    if (sceneDraw) sceneDraw(dl, origin, size);
    DrawGizmo(dl, gizmoSnap);
    gizmoSnap = false;
    DrawNavHandles(dl);

    // HUD, same spot as Leorix's mesh-stats overlay
    char hud[256];
    std::snprintf(hud, sizeof(hud), "%s\n%s Mode  |  Vulkan  |  %s\nMiddle-drag orbit  |  Right-drag pan  |  Wheel zoom",
                  ViewTitle().c_str(), modeLabel, gpuName ? gpuName : "GPU");
    ImVec2 ts = ImGui::CalcTextSize(hud);
    ImVec2 hp(origin.x + hudLeftInset, origin.y + size.y - ts.y - 18);
    dl->AddRectFilled(ImVec2(hp.x - 6, hp.y - 5), ImVec2(hp.x + ts.x + 8, hp.y + ts.y + 5), IM_COL32(15, 18, 22, 185), 5.0f);
    dl->AddText(hp, IM_COL32(232, 227, 216, 255), hud);
    dl->PopClipRect();

    if (overlay) overlay(origin, size);
    ImGui::End();
    return hovered;
}

bool Viewport3D::ProjectPoint(const Float3& p, float& z, ImVec2& out) const { return Project(Vec3{p.x, p.y, p.z}, z, out); }

// ---------------------------------------------------------------- Blender-style view shortcuts
void Viewport3D::SetAxisView(int which, bool opposite) {
    const float kPi = 3.14159265f;
    if (autoOrtho && !orthographic) { ToggleOrtho(); autoOrthoActive = true; }
    switch (which) {
        case 1: yaw = opposite ? kPi : 0.0f;        pitch = 0.0f;                   // front / back
                viewLabel = opposite ? "Back" : "Front"; break;
        case 3: yaw = opposite ? -kPi * 0.5f : kPi * 0.5f; pitch = 0.0f;            // right / left
                viewLabel = opposite ? "Left" : "Right"; break;
        case 7: pitch = opposite ? -kPi * 0.4999f : kPi * 0.4999f; yaw = 0.0f;      // top / bottom
                viewLabel = opposite ? "Bottom" : "Top"; break;
        default: break;
    }
}
void Viewport3D::ToggleOrtho() {
    orthographic = !orthographic;
    autoOrthoActive = false;
    // keep the framing roughly the same when switching
    if (orthographic) orthoScale = distance * tanHalf;
    else distance = orthoScale / std::max(0.05f, tanHalf);
}
void Viewport3D::OrbitSteps(float yawDeg, float pitchDeg) {
    const float k = 3.14159265f / 180.0f;
    yaw += yawDeg * k;
    pitch = std::clamp(pitch + pitchDeg * k, -1.55f, 1.55f);
    viewLabel = "User";
    if (autoOrthoActive && orthographic) ToggleOrtho();
    autoOrthoActive = false;
}
void Viewport3D::FrameBounds(const Vec3& center, float radius) {
    target = center;
    float r = std::max(0.2f, radius);
    distance = r / std::max(0.05f, tanHalf) * 1.25f;
    orthoScale = r * 1.3f;
}

// Blender's navigation buttons beside the gizmo: drag the hand to pan, the magnifier to zoom,
// and the gizmo itself to orbit. They work with any pointer, including trackpads and pens.
// The background as the loaded HDRI, reprojected each frame from the current camera so it pans
// correctly as you orbit - not just a static image behind the model. Drawn as a coarse grid of
// textured quads, since each screen pixel's view ray points somewhere different on the sphere
// and ImGui's draw list has no shader stage to compute that per-pixel.
void Viewport3D::DrawHdriBackground(ImDrawList* dl, ImVec2 origin, ImVec2 size) {
    const int cols = 32, rows = 20;
    static std::vector<ImVec2> screenPos((cols + 1) * (rows + 1));
    static std::vector<ImVec2> uvPos((cols + 1) * (rows + 1));

    auto dirToUv = [&](float dx, float dy, float dz) {
        float u = std::atan2(dz, dx) + hdriRotationRad;
        while (u < 0) u += 6.2831853f;
        while (u > 6.2831853f) u -= 6.2831853f;
        u /= 6.2831853f;
        float v = std::acos(std::clamp(dy, -1.0f, 1.0f)) / 3.14159265f;
        return ImVec2(u, v);
    };

    for (int j = 0; j <= rows; ++j) {
        float ny = 1.0f - 2.0f * j / rows;          // top to bottom
        for (int i = 0; i <= cols; ++i) {
            float nx = 2.0f * i / cols - 1.0f;       // left to right
            Vec3 dir = orthographic
                ? forward
                : Normalize(forward + right * (nx * tanHalf * aspect) + up * (ny * tanHalf));
            int idx = j * (cols + 1) + i;
            screenPos[idx] = ImVec2(origin.x + (i / (float)cols) * size.x, origin.y + (j / (float)rows) * size.y);
            uvPos[idx] = dirToUv(dir.x, dir.y, dir.z);
        }
    }

    dl->PushTextureID(hdriTexture);
    ImU32 tint = IM_COL32(255, 255, 255, 255);
    for (int j = 0; j < rows; ++j) {
        for (int i = 0; i < cols; ++i) {
            int a = j * (cols + 1) + i, b = a + 1, c = a + cols + 2, d = a + cols + 1;
            // a quad whose U wraps around the back of the sphere would smear across the whole
            // texture; split it at the seam instead of drawing the wrap-around triangle.
            float us[4] = {uvPos[a].x, uvPos[b].x, uvPos[c].x, uvPos[d].x};
            float umin = *std::min_element(us, us + 4), umax = *std::max_element(us, us + 4);
            if (umax - umin > 0.5f) continue;
            dl->AddImageQuad(hdriTexture, screenPos[a], screenPos[b], screenPos[c], screenPos[d],
                             uvPos[a], uvPos[b], uvPos[c], uvPos[d], tint);
        }
    }
    dl->PopTextureID();
}

void Viewport3D::DrawNavHandles(ImDrawList* dl) {
    auto handle = [&](ImVec2 c, bool active, int kind) {
        ImVec2 m = ImGui::GetIO().MousePos;
        float ddx = m.x - c.x, ddy = m.y - c.y;
        bool over = ddx * ddx + ddy * ddy <= 15.0f * 15.0f;
        dl->AddCircleFilled(c, 14.0f, active ? IM_COL32(180, 130, 70, 230) : over ? IM_COL32(255, 255, 255, 40) : IM_COL32(255, 255, 255, 18));
        ImU32 ink = IM_COL32(210, 214, 222, 235);
        if (kind == 0) {               // hand: a palm and four fingers
            dl->AddRectFilled(ImVec2(c.x - 5, c.y - 1), ImVec2(c.x + 5, c.y + 7), ink, 2.0f);
            for (int f = 0; f < 4; ++f) dl->AddRectFilled(ImVec2(c.x - 5 + f * 2.7f, c.y - 7), ImVec2(c.x - 3.6f + f * 2.7f, c.y), ink, 1.0f);
        } else {                       // magnifier
            dl->AddCircle(ImVec2(c.x - 2, c.y - 2), 5.0f, ink, 16, 1.8f);
            dl->AddLine(ImVec2(c.x + 1.5f, c.y + 1.5f), ImVec2(c.x + 6, c.y + 6), ink, 2.2f);
        }
    };
    handle(panHandle, navMode == NavPan, 0);
    handle(zoomHandle, navMode == NavZoom, 1);
}
