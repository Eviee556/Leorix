# Leorix Native (C++)

Milestone 1: app shell with Leorix's dockable layout and theme, Vulkan device selection,
GPU and render-backend detection, and a Blender-style 3D viewport grid with navigation gizmo.

## Run
Double-click `leorix.exe` (prebuilt, no install needed).

## Build (Windows PowerShell, WinLibs MinGW - no Visual Studio)
    cd C:\LeorixNative
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    .\build\leorix.exe

The first configure downloads SDL3, Dear ImGui, volk and Vulkan-Headers (internet needed once).

## Mesh formats
    Import:  OBJ, FBX (binary + ASCII, Blender/Maya), glTF, GLB, USD (USDA ASCII)
    Export:  OBJ, glTF, GLB, USDA          (FBX export not available yet)
Quads and n-gons survive an OBJ/USDA round trip; glTF is a triangle format by spec,
so a mesh that goes out as glTF comes back triangulated.
Binary USDC is detected and reported - re-export it as USDA.

## Leorix Scripting (Lua)
    cube(size)  plane(size)  circle(r, verts)  sphere(r, segments, rings)
    ico_sphere(r, subdivisions)  cylinder(r, depth, verts)  cone(r, depth, verts)
    torus(major, minor, majorSeg, minorSeg)  grid(x, y, size)
    m = mesh();  a = m:vert(x, y, z);  m:face(a, b, c[, d, ...]);  m:build()
Sandboxed: no file, OS or network access.

## Command line
    leorix.exe --brush C:\path\stamp.png     (loads a custom sculpt brush image at startup)
    leorix.exe --open C:\path\model.fbx      (opens a mesh at startup)
    leorix.exe --convert in.fbx out.glb      (headless format conversion)
    leorix.exe --selftest                    (prints primitive counts, verifies scripting)

## Primitive topology
Counts match Blender's Add Mesh defaults exactly, with no subdivision:
    Plane 4 v / 1 face        Cube 8 v / 6 faces         Circle 32 v / 1 face
    UV Sphere 482 v / 512     Ico Sphere 42 v / 80       Cylinder 64 v / 34
    Cone 33 v / 33            Torus 576 v / 576          Grid 121 v / 100
Quads stay quads: the mesh is triangulated only for drawing, and every triangle remembers its
polygon, so the wireframe, edge selection and OBJ/USDA export all work on the real faces.
Objects are flat shaded by default (Shade Smooth is in the 3D menu and the hierarchy right-click).

## Undo
Ctrl+Z undoes, Ctrl+Shift+Z or Ctrl+Y redoes, and the Edit menu names the step. The History
panel lists every step and how much memory the history holds. Paint strokes only store the
tiles they touched, so a stroke costs kilobytes rather than a 16 MB layer copy.

## Projects (.leorix)
Ctrl+S saves, Ctrl+O opens. The file is a ZIP following ENGINE_SPEC v1.0: `manifest.json` holds
the canvas metadata and the whole layer hierarchy, and every layer is stored as
`layers/L{ID}_{Map}.png`. Round trips are lossless - pixels, opacity, blend mode, visibility,
lock state, map kinds and brush settings all come back.

## UV editor
Edit Mode + Edge select, then Mark Seam / Clear Seam / Mark Sharp / Clear Sharp - in the 3D menu,
the UV Editor panel or the Edit tool settings. Seams draw red on the mesh, sharp edges cyan.
Press U to unwrap: the mesh is cut into islands at the seams, each island flattened with LSCM,
then packed into the 0..1 square. Every one of these is undoable.
The panel opens with the 3D Design workspace, or View > UV Editor. It draws the selected model's UV
wireframe over the 2D canvas and marks seams and open borders in red, with counts for edges,
seams and islands. Both can be baked to a new layer to paint against.

## 2D painting
Maps panel: add a map of any of the 11 kinds; each gets its own layer stack and default fill.
Layers panel: new / duplicate / delete, opacity, 16 blend modes, visibility, lock, reorder, rename.
Canvas: paint with Brush, Pencil, Eraser and Fill using the size, opacity and hardness in Tool
Options. "Apply Base Color to 3D Material" pushes the painted map onto the selected model.

## Modifiers
A non-destructive stack per object, previewed live in the viewport while the base mesh stays
untouched until you press Apply: Array, Mirror, Solidify, Subdivision Surface (Catmull-Clark),
Smooth, Weld, Shrinkwrap, Screw/Spin, Remesh (voxel), Displace, Triangulate, Decimate, Curve Deform.
Reorder with ^ and v, toggle with the checkbox, remove with x. Edit Mode always edits the base
mesh (the cage), not the evaluated result.

