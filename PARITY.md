# Leorix Native - feature parity map

Status of the native rewrite against the Electron app. This is written from what has actually
been built and tested here, not from the Electron source: **`index.html` (the ~456 KB file with
the real 2D, 2.5D and webcomic code) was never uploaded to this conversation.** Only `main.js`,
`preload.js` and `package.json` came through. Anything below marked "reconstructed" was built
from descriptions, so names and behavior may differ from the original.

## Working and verified
| Area | State |
|---|---|
| App shell, docking, Ink & Brass theme | done |
| GPU/Vulkan setup, discrete-GPU pick, backend detection | done |
| New Project dialog (4 modes, templates, background, comic presets, 3D workspaces) | reconstructed |
| 3D viewport: orbit/pan/zoom, grid, axes, view gizmo | done |
| Mesh import: OBJ, FBX (Blender + Maya, binary/ASCII), glTF, GLB, USDA | done |
| Mesh export: OBJ, glTF, GLB, USDA | done (no FBX export) |
| Primitives: Blender counts verified, quads kept, no subdivision, flat shaded | done |
| Quad wireframe: triangulation diagonals never drawn or selectable | done |
| Per-face-corner UVs, so texturing never splits vertices | done |
| **Hierarchy / outliner**: object list, select, rename, duplicate, delete, visibility, transform fields | done |
| **Object mode selection**: click to select, Shift to extend, orange outline | done |
| **Grab / Rotate / Scale (G / R / S)** with X/Y/Z constraints, Esc cancel | done |
| Edit mode: vertex / edge / face select with purple / orange / yellow highlights | done |
| Edit mode grab: moves the selected vertices, edges or faces | done |
| **Viewport shading**: Wireframe / Solid / Material Preview / Rendered | done |
| **Environment panel**: sky gradient, world colour + strength, sun elevation/rotation/colour/strength, exposure | done (drives the viewport) |
| **Shift+T texture setup** (Node Wrangler style, slot guessed from file name) | done |
| Material panel: base colour, roughness, metallic, 7 map slots with thumbnails | done |
| Sculpt/Edit/Retopo tool catalogs and settings (Blender + Maya/RetopoFlow parity) | Sculpt: 23 of 31 brushes and most settings live; Retopo UI only |
| Custom image sculpt brush, shown in the 2D viewport | done |
| Leorix Scripting (Lua) mesh building | done |
| Cursor feedback in 2D and 3D viewports | done |
| **HDRI now shows in the viewport, not just F12 renders**: the equirectangular image is uploaded as a texture and reprojected every frame from the camera direction, so it pans correctly as you orbit instead of sitting still or not appearing at all - the bug was that the viewport only ever drew a flat two-colour gradient regardless of which environment was active | done |
| "Use as viewport background" checkbox (was already in the panel, now actually wired up) toggles between the HDRI and the flat gradient | done |
| **Viewport navigation fixed**: orbit, pan and zoom could silently stop working once an opened-but-undrawn popup (Ctrl+E) held ImGui's "active item", which is what navigation was relying on. Navigation now tracks its own drag state and cannot be stolen this way | done |
| **Ctrl+E edge menu now actually opens**: it was calling OpenPopup with nothing to draw it, so the popup existed but was invisible and ate the click | done |
| **Alt+LMB orbit / Alt+Shift+LMB pan / Alt+Ctrl+LMB zoom**: Blender's "emulate 3 button mouse", for trackpads and tablets with no middle button | done |
| **On-screen pan and zoom handles** beside the navigation gizmo (a hand and a magnifier) - drag either with any pointer | done |
| **Drag the gizmo itself to orbit**, click without dragging to snap to that axis (unchanged) | done |
| **Shade Smooth fixed**: smooth objects now get a colour per corner (Gouraud) in Solid, Material and Rendered; it previously averaged each triangle to one flat tone, so spheres stayed faceted | done |
| **Blender axes everywhere the user sees them**: X left/right, Y front/back, Z up/down - gizmo (X red, Y green, Z blue), transform fields, G/R/S axis keys, Mirror/Array/Screw/Curve Deform axes, camera Look At. Geometry is still stored Y-up so importers, primitives and the renderer are unchanged | done |
| **View titles and Auto Perspective**: "Front Orthographic", "Top Orthographic", "User Perspective"; axis views go orthographic, orbiting returns to perspective, panning and zooming keep the view | done |
| **Ctrl shortcuts actually fire**: Ctrl+Space, Ctrl+J and Ctrl+E were swallowed by a blanket Ctrl check; all Ctrl combos now route first, and plain letters are guarded so Ctrl+S cannot also start a Scale | done |
| **Shift+D duplicate**: copies the selection and goes straight into moving it; click to place, undoable | done |
| **LMB drag box select**: Object mode selects objects, Edit mode selects vertices / edges / faces by the current select mode; Shift extends, Ctrl subtracts | done |
| **Multiple cameras**: Shift+A > Camera (or 3D > Add Camera) adds one aligned to the current view; Ctrl+Numpad 0 or right-click > Set Active switches which one renders; delete from the Hierarchy; undoable | done |
| Camera drawn as a camera: a filled body box plus the lens frustum; the active camera's up triangle is filled, others are outlined | done |
| **Camera is a visible object**: frustum cone, body and up marker drawn in the viewport, orange when selected, with its name; selectable from the Hierarchy | done |
| **Camera is movable**: G moves it and R aims it like any object, or type Look At / Yaw / Pitch / Distance in the panel; Esc cancels | done |
| **Camera view is live both ways**: navigating while in camera view moves the camera itself, and panel edits push straight into the view | done |
| **Scene camera**: perspective/orthographic, focal length, sensor, clip range, DoF settings, passepartout | done (DoF applies at render time) |
| **Numpad views**: 1/3/7 front/right/top (Ctrl for opposite), 9 flip, 5 ortho toggle, 4/6/8/2 orbit, 0 camera view, Ctrl+Alt+0 align camera, . frame selected | done |
| Orthographic viewport projection | done |
| Depth buffer: grid, axes and edit overlays no longer draw through solid geometry | done |
| Rendered mode: ground shadows, depth-based ambient occlusion, specular highlights | done (raster, not ray traced) |
| Shift+RMB horizontal drag rotates the environment | done |
| Recalculate Normals Outside (Shift+N) / Flip Normals | done |
| Armature, automatic weights, linear blend skinning, Pose Mode | done |
| Keyframes (object + bone loc/rot/scale), Timeline, playback, Bake | done |
| `.leorix` project save/load (ENGINE_SPEC container) | done, lossless round trip |
| Undo/redo: paint tiles, layer structure, 3D transforms, add/delete, edit ops, sculpt strokes, materials, maps, armatures, keyframes | done |
| **Join Meshes (Ctrl+J)**: merges selected objects into the active one, transforming into its local space; UVs, seam and sharp flags and polygon ids all carry over; undoable | done |
| **Normals are consistent automatically**: every import runs a topological orientation pass (walk the shells, flip disagreeing neighbours, orient each shell outward by signed volume). There is no "Recalculate Normals" button because there is nothing to recalculate | done |
| **Face Orientation overlay**: front faces blue, flipped faces red, depth-tested so the far side does not bleed through. In the 3D menu and the Edit overlays | done |
| Flip Normals kept as a deliberate operation | done |
| **Multiresolution**: subdivide to a level, sculpt into a displacement layer that leaves the base mesh untouched, change levels keeping detail, Apply to bake, Clear to reset | done |
| **Sculpt brushes finally deform geometry**: Draw/Clay, Inflate, Flatten, Grab/Snake Hook, Smooth, with screen-space radius, smooth falloff, X symmetry, Shift to smooth, Ctrl to invert | done |
| **10 map slots wired into the renderer**: Base Color (with alpha), Normal, Roughness, Metallic, AO, Height, Emissive, Specular, Opacity, Subsurface - all sampled per hit in the path tracer | done |
| **Parallax occlusion mapping** from the Height map, with depth and step count | done |
| Emission fixed: an Emissive map now lights up on load (strength defaults to 1, colour defaults to black so nothing glows unintentionally) | done |
| **Drag a map from the Maps panel onto a material slot** to assign it | done |
| **8 built-in environments**: Studio Softbox, Noon Sky, Golden Hour, Overcast, Night, Interior Window, Warehouse, Forest Clearing - generated procedurally as equirectangular float images, picked from a thumbnail grid | done |
| **HDRI environments**: equirectangular .hdr or LDR images, strength and rotation, Shift+right-drag spins it, used by the path tracer | done |
| Path tracer light transport: emissive surfaces as lights, glass/refraction with IOR and Fresnel, edge-aware denoiser | done |
| **Path tracer (CPU)**: BVH, multi-threaded, cosine-weighted diffuse, rough metal reflection, soft sun shadows, sky gradient, Russian roulette, firefly clamp, filmic tonemap; F12 renders, result shown in-panel and savable as PNG | done |
| **Ray Tracing panel**: resolution presets, samples, seed, max/diffuse/glossy/transmission bounces, clamping, shadow toggle, sun angle (penumbra width), thread count, progress + cancel | done |
| Headless rendering: `leorix --open mesh --render out.png` | done |
| Curves in the Shift+A dialog beside the mesh primitives | done |
| **Subsurface scattering**: weight, method, per-channel radius, scale, anisotropy, IOR - approximated live in Rendered with wrapped diffuse + back-lit transmission | done |
| **Curves**: Bezier line, Circle, Spiral objects with Catmull-Rom evaluation, drawn with control points | done |
| **Curve Deform modifier**: maps a mesh's extent along a curve, with axis and stretch | done |
| **Snapping** during move: Increment, Nearest Vertex, 1 m Grid; Ctrl toggles while dragging | done |
| **Modifier stack (13)**: Array, Mirror, Solidify, Subdivision (Catmull-Clark), Smooth, Weld, Shrinkwrap, Screw/Spin, Remesh (voxel), Displace, Triangulate, Decimate - non-destructive, live preview, reorder, enable/disable, Apply (undoable) | done |
| Keyframe interpolation: Constant, Linear, Bezier + 9 easing equations (Sinusoidal, Quadratic, Cubic, Quartic, Exponential, Circular, Back, Bounce, Elastic) with Ease In / Out / In-Out | done |
| Ctrl+Space cycles Object -> Sculpt -> Edit -> Retopology -> Pose | done |
| **Add Armature**: from the 3D menu or the Shift+A dialog - adds a bone to the selected mesh, or creates a standalone armature object | done |
| **Visual scripting (blocks)**: beginner node editor, 17 plain-English blocks, typed wires, cycle checks; graphs are not saved yet | done |

