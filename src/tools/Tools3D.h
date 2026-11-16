#pragma once
// 3D interaction modes, tool catalogs and every tool setting, modeled on Blender's sculpt and
// edit modes plus Maya Quad Draw / RetopoFlow style retopology.
// Milestone 2 plugs the mesh engine into these; the settings already persist per session.
#include "app/Icons.h"
#include <string>

enum class Mode3D { Object, Sculpt, Edit, Retopology, Pose, Weight };
enum class SelectMode { Vertex = 1, Edge = 2, Face = 4 }; // bit flags, Shift+click combines like Blender

enum class BrushCategory { AddSubtract, Contrast, Transform, Cloth, General, Paint, Leorix };

struct SculptBrushDef {
    const char*   name;
    Icon          icon;
    BrushCategory category;
    const char*   shortcut;     // Blender keymap where one exists
    float         strength;     // default strength 0..1
    bool          hasDirection; // Add/Subtract toggle
    const char*   hint;
};

inline const SculptBrushDef kSculptBrushes[] = {
    // Add / Subtract
    {"Draw",               Icon::SculptDraw,        BrushCategory::AddSubtract, "X",       0.50f, true,  "Moves vertices along the average normal"},
    {"Draw Sharp",         Icon::SculptDrawSharp,   BrushCategory::AddSubtract, "",        0.50f, true,  "Draw with a sharp falloff for fine creases"},
    {"Clay",               Icon::SculptClay,        BrushCategory::AddSubtract, "",        0.50f, true,  "Builds up material toward a plane"},
    {"Clay Strips",        Icon::SculptClayStrips,  BrushCategory::AddSubtract, "C",       0.60f, true,  "Square-tipped clay for blocking forms"},
    {"Clay Thumb",         Icon::SculptClayThumb,   BrushCategory::AddSubtract, "",        0.50f, true,  "Simulates pressing clay with a thumb"},
    {"Layer",              Icon::SculptLayer,       BrushCategory::AddSubtract, "L",       0.50f, true,  "Raises a layer of constant height"},
    {"Inflate",            Icon::SculptInflate,     BrushCategory::AddSubtract, "I",       0.50f, true,  "Pushes vertices along their own normals"},
    {"Blob",               Icon::SculptBlob,        BrushCategory::AddSubtract, "",        0.50f, true,  "Magnifies and inflates a round shape"},
    {"Crease",             Icon::SculptCrease,      BrushCategory::AddSubtract, "Shift+C", 0.50f, true,  "Pinches and pushes to cut sharp creases"},
    // Contrast
    {"Smooth",             Icon::SculptSmooth,      BrushCategory::Contrast,    "S",       0.50f, false, "Relaxes the surface (hold Shift with any brush)"},
    {"Flatten",            Icon::SculptFlatten,     BrushCategory::Contrast,    "Shift+T", 0.50f, true,  "Pulls vertices toward an average plane"},
    {"Fill",               Icon::SculptFill,        BrushCategory::Contrast,    "",        0.50f, true,  "Raises areas below the plane only"},
    {"Scrape",             Icon::SculptScrape,      BrushCategory::Contrast,    "",        0.50f, true,  "Lowers areas above the plane only"},
    {"Multi-plane Scrape", Icon::SculptMultiScrape, BrushCategory::Contrast,    "",        0.50f, true,  "Scrapes with two angled planes for hard edges"},
    {"Pinch",              Icon::SculptPinch,       BrushCategory::Contrast,    "P",       0.50f, true,  "Pulls vertices toward the brush center"},
    // Transform
    {"Grab",               Icon::SculptGrab,        BrushCategory::Transform,   "G",       1.00f, false, "Drags a group of vertices with the mouse"},
    {"Elastic Deform",     Icon::SculptElastic,     BrushCategory::Transform,   "",        1.00f, false, "Grab with physically plausible falloff"},
    {"Snake Hook",         Icon::SculptSnakeHook,   BrushCategory::Transform,   "K",       1.00f, false, "Pulls long tendrils out of the surface"},
    {"Thumb",              Icon::SculptThumb,       BrushCategory::Transform,   "",        0.50f, false, "Slides vertices along the surface"},
    {"Pose",               Icon::SculptPose,        BrushCategory::Transform,   "",        1.00f, false, "Poses a limb with automatic IK segments"},
    {"Nudge",              Icon::SculptNudge,       BrushCategory::Transform,   "",        0.50f, false, "Pushes vertices in the stroke direction"},
    {"Rotate",             Icon::SculptRotate,      BrushCategory::Transform,   "",        1.00f, false, "Twists vertices around the brush center"},
    {"Slide Relax",        Icon::SculptSlideRelax,  BrushCategory::Transform,   "",        0.50f, false, "Slides topology without changing the shape"},
    {"Boundary",           Icon::SculptBoundary,    BrushCategory::Transform,   "",        1.00f, false, "Deforms open mesh boundaries and loops"},
    // Cloth
    {"Cloth",              Icon::SculptCloth,       BrushCategory::Cloth,       "",        1.00f, false, "Local cloth simulation under the brush"},
    {"Cloth Folds",        Icon::SculptClothFolds,  BrushCategory::Cloth,       "",        0.80f, false, "Cloth preset that pinches perpendicular folds"},
    // General
    {"Simplify",           Icon::SculptSimplify,    BrushCategory::General,     "",        0.50f, false, "Collapses edges (needs Dyntopo)"},
    {"Mask",               Icon::SculptMask,        BrushCategory::General,     "M",       1.00f, true,  "Paints a mask that protects vertices"},
    {"Draw Face Sets",     Icon::SculptFaceSets,    BrushCategory::General,     "",        1.00f, false, "Paints face sets for isolating regions"},
    // Paint
    {"Paint",              Icon::SculptPaint,       BrushCategory::Paint,       "",        1.00f, false, "Paints the active texture map straight onto the model"},
    {"Smear",              Icon::SculptSmear,       BrushCategory::Paint,       "",        1.00f, false, "Smears vertex color"},
    // Leorix extras
    {"Wrinkle",            Icon::SculptWrinkle,     BrushCategory::Leorix,      "",        0.40f, true,  "Parallel micro-folds for skin and fabric"},
    {"Noise",              Icon::SculptNoise,       BrushCategory::Leorix,      "",        0.30f, true,  "Procedural surface noise for pores and stone"},
    {"Polish",             Icon::SculptPolish,      BrushCategory::Leorix,      "",        0.50f, false, "Flattens and tightens hard-surface planes"},
    {"Planar Trim",        Icon::SculptPlanarTrim,  BrushCategory::Leorix,      "",        0.70f, false, "Cuts everything past a view-aligned plane"},
    {"Custom Image",       Icon::SculptCustomImage, BrushCategory::Leorix,      "",        0.50f, true,  "Stamps your own imported height image"},
};
inline const int kSculptBrushCount = sizeof(kSculptBrushes) / sizeof(kSculptBrushes[0]);
inline const char* BrushCategoryName(BrushCategory c) {
    switch (c) {
        case BrushCategory::AddSubtract: return "Add / Subtract";
        case BrushCategory::Contrast:    return "Contrast";
        case BrushCategory::Transform:   return "Transform";
        case BrushCategory::Cloth:       return "Cloth";
        case BrushCategory::General:     return "General";
        case BrushCategory::Paint:       return "Paint";
        case BrushCategory::Leorix:      return "Leorix";
    }
    return "";
}

