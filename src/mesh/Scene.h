#pragma once
// Holds the mesh being worked on plus edit-mode selection. ZBrush-scale friendly:
// selection is index sets, adjacency is built lazily and freed when topology changes.
#include "mesh/Mesh.h"
#include "render/Material.h"
#include "render/Camera.h"
#include "anim/Armature.h"
#include "mesh/Modifier.h"
#include "mesh/Multires.h"
#include "anim/Animation.h"
#include "physics/RigidBody.h"
#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// A transform in the scene. Kept separate from the mesh so Object mode moves the object,
// while Edit mode moves vertices inside it - the same split Blender uses.
struct Transform {
    Float3 position{0, 0, 0};
    Float3 rotation{0, 0, 0};   // euler XYZ, radians
    Float3 scale{1, 1, 1};
    Float3 Apply(const Float3& v) const;
    // Undoes Apply: world/parent space back into this object's local space. Used by Join, which
    // has to express every incoming vertex in the target object's space.
    Float3 InverseApply(const Float3& v) const;
};

struct MeshObject {
    std::string name = "Mesh";
    Transform   transform;
    bool        visible = true;
    // Blender's primitives are flat shaded until you pick Shade Smooth; a smooth-shaded cube
    // shows a diagonal gradient across every quad, which reads as if it were triangulated.
    bool        shadeSmooth = false;
    bool        selected = false;
    bool        active = false;    // the one Edit mode operates on
    Mesh mesh;
    Material material;
    bool      isCurve = false;         // a curve object: control points, no faces
    CurvePath curve;
    Multires multires;                 // sculpted detail, kept off the base mesh
    std::vector<Modifier> modifiers;   // evaluated top to bottom, never touching `mesh`
    Mesh     displayMesh;              // the stack's result; drawn instead of `mesh` when present
    bool     hasDisplay = false;
    bool     stackDirty = true;
    Armature armature;          // empty until bones are added
    Action   action;            // this object's keyframes
    RigidBody rigidBody;        // physics settings and baked cache
    std::vector<Float3> skinned;// deformed positions for the current pose
    // Edit-mode selection
    std::unordered_set<uint32_t> selVerts;
    std::unordered_set<uint64_t> selEdges;
    std::unordered_set<uint32_t> selFaces;   // polygon ids

    // Lazy topology
    std::vector<uint64_t>                       uniqueEdges;      // every triangle edge
    // Edges that bound a polygon. A quad is split into two triangles for drawing, and the shared
    // diagonal is not a real edge - Blender never shows it, and it must not be selectable either.
    std::vector<uint64_t>                       polyEdges;
    std::unordered_set<uint64_t>                diagonals;
    std::unordered_map<uint32_t, std::vector<uint32_t>> vertToTris;
    bool topoDirty = true;

    void BuildTopology();
    void MarkTopoDirty() { topoDirty = true; }
};

// The scene graph the Hierarchy panel shows: many objects, one of them active.
struct Scene {
    std::vector<std::unique_ptr<MeshObject>> objects;
    int activeIndex = -1;
    // Every camera in the scene. One is active: it is what F12 renders and Numpad 0 looks through.
    std::vector<SceneCamera> cameras{SceneCamera{}};
    std::vector<std::string> cameraNames{"Camera"};
    int activeCamera = 0;
    SceneCamera& Camera() { activeCamera = std::clamp(activeCamera, 0, (int)cameras.size() - 1); return cameras[activeCamera]; }
    const SceneCamera& Camera() const { return cameras[std::clamp(activeCamera, 0, (int)cameras.size() - 1)]; }
    int AddCamera(const SceneCamera& c) {
        cameras.push_back(c);
        char buf[32]; std::snprintf(buf, sizeof(buf), "Camera.%03d", (int)cameras.size() - 1);
        cameraNames.push_back(buf);
        return (int)cameras.size() - 1;
    }

    MeshObject* Active() {
        return (activeIndex >= 0 && activeIndex < (int)objects.size()) ? objects[activeIndex].get() : nullptr;
    }
    const MeshObject* Active() const {
        return (activeIndex >= 0 && activeIndex < (int)objects.size()) ? objects[activeIndex].get() : nullptr;
    }
    bool HasMesh() const { const MeshObject* o = Active(); return o && !o->mesh.positions.empty(); }

    // Adds a new object and makes it active/selected (Blender adds rather than replaces).
    MeshObject* AddMesh(Mesh m, const std::string& name) {
        auto obj = std::make_unique<MeshObject>();
        obj->name = UniqueName(name);
        obj->mesh = std::move(m);
        // Imports can arrive with mixed or inverted winding; fix it here so nothing downstream
        // has to ask the user to "recalculate normals".
        if (!obj->mesh.tris.empty()) obj->mesh.HarmonizeNormals();
        else if (obj->mesh.normals.size() != obj->mesh.positions.size()) obj->mesh.ComputeNormals();
        obj->mesh.EnsureUVs();
        obj->MarkTopoDirty();
        objects.push_back(std::move(obj));
        activeIndex = (int)objects.size() - 1;
        SelectOnly(activeIndex);
        return objects.back().get();
    }
    // Kept so existing callers that replace the whole scene still work.
    void SetMesh(Mesh m) { objects.clear(); activeIndex = -1; AddMesh(std::move(m), "Mesh"); }

    void SelectOnly(int index) {
        for (int i = 0; i < (int)objects.size(); ++i) { objects[i]->selected = (i == index); objects[i]->active = (i == index); }
        activeIndex = index;
    }
    void SetActive(int index) {
        for (int i = 0; i < (int)objects.size(); ++i) objects[i]->active = (i == index);
        activeIndex = index;
    }
    void Remove(int index) { Take(index); }
    // Takes an object out of the scene and hands it over, so an undo step can put back that exact object.
    std::unique_ptr<MeshObject> Take(int index) {
        if (index < 0 || index >= (int)objects.size()) return nullptr;
        std::unique_ptr<MeshObject> obj = std::move(objects[index]);
        objects.erase(objects.begin() + index);
        if (objects.empty()) activeIndex = -1;
        else activeIndex = std::min(index, (int)objects.size() - 1);
        if (activeIndex >= 0) SelectOnly(activeIndex);
        return obj;
    }
    void Insert(int index, std::unique_ptr<MeshObject> obj) {
        if (!obj) return;
        index = std::clamp(index, 0, (int)objects.size());
        objects.insert(objects.begin() + index, std::move(obj));
        SelectOnly(index);
    }
    int IndexOf(const MeshObject* obj) const {
        for (int i = 0; i < (int)objects.size(); ++i) if (objects[i].get() == obj) return i;
        return -1;
    }
    std::string UniqueName(const std::string& base) const {
        bool taken = false;
        for (const auto& o : objects) if (o->name == base) taken = true;
        if (!taken) return base;
        for (int n = 1; n < 10000; ++n) {
            std::string candidate = base + "." + (n < 100 ? std::string(3 - std::to_string(n).size(), '0') : "") + std::to_string(n);
            bool used = false;
            for (const auto& o : objects) if (o->name == candidate) used = true;
            if (!used) return candidate;
        }
        return base;
    }
    void Clear() { objects.clear(); activeIndex = -1; }
};