## Known gaps in "everything live"
Material, Environment, Camera and Sculpt settings update the viewport immediately. Retopology
settings and some sculpt brushes (Rotate, Slide Relax, Boundary, Pose, Cloth, Mask, Face Sets,
Paint, Smear, Wrinkle, Noise, Simplify) have no engine yet. 2D tool settings likewise have no engine behind them.

## From ENGINE_SPEC v1.0
| Area | State |
|---|---|
| `.leorix` container: ZIP with `manifest.json` + `layers/L{ID}_{Map}.png` | done |
| Manifest: canvas metadata (size, colour space, bit depth), project settings, full layer hierarchy (id, name, opacity, blend, visibility, lock) | done |
| Save Project As (Ctrl+S) / Open Project (Ctrl+O), verified lossless round trip | done |
| Reoriented Normal Mapping when compositing Normal maps, instead of alpha blending | done |
| Dirty-tile undo deltas (spec section 3) - tiles yes, LZ4 compression no | partial |
| Not yet: brushes/ and cache/ directories in the container, .bin raw precision for Normal and Displacement, SharedArrayBuffer-style worker architecture (native app is threaded differently) | open |

## Undo / Redo
| Area | State |
|---|---|
| Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y, Edit menu showing the step name, History panel | done |
| Paint strokes: only the 128x128 tiles a stroke touched are stored, before and after | done |
| Layer structure: add, duplicate, delete, reorder (whole-stack snapshot, as Leorix does) | done |
| 3D: move/rotate/scale, add primitive, delete object | done |
| History capped by memory budget and step count; oldest steps drop first | done |
| Map add/remove, material params (one step per drag), texture loads, armature edits, keyframes | done |