struct ToolDef3D { const char* name; Icon icon; const char* shortcut; const char* hint; };

inline const ToolDef3D kObjectTools[] = {
    {"Select Box", Icon::SelectBox, "B", "Drag a box to select objects"},
    {"Cursor",     Icon::Cursor3D,  "Shift+RMB", "Place the 3D cursor"},
    {"Move",       Icon::Move3D,    "G", "Translate the selection"},
    {"Rotate",     Icon::Rotate3D,  "R", "Rotate the selection"},
    {"Scale",      Icon::Scale3D,   "S", "Scale the selection"},
    {"Transform",  Icon::Transform3D, "", "Move, rotate and scale gizmo"},
};

inline const ToolDef3D kEditTools[] = {
    {"Select Box",      Icon::SelectBox,     "B", "Box select; Shift extends, Ctrl subtracts"},
    {"Cursor",          Icon::Cursor3D,      "Shift+RMB", "Place the 3D cursor"},
    {"Move",            Icon::Move3D,        "G", "Translate; press X/Y/Z to constrain"},
    {"Rotate",          Icon::Rotate3D,      "R", "Rotate; press X/Y/Z to constrain"},
    {"Scale",           Icon::Scale3D,       "S", "Scale; press X/Y/Z to constrain"},
    {"Extrude Region",  Icon::Extrude,       "E", "Extrude connected faces together"},
    {"Inset Faces",     Icon::Inset,         "I", "Inset selected faces"},
    {"Bevel",           Icon::Bevel,         "Ctrl+B", "Bevel edges (Ctrl+Shift+B for vertices)"},
    {"Loop Cut",        Icon::LoopCut,       "Ctrl+R", "Add edge loops across a face ring"},
    {"Knife",           Icon::Knife,         "K", "Cut new edges across faces"},
    {"Poly Build",      Icon::PolyBuild,     "", "Build and delete faces vertex by vertex"},
    {"Spin",            Icon::Spin,          "", "Extrude selection around an axis"},
    {"Smooth Vertices", Icon::SmoothVerts,   "", "Relax selected vertices"},
    {"Edge Slide",      Icon::EdgeSlide,     "G G", "Slide edges along their neighbors"},
    {"Shrink/Fatten",   Icon::ShrinkFatten,  "Alt+S", "Move along normals"},
    {"Shear",           Icon::Shear,         "Shift+Ctrl+Alt+S", "Shear the selection"},
    {"Rip Region",      Icon::Rip,           "V", "Rip faces apart"},
    {"Merge",           Icon::Merge,         "M", "Merge vertices (center, cursor, distance)"},
};

