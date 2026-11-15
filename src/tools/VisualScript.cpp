#include "tools/VisualScript.h"
#include "mesh/Primitives.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <random>
#include <unordered_map>

namespace {
using K = WireKind;
const BlockInput kShapeIn{"Shape", K::Shape, 0, 0, 0, "Plug a shape in here."};

const std::vector<BlockInfo>& Table() {
    static const std::vector<BlockInfo> t = {
        // Shapes
        {"Box", "Shapes", "A cube.", "Makes a cube. Size is how long each side is.\nExample: Size 2 makes a box 2 metres wide.", true, K::Shape,
            {{"Size", K::Number, 2, 0.01f, 100, "How long each side is."}}},
        {"Ball", "Shapes", "A round ball.", "Makes a smooth ball. Size is how wide it is from one side to the other.\nExample: Size 1 makes a small marble.", true, K::Shape,
            {{"Size", K::Number, 2, 0.01f, 100, "How wide the ball is."}}},
        {"Tube", "Shapes", "A can or pillar shape.", "Makes a round tube with flat ends, like a can or a pillar.\nExample: Width 0.5, Height 4 makes a thin pillar.", true, K::Shape,
            {{"Width", K::Number, 2, 0.01f, 100, "How wide the tube is."}, {"Height", K::Number, 2, 0.01f, 100, "How tall the tube is."}}},
        {"Cone", "Shapes", "A party-hat shape.", "Makes a cone: round at the bottom, pointy at the top.\nExample: Width 2, Height 3 makes a tree top.", true, K::Shape,
            {{"Width", K::Number, 2, 0.01f, 100, "How wide the bottom is."}, {"Height", K::Number, 2, 0.01f, 100, "How tall the cone is."}}},
        {"Donut", "Shapes", "A ring with a hole.", "Makes a donut (ring) shape.\nExample: Size 2, Thickness 0.3 makes a thin ring.", true, K::Shape,
            {{"Size", K::Number, 2, 0.01f, 100, "How wide the whole ring is."}, {"Thickness", K::Number, 0.5f, 0.01f, 50, "How thick the dough is."}}},
        {"Flat Square", "Shapes", "A flat floor tile.", "Makes a flat square with no thickness, like a floor tile.\nExample: Size 20 makes a big floor.", true, K::Shape,
            {{"Size", K::Number, 2, 0.01f, 1000, "How long each side is."}}},
        // Change
        {"Move", "Change", "Slides a shape somewhere else.", "Moves a shape without changing it.\nExample: Up/Down 3 lifts it 3 metres off the ground.", true, K::Shape,
            {kShapeIn, {"Left/Right", K::Number, 0, -100, 100, "Positive goes right, negative goes left."},
             {"Up/Down", K::Number, 0, -100, 100, "Positive goes up, negative goes down."},
             {"Forward/Back", K::Number, 0, -100, 100, "Positive comes toward you, negative goes away."}}},
        {"Turn", "Change", "Spins a shape around.", "Turns a shape. The numbers are degrees: 90 is a quarter turn, 180 is half.\nExample: Tip 90 lays a standing tube on its side.", true, K::Shape,
            {kShapeIn, {"Spin", K::Number, 0, -360, 360, "Turns around the up direction, like a spinning top."},
             {"Tip", K::Number, 0, -360, 360, "Tips forward or back, like a nodding head."},
             {"Roll", K::Number, 0, -360, 360, "Rolls to the side, like tilting your head."}}},
        {"Resize", "Change", "Makes a shape bigger or smaller.", "Changes a shape's size. 1 means no change, 2 means twice as big, 0.5 means half.\nExample: Taller 3 stretches a box into a tall block.", true, K::Shape,
            {kShapeIn, {"Bigger/Smaller", K::Number, 1, 0.01f, 100, "Grows or shrinks the whole shape."},
             {"Wider", K::Number, 1, 0.01f, 100, "Stretches it left to right."},
             {"Taller", K::Number, 1, 0.01f, 100, "Stretches it up and down."},
             {"Deeper", K::Number, 1, 0.01f, 100, "Stretches it front to back."}}},
        // Repeat
        {"Copy in a Line", "Repeat", "Makes copies in a straight row.", "Makes several copies side by side, left to right.\nExample: 10 copies with Gap 1 makes a fence.", true, K::Shape,
            {kShapeIn, {"How Many", K::Number, 3, 1, kMaxCopies, "How many copies to make."},
             {"Gap", K::Number, 2.5f, 0, 100, "Distance from one copy to the next."}}},
        {"Copy in a Circle", "Repeat", "Makes copies in a ring.", "Places copies evenly around a circle, all facing outward.\nExample: 8 pillars with How Wide 5 makes a stone circle.", true, K::Shape,
            {kShapeIn, {"How Many", K::Number, 8, 1, kMaxCopies, "How many copies to make."},
             {"How Wide", K::Number, 4, 0, 100, "How far the copies are from the middle."}}},
        {"Copy in a Grid", "Repeat", "Makes copies in rows and columns.", "Makes a grid of copies on the ground, like a chessboard.\nExample: 3 rows and 4 columns of boxes makes 12 boxes.", true, K::Shape,
            {kShapeIn, {"Rows", K::Number, 2, 1, kMaxCopies, "How many rows (front to back)."},
             {"Columns", K::Number, 3, 1, kMaxCopies, "How many columns (left to right)."},
             {"Gap", K::Number, 2.5f, 0, 100, "Distance between neighbours."}}},
        // Mix
        {"Put Together", "Mix", "Joins shapes into one.", "Combines up to four shapes into a single model.\nExample: a Tube for a trunk and a Ball moved up for leaves makes a tree.", true, K::Shape,
            {{"Shape 1", K::Shape, 0, 0, 0, "A shape to join."}, {"Shape 2", K::Shape, 0, 0, 0, "A shape to join."},
             {"Shape 3", K::Shape, 0, 0, 0, "A shape to join (optional)."}, {"Shape 4", K::Shape, 0, 0, 0, "A shape to join (optional)."}}},
        // Numbers
        {"Number", "Numbers", "A number you can reuse.", "Holds one number. Wire it into several blocks so one slider changes them all.\nExample: one Number wired into every Size.", true, K::Number,
            {{"Value", K::Number, 1, -100, 100, "The number."}}},
        {"Random Number", "Numbers", "A surprise number.", "Picks a number between Min and Max. The same Seed always gives the same number, so your model won't change by itself.\nExample: change the Seed to try a different look.", true, K::Number,
            {{"Min", K::Number, 0, -100, 100, "The smallest it can be."}, {"Max", K::Number, 10, -100, 100, "The biggest it can be."},
             {"Seed", K::Number, 1, 0, 9999, "Change this for a different random pick."}}},
        {"Math", "Numbers", "Adds, takes away, times or divides.", "Does simple sums with two numbers.\nExample: Number 3 times 2 gives 6.", true, K::Number,
            {{"A", K::Number, 1, -100, 100, "The first number."}, {"B", K::Number, 1, -100, 100, "The second number."}}},
        // Finish
        {"Show in Scene", "Finish", "Whatever you plug in here appears in 3D.", "The end of every recipe. Plug your final shape in, then press Build it!\nYou need exactly one of these.", false, K::Shape,
            {{"Shape", K::Shape, 0, 0, 0, "Your finished shape."}}},
    };
    return t;
}

void Transform(Mesh& m, const std::function<Float3(Float3)>& f) { for (Float3& p : m.positions) p = f(p); }

Float3 RotY(Float3 p, float a) { float c = std::cos(a), s = std::sin(a); return {p.x * c + p.z * s, p.y, -p.x * s + p.z * c}; }
Float3 RotX(Float3 p, float a) { float c = std::cos(a), s = std::sin(a); return {p.x, p.y * c - p.z * s, p.y * s + p.z * c}; }
Float3 RotZ(Float3 p, float a) { float c = std::cos(a), s = std::sin(a); return {p.x * c - p.y * s, p.x * s + p.y * c, p.z}; }

// Appends src to dst keeping polygons and per-corner UVs.
void Append(Mesh& dst, Mesh src) {
    if (dst.positions.empty()) { dst = std::move(src); dst.EnsureUVs(); return; }
    dst.EnsureUVs(); src.EnsureUVs();
    const uint32_t base = (uint32_t)dst.positions.size(), polyBase = dst.polyCount;
    dst.positions.insert(dst.positions.end(), src.positions.begin(), src.positions.end());
    for (Tri t : src.tris) { t.a += base; t.b += base; t.c += base; t.poly += polyBase; dst.tris.push_back(t); }
    dst.loopUVs.insert(dst.loopUVs.end(), src.loopUVs.begin(), src.loopUVs.end());
    dst.polyCount += src.polyCount;
    dst.uvs.clear(); dst.normals.clear();   // per-vertex sets no longer line up; loopUVs carry the UVs
    if (!dst.HasLoopUVs()) dst.loopUVs.clear();
}

struct Eval {
    VisualGraph& g;
    std::unordered_map<int, int> state;     // 1 visiting, 2 done
    std::unordered_map<int, Mesh> meshes;
    std::unordered_map<int, float> numbers;
    std::vector<std::string> notes;
    std::string firstError;