## UV editor
| Area | State |
|---|---|
| UV wireframe drawn over the 2D canvas, with opacity and colour | done |
| Seam detection: an edge whose two sides land apart in UV space, or has one adjoining face | done |
| Seam colour and width, island and edge counts | done |
| Bake UV Layout / Bake Seams to a new layer (undoable) | done |
| Mark Seam / Clear Seam / Mark Sharp / Clear Sharp - Edit Mode, Edge select only | done |
| Seams drawn red and sharp edges cyan in the viewport, depth-tested | done |
| Unwrap (U): islands cut at seams, LSCM flattening, shelf packing into 0..1 | done |
| Verified: flat and developable surfaces unwrap with 0 degree angle distortion | done |
| Not yet: UV editing (moving verts/islands in UV space), Smart UV, live unwrap, sharp edges affecting shading | open |

## Ported from index.html this round
| Area | State |
|---|---|
| Map system: 11 kinds (Base Color, Normal, Roughness, Metallic, AO, Cavity, Height, Emissive, Opacity, ORM, Custom) with Leorix's labels, dot colours and default fills | done |
| Layer stacks per map: add, duplicate, delete, reorder, rename, visibility, lock, opacity | done |
| All 16 blend modes including the four HSL ones (Hue, Saturation, Color, Luminosity) | done |
| Painting: brush, pencil, eraser, fill - size, opacity, hardness, spaced dabs | done |
| Composite only the dirty region, then upload to the GPU | done |
| Apply a painted Base Color map to the 3D material | done |