inline const ToolDef3D kRetopoTools[] = {
    {"Quad Draw",   Icon::QuadDraw,   "Q", "Maya-style: click dots on the surface, Shift+click fills a quad"},
    {"Contours",    Icon::Contours,   "1", "Draw strokes across a cylinder to create loops"},
    {"PolyStrips",  Icon::PolyStrips, "2", "Draw strokes to lay down quad strips"},
    {"Strokes",     Icon::Strokes,    "3", "Draw spans that bridge and extrude"},
    {"Patches",     Icon::Patches,    "4", "Fill a closed boundary with a quad patch"},
    {"PolyPen",     Icon::PolyPen,    "5", "Insert vertices, edges and faces one at a time"},
    {"Loops",       Icon::Loops,      "6", "Insert and slide edge loops"},
    {"Knife",       Icon::Knife,      "7", "Cut across retopology faces"},
    {"Tweak",       Icon::Tweak,      "8", "Brush-move vertices, snapped to the source"},
    {"Relax",       Icon::Relax,      "9", "Evenly distribute vertices over the source"},
    {"Select Brush",Icon::SelectBrush,"", "Paint a selection"},
};

inline const int kObjectToolCount = sizeof(kObjectTools) / sizeof(kObjectTools[0]);
inline const int kEditToolCount   = sizeof(kEditTools) / sizeof(kEditTools[0]);
inline const int kRetopoToolCount = sizeof(kRetopoTools) / sizeof(kRetopoTools[0]);