    bool Fail(Block& b, const std::string& msg) {
        if (b.error.empty()) b.error = msg;
        if (firstError.empty()) firstError = std::string(GetBlockInfo(b.type).name) + ": " + msg;
        return false;
    }

    bool Number(Block& b, int input, float& out) {
        if (const Wire* w = g.WireInto(b.id, input)) {
            if (!Run(w->from)) return Fail(b, "Something plugged into this block has a problem.");
            out = numbers[w->from];
        } else out = b.values[input];
        if (!std::isfinite(out)) return Fail(b, "A number here is too big to use.");
        return true;
    }
    bool Shape(Block& b, int input, Mesh& out) {
        const Wire* w = g.WireInto(b.id, input);
        if (!w) return Fail(b, "This block needs a shape plugged into it.");
        if (!Run(w->from)) return Fail(b, "Something plugged into this block has a problem.");
        out = meshes[w->from];
        return true;
    }
    bool Count(Block& b, int input, int& out) {
        float v; if (!Number(b, input, v)) return false;
        int n = (int)std::lround(v);
        if (n < 1) { n = 1; notes.push_back(std::string(GetBlockInfo(b.type).name) + ": you need at least 1 copy, so it uses 1."); }
        if (n > kMaxCopies) { n = kMaxCopies; notes.push_back(std::string(GetBlockInfo(b.type).name) + ": capped at 1000 copies so your computer doesn't freeze."); }
        out = n; return true;
    }
    bool CheckSize(Block& b, size_t verts, size_t copies) {
        if (verts * copies > kMaxVerts) return Fail(b, "That would make a model with over 2 million points. Use fewer copies.");
        return true;
    }

