#pragma once
// Visual Scripting: a beginner node graph ("blocks" joined by "wires") that builds a mesh.
// Pure data + evaluator, no UI, so --selftest can check it headless.
#include "mesh/Mesh.h"
#include <string>
#include <vector>

enum class WireKind { Shape, Number };

enum class BlockType {
    Box, Ball, Tube, Cone, Donut, FlatSquare,          // Shapes
    Move, Turn, Resize,                                // Change
    CopyLine, CopyCircle, CopyGrid,                    // Repeat
    PutTogether,                                       // Mix
    Number, Random, Math,                              // Numbers
    ShowInScene,                                       // Finish
    Count
};

struct BlockInput {
    const char* label;
    WireKind    kind;
    float       value, min, max;   // value is the default used when nothing is plugged in
    const char* help;
};

struct BlockInfo {
    const char* name;
    const char* category;
    const char* blurb;     // one short line, shown on the block
    const char* help;      // longer tooltip with an example
    bool        hasOutput;
    WireKind    output;
    std::vector<BlockInput> inputs;
};

const BlockInfo& GetBlockInfo(BlockType t);
const char* const* BlockCategories(int& count);    // display order of the "Add a block" list
const char* MathOpName(int op);                    // 0 add, 1 take away, 2 times, 3 divide by
constexpr int kMathOpCount = 4;
constexpr int kMaxCopies = 1000;
constexpr size_t kMaxVerts = 2000000;

struct Block {
    int  id = 0;
    BlockType type = BlockType::Box;
    float x = 0, y = 0;                // canvas position (UI only)
    std::vector<float> values;         // one per input, used when that input has no wire
    int  op = 0;                       // Math only
    std::string error;                 // set by Evaluate, shown on the block
};

struct Wire { int from = 0; int to = 0; int input = 0; };  // from's output -> to's input

struct VisualGraph {
    std::vector<Block> blocks;
    std::vector<Wire>  wires;
    int nextId = 1;

    int    Add(BlockType t, float x = 0, float y = 0);
    Block* Find(int id);
    // Returns false and a plain-words reason when the wire is not allowed.
    bool   Connect(int from, int to, int input, std::string& why);
    void   Remove(int id);
    void   Unplug(int to, int input);
    const Wire* WireInto(int to, int input) const;
    void   Clear() { blocks.clear(); wires.clear(); nextId = 1; }
    void   LoadStarter();     // Ball -> Copy in a Circle -> Show in Scene
};

struct BuildResult {
    bool ok = false;
    Mesh mesh;
    std::string message;              // plain-words summary or the first problem
    std::vector<std::string> notes;   // e.g. "capped at 1000 copies"
};

// Builds whatever is plugged into the one Show in Scene block. Fills each block's error.
BuildResult EvaluateGraph(VisualGraph& g);
// Number value of a block's output (for the self-test and the on-block preview).
bool EvaluateNumber(VisualGraph& g, int id, float& out, std::string& why);
