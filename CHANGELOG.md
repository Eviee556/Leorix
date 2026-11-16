# Changelog

All notable changes to Leorix Native are listed here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/).

## [Unreleased]

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