## Still to port from index.html
Panels that exist in the Electron app and are now readable as reference (the handoff document
confirms these, with implementation notes for anything non-obvious):
`effects-panel` (8 layer effects: Drop Shadow, Inner Shadow, Outer/Inner Glow, Stroke, Color
Overlay, Gradient Overlay, Bevel & Emboss), layer masks, selections (Marquee/Lasso/Selection
Mask), `bc-panel` (Brightness/Contrast - snapshots the layer once at drag-start, not on every
tick, or the formula compounds), `map-brush-panel` (the modifier stack - 13 modifiers - and HDRI presets
are already ported natively, see `src/mesh/Modifier.*` and `src/render/HdriPresets.*`), `cloth-panel` (PBD, 22x22, no self-collision), `weight-panel`
(inverse-distance falloff painting), `rig-panel` (17-marker humanoid, 21 bones, UE5/Unity/
Mixamo/Godot/Plain naming presets, two-bone IK), `rig2d-panel`, `scene25d-panel` (flat parallax
+ 3D diorama + keyframeable camera path), `comic-panel`, `create3d-panel`, Channel Packer,
Normal-from-Height (Sobel), ORM packing, custom EXR export (16-bit half-float).

## Poly Spec (Blender-style edit mode) operators
**Extrude (E), Delete (X), Merge at Center (M)** and box-select are done (`src/mesh/EditOps.*`), each one
undo step and covered by `--selftest`. Still missing: extruding bare edges/verts, UVs on extruded side faces
(stretched for now), and the G/R/S axis-lock behavior of the Electron version. The rule all of them follow:
any operation that changes vertex or triangle count must rebuild `loopUVs` in the same loop, at the same
indices, as it rebuilds `positions`/`tris` - otherwise the UV-seam and wireframe code silently reads garbage.
Ops refuse to run while an armature is bound or multires is active (both store per-vertex data).

