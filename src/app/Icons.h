#pragma once
// Resolution-independent vector icons drawn with ImDrawList on a 24x24 grid (same grid as
// Leorix's SVG icons), so they stay crisp at any DPI and pick up the theme colors.
#include "imgui.h"

enum class Icon {
    None,
    // 2D tools
    Move2D, RectSelect, EllipseSelect, Lasso, MaskSelect, Brush, Pencil, Eraser, Fill, Eyedropper,
    Line, Rect, Ellipse, Pen, Text, Balloon, Pan, Zoom,
    // 3D modes and selection
    ModeObject, ModeSculpt, ModeEdit, ModeRetopo, SelVertex, SelEdge, SelFace, XRay,
    // object / edit tools
    SelectBox, Cursor3D, Move3D, Rotate3D, Scale3D, Transform3D, Extrude, Inset, Bevel, LoopCut, Knife,
    PolyBuild, Spin, SmoothVerts, EdgeSlide, ShrinkFatten, Shear, Rip, Merge,
    // retopology
    QuadDraw, Contours, PolyStrips, Strokes, Patches, PolyPen, Loops, Tweak, Relax, SelectBrush,
    // sculpt brushes
    SculptDraw, SculptDrawSharp, SculptClay, SculptClayStrips, SculptClayThumb, SculptLayer, SculptInflate,
    SculptBlob, SculptCrease, SculptSmooth, SculptFlatten, SculptFill, SculptScrape, SculptMultiScrape,
    SculptPinch, SculptGrab, SculptElastic, SculptSnakeHook, SculptThumb, SculptPose, SculptNudge,
    SculptRotate, SculptSlideRelax, SculptBoundary, SculptCloth, SculptClothFolds, SculptSimplify,
    SculptMask, SculptFaceSets, SculptPaint, SculptSmear, SculptWrinkle, SculptNoise, SculptPolish,
    SculptPlanarTrim, SculptCustomImage,
    // New Project cards and misc
    Card2D, Card3D, CardComic, Card25D, Import,
};

void DrawIcon(ImDrawList* dl, Icon icon, ImVec2 center, float size, ImU32 color);

// Square toolbar button with an icon, brass highlight when active, tooltip on hover.
bool IconButton(const char* id, Icon icon, bool active, const char* tooltip, float size = 36.0f);