    bool Run(int id) {
        Block* bp = g.Find(id);
        if (!bp) return false;
        Block& b = *bp;
        if (state[id] == 2) return true;
        if (state[id] == 1) return Fail(b, "Loops aren't allowed: a block can't feed into itself.");
        state[id] = 1;
        bool ok = Build(b);
        state[id] = ok ? 2 : 0;
        return ok;
    }

    bool Build(Block& b) {
        float v[4] = {0, 0, 0, 0};
        auto nums = [&](int from, int to) { for (int i = from; i < to; ++i) if (!Number(b, i, v[i])) return false; return true; };
        auto prim = [&](Primitive t, PrimitiveParams p) { meshes[b.id] = BuildPrimitive(t, p); return true; };
        PrimitiveParams p;
        switch (b.type) {
        case BlockType::Box:        if (!nums(0, 1)) return false; p = DefaultParams(Primitive::Cube); p.size = v[0]; return prim(Primitive::Cube, p);
        case BlockType::Ball:       if (!nums(0, 1)) return false; p = DefaultParams(Primitive::UVSphere); p.radius = v[0] / 2; return prim(Primitive::UVSphere, p);
        case BlockType::Tube:       if (!nums(0, 2)) return false; p = DefaultParams(Primitive::Cylinder); p.radius = v[0] / 2; p.depth = v[1]; return prim(Primitive::Cylinder, p);
        case BlockType::Cone:       if (!nums(0, 2)) return false; p = DefaultParams(Primitive::Cone); p.radius = v[0] / 2; p.depth = v[1]; return prim(Primitive::Cone, p);
        case BlockType::Donut:      if (!nums(0, 2)) return false; p = DefaultParams(Primitive::Torus); p.radius2 = v[1] / 2; p.radius = std::max(v[0] / 2 - p.radius2, 0.001f); return prim(Primitive::Torus, p);
        case BlockType::FlatSquare: if (!nums(0, 1)) return false; p = DefaultParams(Primitive::Plane); p.size = v[0]; return prim(Primitive::Plane, p);
        case BlockType::Move: {
            Mesh m; if (!Shape(b, 0, m) || !nums(1, 4)) return false;
            Transform(m, [&](Float3 q) { return Float3{q.x + v[1], q.y + v[2], q.z + v[3]}; });
            meshes[b.id] = std::move(m); return true;
        }
        case BlockType::Turn: {
            Mesh m; if (!Shape(b, 0, m) || !nums(1, 4)) return false;
            const float d = 3.14159265f / 180.0f;
            Transform(m, [&](Float3 q) { return RotY(RotX(RotZ(q, v[3] * d), v[2] * d), v[1] * d); });
            meshes[b.id] = std::move(m); return true;
        }
        case BlockType::Resize: {
            Mesh m; float s[4]; if (!Shape(b, 0, m)) return false;
            for (int i = 0; i < 4; ++i) if (!Number(b, i + 1, s[i])) return false;
            Transform(m, [&](Float3 q) { return Float3{q.x * s[0] * s[1], q.y * s[0] * s[2], q.z * s[0] * s[3]}; });
            meshes[b.id] = std::move(m); return true;
        }
        case BlockType::CopyLine: {
            Mesh m; int n; float gap; if (!Shape(b, 0, m) || !Count(b, 1, n) || !Number(b, 2, gap) || !CheckSize(b, m.VertexCount(), n)) return false;
            Mesh out;
            for (int i = 0; i < n; ++i) { Mesh c = m; Transform(c, [&](Float3 q) { q.x += gap * i; return q; }); Append(out, std::move(c)); }
            meshes[b.id] = std::move(out); return true;
        }
        case BlockType::CopyCircle: {
            Mesh m; int n; float r; if (!Shape(b, 0, m) || !Count(b, 1, n) || !Number(b, 2, r) || !CheckSize(b, m.VertexCount(), n)) return false;
            Mesh out;
            for (int i = 0; i < n; ++i) {
                float a = 6.2831853f * i / n; Mesh c = m;
                Transform(c, [&](Float3 q) { q.z += r; return RotY(q, a); });
                Append(out, std::move(c));
            }
            meshes[b.id] = std::move(out); return true;
        }
        case BlockType::CopyGrid: {
            Mesh m; int rows, cols; float gap;
            if (!Shape(b, 0, m) || !Count(b, 1, rows) || !Count(b, 2, cols) || !Number(b, 3, gap)) return false;
            if (rows * cols > kMaxCopies) { cols = std::max(1, kMaxCopies / rows); notes.push_back("Copy in a Grid: capped at 1000 copies so your computer doesn't freeze."); }
            if (!CheckSize(b, m.VertexCount(), (size_t)rows * cols)) return false;
            Mesh out;
            for (int r = 0; r < rows; ++r) for (int c = 0; c < cols; ++c) {
                Mesh k = m; Transform(k, [&](Float3 q) { q.x += gap * c; q.z += gap * r; return q; }); Append(out, std::move(k));
            }
            meshes[b.id] = std::move(out); return true;
        }
        case BlockType::PutTogether: {
            Mesh out; bool any = false;
            for (int i = 0; i < 4; ++i) {
                if (!g.WireInto(b.id, i)) continue;
                Mesh m; if (!Shape(b, i, m)) return false;
                if (out.VertexCount() + m.VertexCount() > kMaxVerts) return Fail(b, "That would make a model with over 2 million points. Use fewer copies.");
                Append(out, std::move(m)); any = true;
            }
            if (!any) return Fail(b, "Plug at least one shape into this block.");
            meshes[b.id] = std::move(out); return true;
        }
        case BlockType::Number: if (!nums(0, 1)) return false; numbers[b.id] = v[0]; return true;
        case BlockType::Random: {
            if (!nums(0, 3)) return false;
            std::mt19937 rng((uint32_t)std::lround(std::fabs(v[2])));
            numbers[b.id] = v[0] + (v[1] - v[0]) * (float)(rng() / 4294967296.0);
            return true;
        }
        case BlockType::Math: {
            if (!nums(0, 2)) return false;
            if (b.op == 3 && v[1] == 0.0f) return Fail(b, "You can't divide by zero. Change B to something else.");
            numbers[b.id] = b.op == 0 ? v[0] + v[1] : b.op == 1 ? v[0] - v[1] : b.op == 2 ? v[0] * v[1] : v[0] / v[1];
            return true;
        }
        case BlockType::ShowInScene: { Mesh m; if (!Shape(b, 0, m)) return false; meshes[b.id] = std::move(m); return true; }
        default: return Fail(b, "Unknown block.");
        }
    }
};
} // namespace

