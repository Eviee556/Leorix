#pragma once
// Bezier curves: control points with a left and right handle each, Blender's four handle types,
// per-point radius (taper) and an optional round bevel that sweeps the curve into a tube mesh.
#include "mesh/Mesh.h"
#include <cstdint>
#include <set>
#include <vector>

enum class HandleType : uint8_t { Auto, Vector, Aligned, Free };
const char* HandleTypeName(HandleType t);

struct CurvePath {
    std::vector<Float3> points;
    // Handles are stored in the same space as the points. Auto and Vector handles are recomputed
    // from the neighbours; Aligned and Free keep what the user dragged. Missing or mismatched
    // arrays (old files, presets) count as Auto, so a plain list of points is still a valid curve.
    std::vector<Float3> handleL, handleR;
    std::vector<HandleType> handleType;
    std::vector<float> radius;             // taper per point, 1 = full bevel depth
    bool  cyclic = false;
    int   resolution = 12;                 // samples per span
    float bevelDepth = 0.0f;               // tube radius; 0 = no geometry, just the path
    int   bevelResolution = 2;             // tube sides = 4 + 2 * resolution, like Blender

    // Elements for editing: 3*i is point i, 3*i+1 its left handle, 3*i+2 its right handle.
    int ElemCount() const { return (int)points.size() * 3; }
    Float3 Elem(int e) const;
    void SetElem(int e, Float3 p);         // a moved handle turns Auto->Aligned, Vector->Free
    void Sync();                           // size the arrays, recompute Auto/Vector, re-align Aligned
    void HandlesFor(int i, Float3& l, Float3& r) const;   // effective handles, valid even unsynced
    float RadiusAt(int i) const { return i < (int)radius.size() ? radius[i] : 1.0f; }

    std::vector<Float3> Evaluate(std::vector<float>* radii = nullptr) const;
};

// Tube sweep along the evaluated curve, with rotation-minimising (parallel transport) frames so
// the tube never flips or twists; open ends are capped. Empty when bevelDepth <= 0.
Mesh CurveToMesh(const CurvePath& c);
// The frame normals used by CurveToMesh, one per sample (exposed for the self-test).
std::vector<Float3> CurveFrameNormals(const std::vector<Float3>& path, bool cyclic);

// E in curve Edit Mode: a new point past the selected end point. Returns its index or -1.
int ExtrudeCurveEnd(CurvePath& c, int point);
// X in curve Edit Mode: removes the points, keeping at least two. Returns how many went.
int DeleteCurvePoints(CurvePath& c, const std::set<int>& pts);
