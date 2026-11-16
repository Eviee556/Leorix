#pragma once
// Armature panel sections for rig presets and bone constraints. Kept out of App3D.cpp; the
// panel there owns undo and skinning, these only draw widgets and edit the data.
#include "anim/Armature.h"
#include <functional>
#include <string>

// "Add Humanoid Rig" and "Rename Bones" to a naming convention. `edit` applies a change to the
// armature as one undo step. Returns a status message, empty when nothing happened.
std::string RigPresetUI(const Armature& arm, const Mesh& mesh,
                        const std::function<void(const char*, const std::function<void(Armature&)>&)>& edit);

// The constraint stack of bone `self`. `track(label)` is called after every drag widget so the
// caller can record the drag as one undo step. Returns 0 when nothing changed, 1 for a live
// drag edit, 2 for a discrete edit the caller should record now.
int BoneConstraintsUI(const Armature& arm, int self, Bone& b, const std::function<void(const char*)>& track);
