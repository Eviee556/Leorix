# Changelog

All notable changes to Leorix Native are listed here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/).

## [0.5.0] - 2026-11-16

### Added
- `installer/build_setup.sh`: builds the installer from Git Bash, with no PowerShell needed.
- Weight Paint mode: Add, Subtract, Replace and Smooth brushes for the active bone with the sculpt falloff curves, X mirror that paints the `.L`/`.R` twin bone, a blue-to-red weight heatmap in the viewport, Normalize All and Clean, and one undo step per stroke. Edited weights are saved in projects.
- Bezier curve editing: Auto, Vector, Aligned and Free handles; a curve Edit Mode with G/R/S, E to extrude an end point and X to delete; cyclic curves; per-point radius for taper; bevel tubes swept with rotation-minimising frames so they never flip; Convert to Mesh; and a Path preset. Curves are saved in projects.
- Rigid-body physics: Active and Passive bodies with Box, Sphere or Convex Hull shapes, mass, friction, bounciness and gravity, baked per timeline frame so playback is repeatable. Bake, Free Bake and Apply Transform at Current Frame are undoable. Physics settings are saved in projects; the bake itself is not, so re-bake after opening.

### Changed
- Existing curves are now Bezier with Auto handles instead of a Catmull-Rom spline, so meshes bent with Curve Deform can shift slightly.

### Fixed
- Curve edits now update meshes bent with Curve Deform.
- Duplicating or moving a physics-baked object no longer snaps it back onto the old baked path.
- Weight Paint X Mirror works on posed rigs and no longer moves centre-line vertices to the mirrored bone.
- Joining into, or sculpting, a curve's tube is refused instead of losing the geometry.
- Physics bakes are capped at 10000 frames so the app can't hang.

## [0.4.0] - 2026-11-15

### Added
- Snapping: rotate snaps in 15° steps (5° with Shift) and scale in 0.1 steps; move can snap to the nearest vertex, edge, edge center or face of other objects, in Object and Edit Mode.
- Skinned glTF/GLB export: armatures export as glTF skins with up to four normalized weights per vertex, and keyframes are baked to glTF animation.
- Rigging: two-bone IK with a pole target, and bone constraints (IK, Track To, Copy Location, Copy Rotation, Limit Rotation) evaluated before skinning.
- Add Humanoid Rig (21 bones, sized to the mesh) and Rename Bones presets for Plain, UE5, Unity, Mixamo and Godot naming.
- UV editing on the 2D canvas: select islands or vertices, then move, rotate and scale them, with undo.
- Smart UV Project: groups faces by angle, unwraps each group with LSCM and packs the islands.
- Painting directly on the 3D model with the Sculpt Mode Paint brush, through the model's UVs, with live material updates and tile undo.
- `.leorix` projects now save the whole 3D scene: meshes, materials and textures, modifier stacks, armatures with constraints and weights, keyframes, cameras and playback settings. Older projects still open.
- Tutorial book: 56 tutorials, with new ones for Mirror, per-copy Random, Copy Upwards, Scatter Randomly, the Time block and Lua animation (`frame`, `time`, `fps`).

### Fixed
- Painting on a posed (skinned) model put paint in the wrong place.
- IK combined with other constraints on the same chain solved against a stale pose.
- Bone constraints were ignored by skinned glTF export.
- Geometry snapping broke an axis constraint (G then X).
- The UV panel's Rotate and Scale could act on another object's UVs.
- Painting on a multiresolution object always reported missing UVs.
- A project manifest with a wrongly typed field could crash on load.

## [0.3.0] - 2026-11-15

### Added
- Block graphs in the visual scripting editor can be saved and loaded.
- Random Number can give each copy its own value, so copied buildings and trees no longer repeat.
- A Time value for Lua scripts and visual blocks, for procedural animation.
- Edit Mode: Shift+D duplicates the selected geometry and P separates it into a new object.
- More beginner blocks in the visual scripting editor.
- Windows installer (`LeorixNative-Setup.exe`), built with `installer/build_setup.ps1`. It installs per user, adds a Start menu shortcut and an optional desktop shortcut, and can be removed from Apps & Features.
- GitHub Actions workflow that builds the app and runs the self-test on Windows.
- `.gitattributes` and this changelog.

### Fixed
- A runaway Lua script could escape the instruction limit by catching the error with nested `pcall`.
- Merge at Center (M) could read past the vertex list after a modifier removed vertices.
- Undo gaps: Lua runs, imports and visual-script builds now each get an undo step, so Ctrl+Z no longer removes the wrong object.
- The path tracer no longer reads textures that are edited or deleted while an F12 render is running.
- Normal-map tangents are cached instead of recomputed every frame.
- A sculpt stroke released outside the viewport is still closed and recorded for undo.
- PARITY.md: multiple importance sampling is listed as done.

## [0.2.0] - 2026-11-15

### Added
- Beginner visual scripting block editor: 17 plain-English blocks, colour-coded Shape and Number wires and a description on every block.
- Normal, Roughness, Metallic and AO maps shade the Material and Rendered viewport modes.
- Edit Mode Extrude (E), Delete (X) and Merge at Center (M), keeping quads and UVs.
- Path tracer: next-event estimation with multiple importance sampling for emissive surfaces, plus tangent-space normal maps.
- Sculpt engine with 23 brushes and one undo step per stroke.
- Undo for map deletion, material parameters and textures, armature edits and keyframes.
- Self-test with asserted results, run by CTest.
- Documentation: README, architecture guide, Lua and visual scripting guides, and a procedural generation tutorial book with 31 Lua tutorial scripts.
- MIT license.

### Fixed
- Only the last path-tracer worker thread marks a render finished, which fixes streaks along the bottom of renders.
- Lua scripts: `m:face()` validates its indices, runaway scripts are capped by instruction and memory limits, and merged script meshes keep their UVs.

### Removed
- Duplicate sources under `src/mesh/`.

## [0.1.0] - 2026-10-15

### Added
- First version: Vulkan/SDL3/ImGui app shell, mesh import and export, modifier stack, UV unwrap, layered PBR texture painting, CPU path tracer, rigging and keyframe animation, Lua scripting and `.leorix` project files.

### Changed
- TransformOp and New Project state moved out of `App`.

[0.5.0]: https://github.com/Eviee556/Leorix/compare/v0.4.0...v0.5.0
[0.4.0]: https://github.com/Eviee556/Leorix/releases/tag/v0.4.0
[0.3.0]: https://github.com/Eviee556/Leorix/releases/tag/v0.3.0
