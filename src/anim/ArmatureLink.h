#pragma once
// Links between objects that the armature code itself cannot see: a mesh deformed by another
// object's armature (Blender's Armature modifier), and constraints that target scene objects
// (control-rig widgets). Everything here works on the Scene and is headless, so the self-test
// can drive it without a window.
#include "mesh/Scene.h"
#include <vector>

// The object whose bones deform `o`: o's linked armature object when it resolves to another
// object with bones, otherwise `o` itself when it has its own, otherwise null.
const MeshObject* BoneSource(const Scene& scene, const MeshObject& o);

// Writes every object-targeted constraint's target (position, and rotation for Copy Rotation)
// into armObj's object space. Returns true when any value changed.
bool ResolveObjectTargets(const Scene& scene, MeshObject& armObj);

// Recomputes o.skinned from its own armature or from its linked armature object (clears it when
// neither applies), after resolving object targets.
void SkinObject(const Scene& scene, MeshObject& o);
// SkinObject on `o` and on every mesh linked to o's armature.
void SkinWithLinks(const Scene& scene, MeshObject& o);

// Binds `mesh` to armObj's bones with automatic weights (computed in the armature's space) and
// links it by id. False when armObj has no bones or is the mesh itself.
bool BindToArmatureObject(const Scene& scene, MeshObject& mesh, MeshObject& armObj);

// Per-frame upkeep: re-resolves object targets (a control moved) and re-skins linked meshes whose
// armature object or own transform moved. Returns how many objects were re-skinned.
int SyncRigs(Scene& scene);

// For export: `child` expressed in its armature object's space with that object's bones, so the
// skinned exporters can treat it like a self-contained rig. False when child is not linked.
bool LinkedRigForExport(const Scene& scene, const MeshObject& child, Mesh& meshOut, Armature& armOut,
                        const Action** actionOut);

// Control-rig widgets for a humanoid armature on armObj: hand and foot IK controls with elbow and
// knee poles, a hip control (Copy Location + Copy Rotation on the hips) and a chest control (Copy
// Rotation). Adds wire objects to the scene and the constraints to the bones. Returns the new
// objects (empty when the humanoid bones are not found or the rig already has controls).
std::vector<MeshObject*> AddControlRig(Scene& scene, MeshObject& armObj);

// A control widget's wire, as a line list (positions 2k and 2k+1 form one segment).
enum class ControlShape { Circle, Box, Diamond };
Mesh ControlWire(ControlShape shape, float size);
