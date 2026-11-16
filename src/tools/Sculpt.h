#pragma once
// Sculpt brush engine: pure geometry, no UI, so it runs headless in --selftest.
#include "mesh/Mesh.h"
#include "tools/Tools3D.h"
#include <vector>

struct BrushImage;

// One dab, in the object's local space.
struct SculptDab {
    Float3 center;            // surface point under the cursor
    Float3 drag{0, 0, 0};     // cursor movement since the last dab (transform brushes)
    float  radius = 0;
    float  strength = 0;      // signed: negative subtracts
    bool   smooth = false;    // Shift held, or the Smooth brush
};

// True if the brush has a deformation engine. Mask, paint, cloth, pose and the other
// unimplemented brushes return false and leave the mesh untouched.
bool SculptBrushSupported(Icon brush);

// Brush falloff for t = 0 at the centre .. 1 at the rim, using the curve and hardness in `s`.
float SculptFalloff(float t, const SculptSettings& s);

// Deforms `target.positions`, or `*disp` when given (multires: the surface is positions + disp).
// Applies X/Y/Z symmetry and axis locks from `s`. Returns the number of vertices moved.
// Does not recompute normals; the caller does that once per dab.
size_t ApplySculptDab(Mesh& target, std::vector<Float3>* disp, const SculptSettings& s, Icon brush,
                      const SculptDab& dab, const BrushImage* stamp);