// ---------------------------------------------------------------- settings
struct SculptSettings {
    int   brush = 0;
    float radiusPx = 60.0f, strength = 0.5f, hardness = 0.0f, normalRadius = 0.5f, autoSmooth = 0.0f;
    bool  subtract = false, radiusInSceneUnits = false;
    int   falloffShape = 0;   // Sphere, Projected
    int   falloffCurve = 0;   // Smooth, Smoother, Sphere, Root, Sharp, Linear, Sharper, Inverse Square, Constant
    int   strokeMethod = 0;   // Space, Dots, Drag Dot, Anchored, Airbrush, Line, Curve
    float spacing = 0.1f;
    bool  stabilize = false; float stabilizeRadius = 75.0f, stabilizeFactor = 0.75f;
    bool  symX = true, symY = false, symZ = false, lockX = false, lockY = false, lockZ = false;
    bool  maskTopology = false, maskFaceSets = false, maskBoundary = false, maskFaceSetBoundary = false, maskCavity = false, maskViewNormal = false;
    bool  frontFacesOnly = false;
    float planeOffset = 0.0f, planeTrim = 0.5f; bool useTrim = false;
    bool  dyntopo = false; float detailSize = 12.0f; int refineMethod = 2; int detailing = 0;
    float voxelSize = 0.05f;
    int   clothDeformation = 0; int clothFalloff = 0; float clothMass = 1.0f, clothDamping = 0.01f; int clothSimArea = 0; bool clothPersistent = false;
    int   poseOrigin = 0; int poseSegments = 1; int poseSmooth = 4;
    bool  grabActiveVertex = false, grabSilhouette = false;
    float snakeRake = 0.0f, snakePinch = 0.0f;
    float wrinkleCount = 6.0f, noiseScale = 0.2f;
    // Custom image brush
    int   imageMapping = 0;   // View Plane, Area Plane, Tiled, Random, Stencil
    float imageAngle = 0.0f, imageScale = 1.0f, imageMidLevel = 0.5f;
    bool  imageRandomAngle = false, imageInvert = false, showImageIn2D = true;
};

struct EditSettings {
    int   selectMode = static_cast<int>(SelectMode::Vertex);
    int   tool = 0;
    bool  xray = false; float xrayAlpha = 0.5f;
    bool  proportional = false, propConnected = false, propProjected = false; int propFalloff = 0; float propSize = 1.0f;
    bool  snapping = false; int snapTarget = 0; int snapBase = 0; bool snapMove = true, snapRotate = false, snapScale = false;
    int   pivot = 3; int orientation = 0;
    bool  autoMerge = false; float mergeThreshold = 0.001f; bool splitEdges = false;
    bool  mirrorX = false, mirrorTopology = false, correctUVs = true;
    bool  faceOrientation = false, statistics = true, edgeLength = false, edgeAngle = false;
    int   normalsDisplay = 0; float normalsSize = 0.1f;
    bool  extrudeDissolve = false;
    float insetThickness = 0.02f, insetDepth = 0.0f; bool insetBoundary = true, insetEdgeRail = false, insetIndividual = false;
    int   bevelWidthType = 0; float bevelWidth = 0.05f; int bevelSegments = 1; float bevelShape = 0.5f; int bevelAffect = 1; bool bevelClamp = true, bevelLoopSlide = true;
    int   loopCuts = 1; float loopSmoothness = 0.0f; bool loopEven = false, loopFlip = false;
    bool  knifeOcclude = true, knifeOnlySelected = false, knifeCutThrough = false; int knifeAngleSnap = 0;
    bool  polyBuildQuads = true;
    int   spinSteps = 9; float spinAngle = 360.0f; bool spinAutoMerge = true;
};

struct RetopoSettings {
    int   tool = 0;
    bool  snapToSource = true; float surfaceOffset = 0.001f;
    int   display = 0;        // Overlay, Hidden Line, X-ray
    float overlayOpacity = 0.6f; bool backfaceCulling = true;
    bool  symX = false, symY = false, symZ = false; float mirrorThreshold = 0.001f;
    bool  autoWeld = true; float weldDistance = 8.0f;   // screen pixels, like Quad Draw
    bool  relaxInterior = true, relaxBorder = false;    // Quad Draw Shift-drag relax
    int   contourCuts = 16; bool contourUniform = true;
    float stripBrushSize = 40.0f;
    int   polyPenMode = 0;    // Tri/Quad, Quad Only, Tri Only, Edge Only
    int   patchFillAngle = 135; bool patchAllowTris = false;
    float brushRadius = 50.0f, brushStrength = 0.5f; int brushFalloff = 0;
    bool  maskBoundary = true, maskSelected = false;
};
