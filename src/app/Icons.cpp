#include "app/Icons.h"
#include <cmath>
#include <initializer_list>

namespace {
const float kPi = 3.14159265f;

struct Pen {
    ImDrawList* dl; ImVec2 o; float s; ImU32 c; float t;
    ImVec2 P(float x, float y) const { return ImVec2(o.x + x * s, o.y + y * s); }
    void line(float x1, float y1, float x2, float y2) const { dl->AddLine(P(x1, y1), P(x2, y2), c, t); }
    void poly(std::initializer_list<float> xy, bool closed = false) const {
        ImVec2 pts[32]; int n = 0;
        for (auto it = xy.begin(); it != xy.end() && n < 32; it += 2) pts[n++] = P(*it, *(it + 1));
        dl->AddPolyline(pts, n, c, closed ? ImDrawFlags_Closed : 0, t);
    }
    void fillPoly(std::initializer_list<float> xy, ImU32 col) const {
        ImVec2 pts[32]; int n = 0;
        for (auto it = xy.begin(); it != xy.end() && n < 32; it += 2) pts[n++] = P(*it, *(it + 1));
        dl->AddConcavePolyFilled(pts, n, col);
    }
    void circle(float x, float y, float r) const { dl->AddCircle(P(x, y), r * s, c, 0, t); }
    void disc(float x, float y, float r) const { dl->AddCircleFilled(P(x, y), r * s, c); }
    void disc(float x, float y, float r, ImU32 col) const { dl->AddCircleFilled(P(x, y), r * s, col); }
    void rect(float x1, float y1, float x2, float y2, float round = 0) const { dl->AddRect(P(x1, y1), P(x2, y2), c, round * s, 0, t); }
    void fillRect(float x1, float y1, float x2, float y2, ImU32 col, float round = 0) const { dl->AddRectFilled(P(x1, y1), P(x2, y2), col, round * s); }
    void arc(float x, float y, float r, float a0, float a1) const {
        dl->PathArcTo(P(x, y), r * s, a0, a1, 0); dl->PathStroke(c, 0, t);
    }
    void bez(float x1, float y1, float cx1, float cy1, float cx2, float cy2, float x2, float y2) const {
        dl->AddBezierCubic(P(x1, y1), P(cx1, cy1), P(cx2, cy2), P(x2, y2), c, t);
    }
    void head(float x, float y, float angleDeg, float len = 3.0f) const { // arrow head at (x,y) pointing along angle
        float a = angleDeg * kPi / 180.0f;
        line(x, y, x - len * std::cos(a - 0.6f), y - len * std::sin(a - 0.6f));
        line(x, y, x - len * std::cos(a + 0.6f), y - len * std::sin(a + 0.6f));
    }
    void arrow(float x1, float y1, float x2, float y2) const {
        line(x1, y1, x2, y2);
        head(x2, y2, std::atan2(y2 - y1, x2 - x1) * 180.0f / kPi);
    }
    void dashed(float x1, float y1, float x2, float y2, int segs = 5) const {
        for (int i = 0; i < segs; i += 1) {
            float a = i / (float)segs, b = (i + 0.55f) / (float)segs;
            line(x1 + (x2 - x1) * a, y1 + (y2 - y1) * a, x1 + (x2 - x1) * b, y1 + (y2 - y1) * b);
        }
    }
    // Sculpt icons share a surface line so the family reads as one set.
    void ground() const { line(2, 19, 22, 19); }
};

ImU32 Fade(ImU32 col, float a) {
    ImU32 alpha = static_cast<ImU32>(((col >> IM_COL32_A_SHIFT) & 0xFF) * a);
    return (col & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
}
} // namespace

void DrawIcon(ImDrawList* dl, Icon icon, ImVec2 center, float size, ImU32 col) {
    float s = size / 24.0f;
    Pen p{dl, ImVec2(center.x - 12 * s, center.y - 12 * s), s, col, std::max(1.2f, 1.6f * s)};
    ImU32 soft = Fade(col, 0.28f), mid = Fade(col, 0.55f);

    switch (icon) {
    case Icon::None: break;
    // ------------------------------------------------ 2D tools
    case Icon::Move2D:
        p.arrow(12, 12, 12, 3); p.arrow(12, 12, 12, 21); p.arrow(12, 12, 3, 12); p.arrow(12, 12, 21, 12); break;
    case Icon::RectSelect: p.dashed(4, 5, 20, 5, 4); p.dashed(20, 5, 20, 19, 4); p.dashed(20, 19, 4, 19, 4); p.dashed(4, 19, 4, 5, 4); break;
    case Icon::EllipseSelect:
        for (int i = 0; i < 12; i += 2) p.arc(12, 12, 8, i * kPi / 6, (i + 1) * kPi / 6); break;
    case Icon::Lasso:
        p.bez(12, 4, 4, 4, 3, 13, 9, 14); p.bez(12, 4, 20, 4, 21, 12, 13, 14); p.line(9, 14, 13, 14);
        p.bez(10, 14, 8, 17, 10, 20, 7, 21); break;
    case Icon::MaskSelect: p.dashed(4, 4, 20, 4, 4); p.dashed(20, 4, 20, 20, 4); p.dashed(20, 20, 4, 20, 4); p.dashed(4, 20, 4, 4, 4); p.disc(12, 12, 4); break;
    case Icon::Brush:
        p.poly({14, 10, 19, 5, 21, 7, 16, 12}, true); p.bez(14, 10, 10, 11, 5, 14, 4, 20); p.bez(4, 20, 9, 19, 13, 16, 16, 12); break;
    case Icon::Pencil:
        p.poly({16.5f, 3.5f, 20.5f, 7.5f, 8, 20, 3, 21, 4, 16}, true); p.line(14, 6, 18, 10); break;
    case Icon::Eraser:
        p.poly({4, 15, 12, 7, 19, 14, 13, 20, 8, 20}, true); p.line(8, 11, 15, 18); p.line(13, 20, 21, 20); break;
    case Icon::Fill:
        p.poly({3, 12, 11, 4, 19, 12, 11, 20}, true); p.fillPoly({5, 14, 17, 14, 11, 20}, mid); p.bez(19, 12, 22, 15, 22, 18, 20, 19); break;
    case Icon::Eyedropper:
        p.poly({15, 4, 20, 9, 11, 18, 7, 19, 6, 18, 7, 14}, true); p.line(13, 6, 18, 11); p.line(6, 18, 3, 21); break;
    case Icon::Line: p.line(4, 20, 20, 4); p.disc(4, 20, 1.6f); p.disc(20, 4, 1.6f); break;
    case Icon::Rect: p.rect(4, 6, 20, 18, 1); break;
    case Icon::Ellipse: p.circle(12, 12, 8); break;
    case Icon::Pen: p.bez(5, 18, 9, 10, 14, 7, 19, 6); p.disc(5, 18, 1.8f); p.disc(19, 6, 1.8f); p.line(19, 6, 22, 3); p.disc(12, 9, 1.2f, mid); break;
    case Icon::Text: p.line(5, 5, 19, 5); p.line(12, 5, 12, 19); p.line(9, 19, 15, 19); break;
    case Icon::Balloon:
        p.bez(12, 5, 20, 5, 21, 14, 13, 15); p.bez(12, 5, 4, 5, 3, 14, 10, 15); p.poly({10, 15, 8, 20, 13, 15}); break;
    case Icon::Pan:
        p.line(8, 12, 8, 5); p.line(11, 11, 11, 3.5f); p.line(14, 11, 14, 4.5f); p.line(17, 12, 17, 7);
        p.bez(8, 12, 5, 11, 4, 13, 6, 15); p.bez(6, 15, 9, 20, 11, 21, 14, 21); p.bez(14, 21, 17, 21, 17, 17, 17, 12); break;
    case Icon::Zoom: p.circle(10.5f, 10.5f, 6.5f); p.line(15.5f, 15.5f, 21, 21); p.line(8, 10.5f, 13, 10.5f); p.line(10.5f, 8, 10.5f, 13); break;

    // ------------------------------------------------ 3D modes and selection
    case Icon::ModeObject:
        p.poly({12, 3, 20, 7.5f, 20, 16.5f, 12, 21, 4, 16.5f, 4, 7.5f}, true); p.line(12, 12, 20, 7.5f); p.line(12, 12, 4, 7.5f); p.line(12, 12, 12, 21); break;
    case Icon::ModeSculpt:
        p.arc(12, 14, 8, kPi, 2 * kPi); p.line(4, 14, 20, 14); p.bez(8, 9, 10, 5, 14, 5, 16, 9); p.line(12, 2, 12, 6); p.head(12, 6, 90, 2.5f); break;
    case Icon::ModeEdit:
        p.poly({12, 3, 20, 7.5f, 20, 16.5f, 12, 21, 4, 16.5f, 4, 7.5f}, true); p.line(12, 12, 20, 7.5f); p.line(12, 12, 4, 7.5f); p.line(12, 12, 12, 21);
        for (auto v : {std::pair{12.f, 3.f}, {20.f, 7.5f}, {20.f, 16.5f}, {12.f, 21.f}, {4.f, 16.5f}, {4.f, 7.5f}, {12.f, 12.f}}) p.disc(v.first, v.second, 1.7f);
        break;
    case Icon::ModeRetopo:
        p.circle(12, 12, 9); p.line(3, 12, 21, 12); p.bez(12, 3, 7, 7, 7, 17, 12, 21); p.bez(12, 3, 17, 7, 17, 17, 12, 21);
        p.fillRect(13, 13, 18, 18, mid); break;
    case Icon::SelVertex: p.rect(4, 4, 20, 20); p.disc(4, 4, 2.6f); p.disc(20, 4, 2.6f); p.disc(20, 20, 2.6f); p.disc(4, 20, 2.6f); break;
    case Icon::SelEdge: p.rect(4, 4, 20, 20); { Pen q = p; q.t *= 2.2f; q.line(4, 4, 20, 4); } break;
    case Icon::SelFace: p.fillRect(4, 4, 20, 20, mid); p.rect(4, 4, 20, 20); break;
    case Icon::XRay: p.rect(3, 6, 15, 18); p.fillRect(9, 3, 21, 15, soft); p.rect(9, 3, 21, 15); break;

    // ------------------------------------------------ object / edit tools
    case Icon::SelectBox: p.dashed(3, 3, 17, 3, 4); p.dashed(3, 3, 3, 17, 4); p.poly({11, 11, 20, 14.5f, 16, 16, 14.5f, 20}, true); break;
    case Icon::Cursor3D: p.circle(12, 12, 6); p.line(12, 2, 12, 8); p.line(12, 16, 12, 22); p.line(2, 12, 8, 12); p.line(16, 12, 22, 12); break;
    case Icon::Move3D: p.arrow(12, 12, 12, 3); p.arrow(12, 12, 21, 12); p.arrow(12, 12, 5, 19); p.disc(12, 12, 1.8f); break;
    case Icon::Rotate3D: p.arc(12, 12, 8, -kPi * 0.2f, kPi * 1.45f); p.head(17.5f, 6.5f, -40); p.disc(12, 12, 1.8f); break;
    case Icon::Scale3D: p.rect(4, 11, 13, 20); p.rect(4, 4, 20, 20); p.arrow(11, 13, 19, 5); break;
    case Icon::Transform3D: p.arrow(12, 12, 12, 3); p.arrow(12, 12, 21, 12); p.arc(12, 12, 6, kPi * 0.55f, kPi * 1.35f); p.fillRect(4, 17, 8, 21, mid); break;
    case Icon::Extrude: p.poly({3, 16, 13, 16, 19, 12, 9, 12}, true); p.fillPoly({3, 9, 13, 9, 19, 5, 9, 5}, mid); p.arrow(11, 14, 11, 6); break;
    case Icon::Inset: p.rect(3, 3, 21, 21); p.fillRect(8, 8, 16, 16, mid); p.rect(8, 8, 16, 16); p.line(3, 3, 8, 8); p.line(21, 3, 16, 8); p.line(21, 21, 16, 16); p.line(3, 21, 8, 16); break;
    case Icon::Bevel: p.poly({3, 21, 3, 11, 11, 3, 21, 3}); p.fillPoly({3, 11, 11, 3, 13, 5, 5, 13}, mid); break;
    case Icon::LoopCut: p.rect(3, 5, 21, 19); { Pen q = p; q.c = col; q.dashed(12, 3, 12, 21, 4); } p.head(12, 21, 90, 2.5f); break;
    case Icon::Knife: p.poly({3, 20, 16, 7, 20, 3, 17, 10}, true); p.dashed(8, 21, 21, 21, 4); break;
    case Icon::PolyBuild: p.poly({4, 18, 10, 6, 20, 9}); p.dashed(20, 9, 17, 20, 3); p.dashed(17, 20, 4, 18, 3); p.disc(17, 20, 2.2f); break;
    case Icon::Spin: p.arc(12, 12, 8, kPi * 0.8f, kPi * 2.2f); p.head(18.4f, 16.8f, 110); p.disc(12, 12, 1.8f); p.line(12, 12, 4.5f, 9.5f); break;
    case Icon::SmoothVerts: p.poly({3, 16, 7, 8, 11, 18, 15, 7, 21, 15}); p.bez(3, 12, 8, 9, 16, 9, 21, 12); break;
    case Icon::EdgeSlide: p.rect(3, 6, 21, 18); p.line(10, 6, 14, 18); p.arrow(13, 12, 20, 12); p.arrow(11, 12, 4, 12); break;
    case Icon::ShrinkFatten: p.circle(12, 12, 5); p.arrow(12, 7, 12, 2); p.arrow(17, 12, 22, 12); p.arrow(12, 17, 12, 22); p.arrow(7, 12, 2, 12); break;
    case Icon::Shear: p.poly({7, 19, 3, 5, 17, 5, 21, 19}, true); p.arrow(8, 3, 16, 3); break;
    case Icon::Rip: p.poly({3, 5, 12, 5, 9, 12, 12, 19, 3, 19}, true); p.poly({21, 5, 15, 5, 12, 12, 15, 19, 21, 19}); break;
    case Icon::Merge: p.arrow(3, 4, 10, 11); p.arrow(21, 4, 14, 11); p.arrow(12, 21, 12, 14); p.disc(12, 12, 2.2f); break;

    // ------------------------------------------------ retopology
    case Icon::QuadDraw:
        p.bez(2, 20, 7, 10, 17, 10, 22, 20); p.poly({6, 13, 11, 11, 12, 16, 7, 17}, true); p.fillPoly({6, 13, 11, 11, 12, 16, 7, 17}, soft);
        p.disc(15, 11, 1.8f); p.disc(17, 16, 1.8f); break;
    case Icon::Contours: p.line(6, 3, 6, 21); p.line(18, 3, 18, 21);
        for (float y : {7.f, 12.f, 17.f}) { p.arc(12, y, 6, 0, kPi); } break;
    case Icon::PolyStrips: p.bez(3, 18, 8, 6, 15, 18, 21, 6);
        for (float x : {6.f, 10.f, 14.f, 18.f}) p.line(x - 1.5f, 10 + (x - 3) * 0.1f, x + 1.5f, 16 - (x - 3) * 0.1f); break;
    case Icon::Strokes: p.bez(3, 6, 9, 3, 15, 9, 21, 6); p.bez(3, 18, 9, 15, 15, 21, 21, 18); p.line(8, 5, 8, 17); p.line(16, 7, 16, 19); break;
    case Icon::Patches: p.poly({4, 6, 12, 3, 20, 7, 19, 17, 11, 21, 4, 16}, true); p.line(8, 5, 7, 18); p.line(16, 5, 15, 19); p.line(4, 11, 20, 12); break;
    case Icon::PolyPen: p.poly({4, 19, 4, 9, 13, 5}); p.dashed(13, 5, 19, 14, 3); p.disc(19, 14, 2.2f); p.poly({17, 20, 21, 16, 22, 21}, true); break;
    case Icon::Loops: p.rect(3, 4, 21, 20); p.line(12, 4, 12, 20); p.arc(12, 12, 4, -kPi * 0.5f, kPi * 0.5f); p.head(12, 16, 180, 2.2f); break;
    case Icon::Tweak: p.circle(12, 12, 8); p.disc(12, 12, 2); p.arrow(12, 12, 20, 5); break;
    case Icon::Relax: p.circle(12, 12, 8);
        for (float a = 0; a < 6; a += 1) p.disc(12 + 5 * std::cos(a * kPi / 3), 12 + 5 * std::sin(a * kPi / 3), 1.3f); p.disc(12, 12, 1.3f); break;
    case Icon::SelectBrush: p.circle(12, 12, 8); p.dashed(4, 20, 20, 4, 5); p.disc(12, 12, 1.8f); break;

    // ------------------------------------------------ sculpt brushes (all on a shared surface line)
    case Icon::SculptDraw: p.ground(); p.bez(4, 19, 9, 19, 9, 11, 12, 11); p.bez(12, 11, 15, 11, 15, 19, 20, 19); p.arrow(12, 9, 12, 3); break;
    case Icon::SculptDrawSharp: p.ground(); p.poly({4, 19, 10, 17, 12, 6, 14, 17, 20, 19}); break;
    case Icon::SculptClay: p.ground(); p.poly({3, 19, 7, 12, 17, 12, 21, 19}); p.line(7, 9, 17, 9); p.line(12, 9, 12, 4); break;
    case Icon::SculptClayStrips: p.ground(); p.fillRect(4, 13, 10, 19, mid); p.fillRect(9, 10, 15, 19, col); p.fillRect(14, 14, 20, 19, mid); break;
    case Icon::SculptClayThumb: p.ground(); p.bez(3, 19, 6, 10, 18, 10, 21, 19); p.bez(8, 13, 10, 16, 14, 16, 16, 13); p.circle(12, 6, 3); break;
    case Icon::SculptLayer: p.ground(); p.poly({3, 19, 5, 13, 19, 13, 21, 19}); p.dashed(5, 10, 19, 10, 4); break;
    case Icon::SculptInflate: p.circle(12, 12, 4.5f); p.arrow(12, 7, 12, 2); p.arrow(17, 12, 22, 12); p.arrow(12, 17, 12, 22); p.arrow(7, 12, 2, 12); break;
    case Icon::SculptBlob: p.ground(); p.disc(12, 13, 5.5f, mid); p.circle(12, 13, 5.5f); p.line(12, 5, 12, 2); break;
    case Icon::SculptCrease: p.poly({2, 11, 9, 11, 12, 20, 15, 11, 22, 11}); p.arrow(6, 5, 10.5f, 9.5f); p.arrow(18, 5, 13.5f, 9.5f); break;
    case Icon::SculptSmooth: p.poly({3, 14, 6, 9, 9, 16, 12, 8, 15, 15, 18, 10, 21, 14}); p.bez(3, 19, 9, 17, 15, 17, 21, 19); break;
    case Icon::SculptFlatten: p.poly({3, 19, 6, 11, 10, 15, 14, 8, 18, 14, 21, 19}); p.line(2, 12, 22, 12); break;
    case Icon::SculptFill: p.ground(); p.poly({3, 11, 8, 17, 16, 17, 21, 11}); p.fillPoly({5, 13, 19, 13, 16, 17, 8, 17}, mid); p.line(2, 11, 22, 11); break;
    case Icon::SculptScrape: p.ground(); p.poly({3, 19, 7, 12, 17, 12, 21, 19}); p.dashed(7, 12, 10, 6, 2); p.dashed(17, 12, 14, 6, 2); p.line(2, 12, 22, 12); break;
    case Icon::SculptMultiScrape: p.ground(); p.poly({3, 19, 12, 9, 21, 19}); p.line(2, 14, 12, 5); p.line(12, 5, 22, 14); break;
    case Icon::SculptPinch: p.arrow(3, 12, 9, 12); p.arrow(21, 12, 15, 12); p.arrow(12, 3, 12, 9); p.arrow(12, 21, 12, 15); p.disc(12, 12, 1.6f); break;
    case Icon::SculptGrab: p.ground(); p.bez(4, 19, 8, 19, 9, 10, 13, 9); p.bez(13, 9, 16, 9, 16, 19, 20, 19); p.arrow(13, 9, 20, 4); p.disc(13, 9, 1.8f); break;
    case Icon::SculptElastic: p.circle(12, 12, 8); p.circle(12, 12, 4); p.arrow(12, 12, 21, 4); break;
    case Icon::SculptSnakeHook: p.ground(); p.bez(9, 19, 9, 10, 13, 5, 20, 5); p.bez(15, 19, 14, 12, 16, 9, 20, 9); p.head(20, 7, 0, 3); break;
    case Icon::SculptThumb: p.ground(); p.bez(3, 19, 7, 13, 13, 13, 19, 19); p.arrow(6, 9, 18, 9); break;
    case Icon::SculptPose: p.disc(6, 17, 2.4f); p.disc(12, 11, 2.4f); p.disc(19, 5, 2.4f); p.line(6, 17, 12, 11); p.line(12, 11, 19, 5); p.arc(12, 11, 6, kPi * 0.1f, kPi * 0.6f); break;
    case Icon::SculptNudge: p.ground(); p.bez(4, 19, 8, 13, 13, 13, 16, 19); p.arrow(4, 9, 13, 9); p.arrow(8, 13, 17, 13); break;
    case Icon::SculptRotate: p.circle(12, 12, 3); p.arc(12, 12, 8, -kPi * 0.1f, kPi * 1.4f); p.head(19.8f, 10.5f, -80); break;
    case Icon::SculptSlideRelax: p.ground(); for (float x : {5.f, 10.f, 15.f}) p.disc(x, 15, 1.5f); p.arrow(4, 9, 20, 9); break;
    case Icon::SculptBoundary: p.poly({4, 19, 4, 5, 20, 5}); p.dashed(20, 5, 20, 19, 4); p.dashed(20, 19, 4, 19, 4); p.arrow(12, 9, 12, 2); p.fillRect(4, 5, 20, 7, mid); break;
    case Icon::SculptCloth: p.poly({3, 5, 21, 5}); p.bez(3, 5, 3, 12, 6, 16, 4, 20); p.bez(21, 5, 21, 12, 18, 16, 20, 20); p.bez(4, 20, 9, 17, 15, 22, 20, 20); break;
    case Icon::SculptClothFolds: p.line(3, 4, 21, 4); p.poly({5, 4, 4, 20}); p.poly({19, 4, 20, 20}); p.poly({8, 6, 7, 19}); p.poly({12, 6, 12, 20}); p.poly({16, 6, 17, 19});
        p.poly({4, 20, 7, 19, 12, 20, 17, 19, 20, 20}); break;
    case Icon::SculptSimplify: p.poly({4, 18, 12, 5, 20, 18}, true); p.line(8, 11.5f, 16, 11.5f); p.line(12, 5, 12, 18); break;
    case Icon::SculptMask: p.circle(12, 12, 8); p.dl->PathArcTo(p.P(12, 12), 8 * s, kPi * 0.5f, kPi * 1.5f, 0); p.dl->PathFillConvex(mid); break;
    case Icon::SculptFaceSets: p.fillRect(3, 3, 11, 11, col, 1); p.fillRect(13, 3, 21, 11, mid, 1); p.fillRect(3, 13, 11, 21, mid, 1); p.fillRect(13, 13, 21, 21, soft, 1); break;
    case Icon::SculptPaint: p.bez(12, 3, 6, 11, 5, 15, 8, 18); p.bez(12, 3, 18, 11, 19, 15, 16, 18); p.bez(8, 18, 10, 21, 14, 21, 16, 18); p.disc(12, 14, 3, mid); break;
    case Icon::SculptSmear: p.bez(9, 4, 4, 11, 4, 15, 7, 17); p.bez(9, 4, 14, 11, 14, 15, 11, 17); p.line(7, 17, 11, 17); p.line(13, 14, 21, 14); p.line(12, 18, 21, 18); break;
    case Icon::SculptWrinkle: for (float y : {6.f, 11.f, 16.f}) p.bez(3, y, 9, y - 3, 15, y + 3, 21, y); break;
    case Icon::SculptNoise: p.ground(); p.poly({3, 17, 5, 13, 6, 16, 8, 11, 10, 15, 11, 12, 13, 16, 14, 10, 16, 15, 18, 12, 19, 16, 21, 14}); break;
    case Icon::SculptPolish: p.ground(); p.poly({3, 19, 6, 12, 18, 12, 21, 19}); p.line(16, 3, 16, 9); p.line(13, 6, 19, 6); break;
    case Icon::SculptPlanarTrim: p.ground(); p.bez(3, 19, 6, 6, 18, 6, 21, 19); p.line(2, 4, 22, 14); break;
    case Icon::SculptCustomImage: p.rect(3, 4, 21, 20, 1.5f); p.poly({4, 18, 9, 11, 13, 15, 16, 12, 20, 18}); p.disc(16, 8, 1.8f); break;

    // ------------------------------------------------ New Project cards, misc
    case Icon::Card2D: p.rect(3, 4, 21, 20, 2); p.poly({3, 15, 7, 11, 10, 14, 14, 9, 21, 16}); break;
    case Icon::Card3D:
        p.poly({12, 2, 21, 7, 21, 17, 12, 22, 3, 17, 3, 7}, true); p.line(12, 12, 21, 7); p.line(12, 12, 3, 7); p.line(12, 12, 12, 22); break;
    case Icon::CardComic: p.rect(3, 3, 21, 21, 2); p.line(3, 10, 21, 10); p.line(11, 10, 11, 21); break;
    case Icon::Card25D: p.poly({3, 8, 12, 3, 21, 8, 12, 13}, true); p.poly({3, 13, 12, 18, 21, 13}); p.poly({3, 18, 12, 21, 21, 18}); break;
    case Icon::Import: p.arrow(12, 3, 12, 15); p.poly({4, 14, 4, 20, 20, 20, 20, 14}); break;
    }
}

bool IconButton(const char* id, Icon icon, bool active, const char* tooltip, float size) {
    const ImVec4 brass(0.79f, 0.54f, 0.29f, 1.0f);
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.79f, 0.54f, 0.29f, 0.18f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.54f, 0.37f, 0.22f, 1.0f));
    }
    bool pressed = ImGui::Button(id, ImVec2(size, size));
    if (active) ImGui::PopStyleColor(2);
    ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    ImU32 col = active ? ImGui::GetColorU32(brass)
                       : ImGui::GetColorU32(ImGui::IsItemHovered() ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    DrawIcon(ImGui::GetWindowDrawList(), icon, ImVec2((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f), size * 0.56f, col);
    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", tooltip);
    return pressed;
}
