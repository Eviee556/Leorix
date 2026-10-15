#pragma once
// CPU projection renderer + interaction for the editable mesh. Draws shaded solid + wireframe
// with ImDrawList, and does edit-mode picking with colored highlights. Big meshes stay responsive
// because the filled pass is capped and falls back to points/wireframe; all vertices are still
// stored and editable (ZBrush-scale storage, level-of-detail display).
#include "imgui.h"
#include "mesh/Mesh.h"
#include "tools/Tools3D.h"
#include <cstdint>
#include <unordered_set>
#include <vector>

struct Camera3D {
    Vec3 target{0, 0, 0};
    float yaw = 0.7f, pitch = 0.42f, distance = 6.0f, fovY = 45.0f;
    Vec3 eye, right, up, forward;
    void Update(float aspect);
    void Frame(const AABB& b);
};

struct PickResult {
    bool hit = false;
    int vertex = -1, edge = -1, face = -1, tri = -1;
    Vec3 point;   // world-space hit point on the surface
    float t = 0;
};

class MeshView {
public:
    Mesh mesh;
    bool hasMesh = false;
    bool wireframe = false, showStats = true;

    // edit-mode selection
    std::unordered_set<uint32_t> selVerts, selEdges, selFaces;

    void SetMesh(Mesh&& m);
    void ClearSelection() { selVerts.clear(); selEdges.clear(); selFaces.clear(); }

    // Draw into the given viewport rect. Returns nothing; picking is separate.
    void Draw(ImDrawList* dl, const Camera3D& cam, ImVec2 origin, ImVec2 size,
              Mode3D mode, int editSelectMask, bool xray, const ViewportPrefs& prefs);

    // Ray pick from a screen point. Fills vertex/edge/face nearest the ray under the given mask.
    PickResult Pick(const Camera3D& cam, ImVec2 origin, ImVec2 size, ImVec2 mouse, int editSelectMask) const;

    // Sculpt: displace vertices under a world-space brush. Returns number of verts touched.
    int SculptStroke(const Vec3& center, float radius, float strength, const Vec3& normal,
                     const Vec3& grabDelta, int brushKind, bool invert);

    // stats
    size_t drawnTris = 0; bool decimated = false;

private:
    // screen projection cache for the current frame
    mutable std::vector<ImVec2> proj;
    mutable std::vector<float>  depth;
    mutable std::vector<uint8_t> visible;
    void Project(const Camera3D& cam, ImVec2 origin, ImVec2 size) const;
    bool RayTriangle(const Vec3& ro, const Vec3& rd, uint32_t a, uint32_t b, uint32_t c, float& t, Vec3& hit) const;
};