const BlockInfo& GetBlockInfo(BlockType t) { return Table()[(size_t)t]; }

const char* const* BlockCategories(int& count) {
    static const char* const c[] = {"Shapes", "Change", "Repeat", "Mix", "Numbers", "Finish"};
    count = 6; return c;
}

const char* MathOpName(int op) {
    static const char* const n[] = {"add", "take away", "times", "divide by"};
    return n[std::clamp(op, 0, kMathOpCount - 1)];
}

int VisualGraph::Add(BlockType t, float x, float y) {
    Block b; b.id = nextId++; b.type = t; b.x = x; b.y = y;
    for (const BlockInput& in : GetBlockInfo(t).inputs) b.values.push_back(in.value);
    blocks.push_back(std::move(b));
    return blocks.back().id;
}

Block* VisualGraph::Find(int id) {
    for (Block& b : blocks) if (b.id == id) return &b;
    return nullptr;
}

const Wire* VisualGraph::WireInto(int to, int input) const {
    for (const Wire& w : wires) if (w.to == to && w.input == input) return &w;
    return nullptr;
}

bool VisualGraph::Connect(int from, int to, int input, std::string& why) {
    Block* a = Find(from); Block* b = Find(to);
    if (!a || !b) { why = "That block doesn't exist any more."; return false; }
    const BlockInfo& fromInfo = GetBlockInfo(a->type); const BlockInfo& toInfo = GetBlockInfo(b->type);
    if (!fromInfo.hasOutput) { why = "Show in Scene is the end of the line. Nothing comes out of it."; return false; }
    if (input < 0 || input >= (int)toInfo.inputs.size()) { why = "That slot doesn't exist."; return false; }
    if (fromInfo.output != toInfo.inputs[input].kind) {
        why = fromInfo.output == WireKind::Shape ? "That wire carries a shape, but this slot wants a number."
                                            : "That wire carries a number, but this slot wants a shape.";
        return false;
    }
    // A loop exists if `from` is already downstream of `to`.
    std::vector<int> stack{to}; std::vector<int> seen;
    while (!stack.empty()) {
        int n = stack.back(); stack.pop_back();
        if (n == from) { why = "Loops aren't allowed: a block can't feed into itself."; return false; }
        if (std::find(seen.begin(), seen.end(), n) != seen.end()) continue;
        seen.push_back(n);
        for (const Wire& w : wires) if (w.from == n) stack.push_back(w.to);
    }
    Unplug(to, input);   // one wire per slot: a new wire replaces the old one
    wires.push_back({from, to, input});
    return true;
}

