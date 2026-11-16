#pragma once
// Snapping for the G/R/S transforms: increment steps for rotate and scale, and world-space
// geometry targets (vertex, edge, edge center, face) for move.
#include "mesh/Scene.h"

// Matches the order of the Snapping menu's "Snap To" combo; 0-2 keep their old meaning.
enum class SnapTo { Increment = 0, Vertex, Grid, Edge, EdgeCenter, Face, Count };
const char* SnapToName(SnapTo s);

// Rounds an angle (radians) to 15 degree steps, or 5 degree steps when `fine`.
float SnapAngle(float radians, bool fine);
// Rounds a scale factor to `step` (0.1 by default), never below one step.
float SnapScale(float factor, float step = 0.1f);

Float3 ClosestPointOnSegment(const Float3& p, const Float3& a, const Float3& b);
Float3 ClosestPointOnTriangle(const Float3& p, const Float3& a, const Float3& b, const Float3& c);

// The nearest Vertex / Edge / EdgeCenter / Face target to `want` on every visible object except
// `skipObject`, in world space. Returns false when the mode is not a geometry mode or nothing
// is in the scene.
bool SnapToGeometry(const Scene& scene, int skipObject, SnapTo mode, const Float3& want, Float3& out);