## Already better than the Electron version
Two things their handoff calls out as deliberate scope limits are already solved in the native
app because of how it's structured differently:
- **Per-object material and per-object Shade Smooth/Flat.** Electron Leorix shares one
  `THREE.MeshStandardMaterial` across the whole scene, so shading toggles are global. The native
  `MeshObject` already carries its own `Material` and its own `shadeSmooth` flag.
- **No raycast-hijacking from overlay geometry.** Electron Leorix parents visual helpers
  (wireframe, selection outline, normals) as raycastable children of the mesh and must
  explicitly disable raycast on each one, or clicking near a wireframed mesh hijacks the click.
  The native picker only ever tests real mesh triangles, so this class of bug doesn't exist here.

## Not built yet (needs the Electron source to match properly, or is separate engine work)
| Area | State / why it's missing |
|---|---|
| 2D drawing and layers | **done** - see the painting/layers/UV sections above |
| PBR map set, blend modes, masks, layer effects, selections | maps/layers/blend modes done; masks, effects, selections not yet |
| 2.5D flat parallax + 3D diorama + camera path | not started |
| Webcomic: panel grids, balloons, page guides, bleed/safe area | not started |
| Shift+D in Edit Mode (duplicate geometry) | not started - Shift+D duplicates objects |
| Separate (P) - the inverse of Join | not started |
| Cross-object armature parenting (one armature deforming several meshes) | not started - an armature currently belongs to the object it deforms |
| Multires level switching is nearest-neighbour resampled, not a true displacement hierarchy | open - detail survives a level change but is approximated when the level moves |
| Snapping for rotate and scale, and face/edge snap targets | not started - move snapping works |
| Curve editing: moving control points in the viewport, handle types, bevel/taper | not started - curves are created from presets and used as deform targets |
| Cloth simulation | not started |
| Weight painting (manual, on top of automatic weights) | not started |
| Rig naming presets (UE5/Unity/Mixamo/Godot/Plain), two-bone IK | not started - Armature/skinning/keyframes exist, presets and IK don't |
| Bone constraints (Copy Rotation/Location, Track To, Limit Rotation, IK) | not started - bone *parenting* works, constraints do not |
| Rigid body / physics simulation | not started |
| Curves (Bezier, NURBS, path, circle) and curve-to-mesh | not started |
| Control rig presets and export for Unreal / Unity / Godot | not started - needs a skinned FBX/glTF exporter first |
| Gyro-style control rig widgets (control objects driving bones) | not started - needs constraints first |
| Texture painting directly onto the 3D model (paint in the viewport, not the canvas) | not started - needs UV-space projection from the viewport |
| UV editing: move verts/islands in UV space, Smart UV Project, live unwrap | not started - unwrap and display work, editing does not |
| Corona, V-Ray, Redshift engines | proprietary SDKs, not publicly redistributable |
| AMD HIP / ROCm render backend | needs MSVC + the HIP SDK; would be an optional plugin DLL |
| GPU (Vulkan RT) rendering | the tracer runs on the CPU; the GPU backend is detected but unused |
| Viewport "Rendered" shading is still the raster preview, not the path tracer | open |
| Volumetrics, caustics-specific sampling, light portals, MIS | not started |
| Next-event estimation for emissive surfaces | done - area sampling + MIS for the diffuse lobe; glossy/glass and textured emitters still rely on BSDF hits |
| Maps in the viewport: Normal/Roughness/Metallic/AO now shade Material/Rendered modes | done per vertex - per-pixel needs a GPU fill pass |
| Tangent-space normal maps (viewport and path tracer, OpenGL green-up convention) | done |
| HDRI as the viewport background (it lights renders, the viewport still draws the gradient) | open |
| Height maps affecting 3D shading | open - Normal/Roughness/Metallic/AO now shade |

## To close the gap
Both `index.html` and the project handoff doc have now been provided and read. The remaining
gap is engine work (2.5D, webcomic, Poly Spec operators, modifiers, cloth, weight painting,
constraints, physics, curves) rather than missing reference material.