void VisualGraph::Remove(int id) {
    wires.erase(std::remove_if(wires.begin(), wires.end(), [&](const Wire& w) { return w.from == id || w.to == id; }), wires.end());
    blocks.erase(std::remove_if(blocks.begin(), blocks.end(), [&](const Block& b) { return b.id == id; }), blocks.end());
}

void VisualGraph::Unplug(int to, int input) {
    wires.erase(std::remove_if(wires.begin(), wires.end(), [&](const Wire& w) { return w.to == to && w.input == input; }), wires.end());
}

void VisualGraph::LoadStarter() {
    Clear();
    int ball = Add(BlockType::Ball, 40, 60);
    int ring = Add(BlockType::CopyCircle, 300, 60);
    int show = Add(BlockType::ShowInScene, 560, 60);
    std::string why;
    Connect(ball, ring, 0, why);
    Connect(ring, show, 0, why);
}

BuildResult EvaluateGraph(VisualGraph& g) {
    BuildResult r;
    for (Block& b : g.blocks) b.error.clear();
    std::vector<Block*> outs;
    for (Block& b : g.blocks) if (b.type == BlockType::ShowInScene) outs.push_back(&b);
    if (outs.empty()) { r.message = "Add a \"Show in Scene\" block and plug your shape into it."; return r; }
    if (outs.size() > 1) {
        for (size_t i = 1; i < outs.size(); ++i) outs[i]->error = "Only one Show in Scene block is allowed. Remove the extra one.";
        r.message = "You have more than one Show in Scene block. Keep just one.";
        return r;
    }
    Eval e{g};
    if (!e.Run(outs[0]->id)) { r.message = e.firstError.empty() ? "Something went wrong." : e.firstError; r.notes = e.notes; return r; }
    r.mesh = std::move(e.meshes[outs[0]->id]);
    r.mesh.name = "Visual script";
    r.mesh.ComputeNormals();
    r.mesh.ComputeBounds();
    r.ok = true;
    r.notes = e.notes;
    r.message = "Built a model with " + std::to_string(r.mesh.VertexCount()) + " points and " + std::to_string(r.mesh.polyCount) + " faces.";
    return r;
}

bool EvaluateNumber(VisualGraph& g, int id, float& out, std::string& why) {
    for (Block& b : g.blocks) b.error.clear();
    Eval e{g};
    Block* b = g.Find(id);
    if (!b || !GetBlockInfo(b->type).hasOutput || GetBlockInfo(b->type).output != WireKind::Number) { why = "Not a number block."; return false; }
    if (!e.Run(id)) { why = e.firstError; return false; }
    out = e.numbers[id];
    return true;
}
