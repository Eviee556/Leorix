#pragma once
// Bezier and NURBS curves: Bezier points carry a left and right handle each (Blender's four
// handle types), NURBS points carry a weight. Both have per-point radius (taper) and an optional
// bevel (round, or another curve's shape) that sweeps the curve into a tube mesh.
#include "mesh/Mesh.h"
#include <algorithm>
#include <cstdint>
#include <functional>
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
    int   bevelObject = -1;                // scene index of a curve used as the cross-section; -1 = round

    // NURBS: the points are control points (no handles), evaluated as a rational B-spline.
    bool  nurbs = false;
    int   order = 4;                       // degree + 1, 2..6 (clamped to the point count)
    bool  endpoint = true;                 // clamp the knots so an open curve reaches its end points
    std::vector<float> weight;             // per point, 1 = plain B-spline
    std::vector<float> knots;              // explicit knots (points + order of them); empty or invalid = generated
    float WeightAt(int i) const { return i < (int)weight.size() ? std::max(weight[i], 1e-4f) : 1.0f; }

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
// the tube never flips or twists; open ends are capped. The cross-section is a round bevel of
// bevelDepth, or the profile curve's X/Z shape when one is given (scaled by the taper radius).
// A curve whose ends meet is swept as a closed loop. Empty when there is nothing to sweep.
Mesh CurveToMesh(const CurvePath& c, const CurvePath* profile = nullptr);
// Box select in curve Edit Mode: the elements (points, and handles for Bezier) inside the box.
std::vector<int> CurveElemsInside(const CurvePath& c, const std::function<bool(const Float3&)>& inside);
// The frame normals used by CurveToMesh, one per sample (exposed for the self-test).
std::vector<Float3> CurveFrameNormals(const std::vector<Float3>& path, bool cyclic);

// An exact NURBS circle of radius r in the X/Z plane: order 3, nine points (the last repeats the
// first), corner weights sqrt(1/2) and doubled interior knots.
void SetNurbsCircle(CurvePath& c, float r);
// E in curve Edit Mode: a new point past the selected end point. Returns its index or -1.
int ExtrudeCurveEnd(CurvePath& c, int point);
// X in curve Edit Mode: removes the points, keeping at least two. Returns how many went.
int DeleteCurvePoints(CurvePath& c, const std::set<int>& pts);
