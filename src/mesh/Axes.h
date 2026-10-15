#pragma once
// Leorix stores geometry Y-up (like glTF, FBX and OBJ as they are exported), but everything the
// user sees follows Blender: X left/right, Y front/back, Z up/down.
//   user X = internal X     user Y = -internal Z     user Z = internal Y
// Converting at the surface keeps importers, primitives, the renderer and skinning untouched.
#include "mesh/Mesh.h"

inline Float3 ToUser(const Float3& v)   { return {v.x, -v.z, v.y}; }
inline Float3 FromUser(const Float3& u) { return {u.x, u.z, -u.y}; }

// Scales have no sign to flip, only an axis swap.
inline Float3 ScaleToUser(const Float3& s)   { return {s.x, s.z, s.y}; }
inline Float3 ScaleFromUser(const Float3& s) { return {s.x, s.z, s.y}; }

// Rotations about each user axis, expressed on the internal axes.
inline Float3 RotToUser(const Float3& r)   { return {r.x, -r.z, r.y}; }
inline Float3 RotFromUser(const Float3& u) { return {u.x, u.z, -u.y}; }

// A user axis (0 X, 1 Y, 2 Z) as the internal axis it lies along.
inline int InternalAxis(int userAxis) { return userAxis == 1 ? 2 : userAxis == 2 ? 1 : 0; }
inline int UserAxis(int internalAxis) { return internalAxis == 2 ? 1 : internalAxis == 1 ? 2 : 0; }
inline const char* UserAxisName(int userAxis) { return userAxis == 0 ? "X" : userAxis == 1 ? "Y" : "Z"; }