## Normals
Orientation is fixed on import: the mesh is walked shell by shell so neighbouring faces agree,
then each shell is turned outward using its signed volume. Nothing to press.
Turn on Face Orientation (3D menu, or Edit > Overlays) to check: front faces read blue and
flipped faces read red. Flip Normals is still there for when you want a surface inverted.

## Sculpting and Multiresolution
Multiresolution panel: pick a level and Subdivide. Sculpt Mode strokes then write into a
displacement layer, so the base mesh keeps its clean topology - Apply bakes the detail down,
Clear throws it away. Draw/Clay, Inflate, Flatten, Grab, Snake Hook and Smooth deform geometry;
Shift smooths with any brush, Ctrl inverts, and X symmetry mirrors the stroke.
Without a multires level the brushes edit the mesh directly.

## Material maps
Ten slots: Base Color, Normal, Roughness, Metallic, AO, Height, Emissive, Specular, Opacity and
Subsurface. Load them with Shift+T (sorted by file name) or drag a painted map from the Maps
panel straight onto a slot. All ten are sampled per hit when you render with F12; the viewport
preview still only shows Base Color.
Emission: the colour is black by default so nothing glows, and loading an Emissive map drives it.
Parallax occlusion mapping uses the Height map - enable it in the Material panel and set depth.

## Camera
Shift+A > Camera (or 3D > Add Camera) adds a camera framed on the current view. There can be
several; the active one is what F12 renders and Numpad 0 looks through - switch with Ctrl+Numpad 0
on a selected camera, or right-click it in the Hierarchy > Set Active. The active camera's up
triangle is filled.
Each camera draws in the viewport as a frustum with a body and an up marker, and turns orange when
selected. Select it in the Hierarchy (or tick Select camera in the Camera panel), then G moves it
and R aims it, exactly like a mesh. Look At, Yaw, Pitch and Distance can also be typed in.
Numpad 0 looks through it - and navigating while in camera view moves the camera itself, so what
you frame is what renders.

## Rendering
F12 (or Render > Render Image) path-traces the scene on the CPU: BVH acceleration, all cores,
soft sun shadows, rough-metal reflections and a sky gradient, with a filmic tonemap. The Ray
Tracing panel holds resolution, samples, seed, per-type bounce limits, firefly clamping, shadow
toggle and sun angle. The result appears in the panel and can be saved as PNG.
Emissive materials light the scene, Transmission gives glass with IOR and Fresnel, and an
edge-aware denoiser cleans up low sample counts.
Environment panel: eight built-in environments (Studio Softbox, Noon Sky, Golden Hour, Overcast,
Night, Interior Window, Warehouse, Forest Clearing) shown as a thumbnail grid - they are generated
procedurally, so nothing to download. The HDRI now shows in the viewport background (Material/Rendered shading), reprojected as you orbit - not only in F12 renders. "Use as viewport background" toggles it off in favour of the flat gradient.
Or load an equirectangular HDRI (.hdr or an ordinary image), set strength and
rotation, or spin it with Shift + right-drag in the viewport.
Headless: leorix.exe --open model.obj --hdri env.hdr --render out.png

## Curves and snapping
3D > Add Curve gives a Bezier line, Circle or Spiral, drawn with its control points. Point a
Curve Deform modifier at one to bend a mesh along it (pick the axis that runs along the curve).
3D > Snapping sets Increment, Nearest Vertex or 1 m Grid; Ctrl held while moving toggles it.

## Object operations
Join Meshes (Ctrl+J) merges every selected object into the active one. Source vertices are
converted into the target's local space, so shapes stay where they were, and UVs, seam/sharp
flags and polygon ids all carry across. Any existing skin binding on the target is dropped,
since the vertex list it referred to no longer matches - re-parent with automatic weights after.

## Rigging and animation
Add Armature from the 3D menu or the Shift+A dialog: with a mesh selected it adds a bone to it,
with nothing selected it creates a standalone armature object.
Armature panel: Add Bone (chains from the selected bone), then "Parent to Mesh with Automatic
Weights" to bind. Pose sliders deform the mesh live. Pose Mode is the fifth mode in the viewport
header. Press I to keyframe the active bone (or the object in Object mode).
Timeline: play/pause, scrub, frame range, key diamonds per channel, Delete Key, and Bake.
Interpolation per key: Constant, Linear, Bezier, or one of nine easing equations (Sinusoidal,
Quadratic, Cubic, Quartic, Exponential, Circular, Back, Bounce, Elastic) with Ease In/Out/In-Out.
Pick the mode, then Set Interpolation - it applies to the keys on the current frame, or to the
whole action if the playhead is not on a key.

Ctrl+Space cycles Object -> Sculpt -> Edit -> Retopology -> Pose.

## Navigating the viewport
Middle-drag orbits, right-drag pans, wheel zooms - and if you don't have a middle mouse button,
Alt+LMB orbits, Alt+Shift+LMB pans, Alt+Ctrl+LMB zooms. The gizmo in the corner can be dragged to
orbit too (click without dragging to snap to that axis), and the hand and magnifier beside it pan
and zoom with any pointer, including a trackpad or a pen.

## Axes
Leorix follows Blender: X is left/right, Y is front/back, Z is up/down. The gizmo, transform
fields, G/R/S axis keys and modifier axis options all use those letters.

## Selection and duplicating
Drag with the left mouse button over empty space to box select - whole objects in Object Mode,
vertices / edges / faces in Edit Mode. Shift adds to the selection, Ctrl removes from it.
Shift+D duplicates the selected objects and starts moving the copy; click to place.

## View shortcuts (pointer over the 3D viewport)
    Numpad 1 / 3 / 7    front / right / top      (Ctrl + key for back / left / bottom)
    Numpad 9            flip to the opposite side
    Numpad 5            orthographic <-> perspective
                        (axis views switch to orthographic on their own; orbiting returns to
                        perspective, panning and zooming keep the view)
    Ctrl+Space          cycle Object -> Sculpt -> Edit -> Retopology -> Pose
    Numpad 4 6 8 2      orbit in 15 degree steps
    Numpad 0            look through the scene camera
    Ctrl+Alt+Numpad 0   align the camera to the current view
    Numpad .            frame the selected object
    Shift + RMB drag    rotate the environment

## Viewport shading
    Wire      edges only
    Solid     flat studio-lit preview (Blender's default working mode)
    Material  base colour textures, lit by the Environment settings
    Rendered  environment lighting, overlays and grid hidden
Environment panel: sky gradient, world colour and strength, sun elevation/rotation/colour/strength,
exposure, transparent film. These change Material and Rendered live.

## Textures
Shift+T (or Material > Load Texture Set) opens a multi-select file dialog and sorts each file into
a map slot by its name, the way Node Wrangler does:
    _BaseColor / _Albedo / _Diffuse    _Normal    _Roughness    _Metallic
    _AO / _Occlusion                   _Height / _Displacement  _Emissive
Base Color is shown in the viewport now; the other maps are loaded and kept for the GPU material pass.

## Controls
Middle-drag orbit, Shift+middle or right-drag pan, wheel zoom, Home resets the view,
click a gizmo ball to snap the view. F1 both workspaces, F2 focus 2D, F3 focus 3D.
Tool shortcuts match Leorix (B brush, E eraser, V move, ...).
3D viewport (pointer over it): Tab toggles Edit Mode, 1/2/3 vertex/edge/face (Shift combines),
Sculpt keys X Draw, C Clay Strips, L Layer, I Inflate, S Smooth, P Pinch, G Grab, K Snake Hook,
M Mask, Shift+C Crease, Shift+T Flatten. Retopology: Q Quad Draw, 1-9 other tools.

## Roadmap
1. App shell, Vulkan, GPU detection, viewport grid, New Project dialog  (done)
1b. Vector icons, Object/Sculpt/Edit/Retopology modes with full tool catalogs and settings,
    custom image sculpt brush shown in the 2D viewport  (this build - tools configure, engine next)
2. Mesh engine: Vulkan mesh viewport, OBJ/FBX import, primitives, undo, selection
3. Tool engines: sculpt brushes on real geometry, edit-mode operators, retopology on a source mesh
4. GPU mesh rasterizer (the current viewport fill is CPU-side) + ray-traced "Rendered" mode
5. 2D painting engine: layers, blend modes, brushes, PBR maps, masks, effects, EXR
6. Rigging, weight paint, timeline, cloth, 2.5D/diorama, webcomic mode
7. Production path tracer features: OpenPBR, random-walk SSS, denoiser, displacement
8. CUDA/OptiX (NVIDIA) and AMD HIP backends as optional plugin DLLs
