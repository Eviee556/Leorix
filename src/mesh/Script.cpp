#include "mesh/Script.h"
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <map>
#include <sstream>
#include <vector>

namespace {
const float kPi = 3.14159265358979f;

struct Tokenizer {
    std::vector<std::string> parts;
    std::map<std::string, std::string> kv;
    void parse(const std::string& line) {
        parts.clear(); kv.clear();
        std::istringstream ss(line); std::string tok;
        while (ss >> tok) {
            size_t eq = tok.find('=');
            if (eq != std::string::npos) kv[tok.substr(0, eq)] = tok.substr(eq + 1);
            else parts.push_back(tok);
        }
    }
};

// Expression: numbers, + - * /, parentheses, and the single variable $i. Deliberately tiny.
struct Expr {
    const char* s; std::map<std::string, float>* vars; bool ok = true;
    float parse() { float v = addsub(); return v; }
    void skip() { while (*s == ' ') ++s; }
    float number() {
        skip();
        if (*s == '(') { ++s; float v = addsub(); skip(); if (*s == ')') ++s; return v; }
        if (*s == '$') {
            const char* start = s; ++s; while (std::isalnum((unsigned char)*s) || *s == '_') ++s;
            std::string name(start, s);
            auto it = vars->find(name);
            return it == vars->end() ? 0.0f : it->second;
        }
        if (*s == '-') { ++s; return -number(); }
        char* end; float v = std::strtof(s, &end);
        if (end == s) { ok = false; return 0; }
        s = end; return v;
    }
    float muldiv() {
        float v = number();
        for (;;) { skip();
            if (*s == '*') { ++s; v *= number(); }
            else if (*s == '/') { ++s; float d = number(); v = d != 0 ? v / d : 0; }
            else break;
        }
        return v;
    }
    float addsub() {
        float v = muldiv();
        for (;;) { skip();
            if (*s == '+') { ++s; v += muldiv(); }
            else if (*s == '-') { ++s; v -= muldiv(); }
            else break;
        }
        return v;
    }
};

// Evaluate a token that may be a plain number or a {expression}.
float Eval(const std::string& tok, std::map<std::string, float>& vars) {
    if (!tok.empty() && tok.front() == '{' && tok.back() == '}') {
        std::string inner = tok.substr(1, tok.size() - 2);
        Expr e{inner.c_str(), &vars};
        return e.parse();
    }
    return static_cast<float>(std::atof(tok.c_str()));
}

int EvalInt(const std::string& tok, std::map<std::string, float>& vars, int def) {
    if (tok.empty()) return def;
    return static_cast<int>(std::lround(Eval(tok, vars)));
}
float KV(Tokenizer& t, const std::string& key, std::map<std::string, float>& vars, float def) {
    auto it = t.kv.find(key); return it == t.kv.end() ? def : Eval(it->second, vars);
}
int KVi(Tokenizer& t, const std::string& key, std::map<std::string, float>& vars, int def) {
    auto it = t.kv.find(key); return it == t.kv.end() ? def : EvalInt(it->second, vars, def);
}

// Merge a primitive into the target mesh, remembering the vertex range so transforms only
// touch geometry built since the last primitive/begin.
struct Builder {
    Mesh mesh;
    size_t rangeStart = 0;               // first vertex of the current editable range
    std::vector<uint32_t> manualVerts;   // indices from 'vertex' commands, for 'face'

    void appendPrimitive(const Mesh& p) {
        rangeStart = mesh.pos.size();
        uint32_t base = static_cast<uint32_t>(mesh.pos.size());
        for (const auto& v : p.pos) mesh.pos.push_back(v);
        for (const Face& f : p.faces) {
            std::vector<uint32_t> idx(f.count);
            for (uint32_t i = 0; i < f.count; ++i) idx[i] = base + p.faceVerts[f.start + i];
            mesh.AddFace(idx.data(), f.count);
        }
    }
    void transformRange(const std::function<Vec3(Vec3)>& fn) {
        for (size_t i = rangeStart; i < mesh.pos.size(); ++i) mesh.pos[i] = fn(mesh.pos[i]);
    }
};
} // namespace

ScriptResult RunLeorixScript(const std::string& source) {
    ScriptResult r;
    Builder b;
    std::map<std::string, float> vars;

    // Split into lines, tracking a simple 'repeat ... end' block (one level, which is plenty
    // for parametric meshes and keeps the interpreter safe and predictable).
    std::vector<std::string> lines;
    std::istringstream ss(source); std::string ln;
    while (std::getline(ss, ln)) lines.push_back(ln);

    Tokenizer t;
    auto runLine = [&](const std::string& line, int lineNo) -> bool {
        std::string trimmed = line;
        size_t hash = trimmed.find('#'); if (hash != std::string::npos) trimmed = trimmed.substr(0, hash);
        size_t s = trimmed.find_first_not_of(" \t"); if (s == std::string::npos) return true;
        trimmed = trimmed.substr(s);
        t.parse(trimmed);
        if (t.parts.empty()) return true;
        std::string cmd = t.parts[0];
        for (auto& c : cmd) c = static_cast<char>(std::tolower(c));

        if (cmd == "cube")     { b.appendPrimitive(Primitives::Cube(KV(t, "size", vars, 2))); r.meshesBuilt++; }
        else if (cmd == "plane")    { b.appendPrimitive(Primitives::Plane(KV(t, "size", vars, 2))); r.meshesBuilt++; }
        else if (cmd == "triangle") { b.appendPrimitive(Primitives::Triangle(KV(t, "size", vars, 2))); r.meshesBuilt++; }
        else if (cmd == "circle")   { b.appendPrimitive(Primitives::Circle(KVi(t, "verts", vars, 32), KV(t, "radius", vars, 1), KVi(t, "fill", vars, 1) != 0)); r.meshesBuilt++; }
        else if (cmd == "sphere")   { b.appendPrimitive(Primitives::UVSphere(KVi(t, "segments", vars, 32), KVi(t, "rings", vars, 16), KV(t, "radius", vars, 1))); r.meshesBuilt++; }
        else if (cmd == "icosphere"){ b.appendPrimitive(Primitives::IcoSphere(KVi(t, "subdivisions", vars, 2), KV(t, "radius", vars, 1))); r.meshesBuilt++; }
        else if (cmd == "cylinder") { b.appendPrimitive(Primitives::Cylinder(KVi(t, "verts", vars, 32), KV(t, "radius", vars, 1), KV(t, "depth", vars, 2), KVi(t, "caps", vars, 1) != 0)); r.meshesBuilt++; }
        else if (cmd == "cone")     { b.appendPrimitive(Primitives::Cone(KVi(t, "verts", vars, 32), KV(t, "radius", vars, 1), KV(t, "depth", vars, 2), KVi(t, "cap", vars, 1) != 0)); r.meshesBuilt++; }
        else if (cmd == "torus")    { b.appendPrimitive(Primitives::Torus(KVi(t, "major", vars, 48), KVi(t, "minor", vars, 12), KV(t, "majorradius", vars, 1), KV(t, "minorradius", vars, 0.25f))); r.meshesBuilt++; }
        else if (cmd == "grid")     { b.appendPrimitive(Primitives::Grid(KVi(t, "x", vars, 10), KVi(t, "y", vars, 10), KV(t, "size", vars, 2))); r.meshesBuilt++; }
        else if (cmd == "move") {
            if (t.parts.size() < 4) { r.error = "move needs x y z"; r.line = lineNo; return false; }
            float x = Eval(t.parts[1], vars), y = Eval(t.parts[2], vars), z = Eval(t.parts[3], vars);
            b.transformRange([=](Vec3 p) { return Vec3{p.x + x, p.y + y, p.z + z}; });
        } else if (cmd == "scale") {
            if (t.parts.size() < 4) { r.error = "scale needs x y z"; r.line = lineNo; return false; }
            float x = Eval(t.parts[1], vars), y = Eval(t.parts[2], vars), z = Eval(t.parts[3], vars);
            b.transformRange([=](Vec3 p) { return Vec3{p.x * x, p.y * y, p.z * z}; });
        } else if (cmd == "rotate") {
            if (t.parts.size() < 3) { r.error = "rotate needs axis and degrees"; r.line = lineNo; return false; }
            char axis = static_cast<char>(std::tolower(t.parts[1][0]));
            float a = Eval(t.parts[2], vars) * kPi / 180.0f, c = std::cos(a), sn = std::sin(a);
            b.transformRange([=](Vec3 p) {
                if (axis == 'x') return Vec3{p.x, p.y * c - p.z * sn, p.y * sn + p.z * c};
                if (axis == 'y') return Vec3{p.x * c + p.z * sn, p.y, -p.x * sn + p.z * c};
                return Vec3{p.x * c - p.y * sn, p.x * sn + p.y * c, p.z};
            });
        } else if (cmd == "vertex") {
            if (t.parts.size() < 4) { r.error = "vertex needs x y z"; r.line = lineNo; return false; }
            uint32_t idx = b.mesh.AddVertex({Eval(t.parts[1], vars), Eval(t.parts[2], vars), Eval(t.parts[3], vars)});
            b.manualVerts.push_back(idx);
        } else if (cmd == "face") {
            std::vector<uint32_t> idx;
            for (size_t i = 1; i < t.parts.size(); ++i) idx.push_back(static_cast<uint32_t>(EvalInt(t.parts[i], vars, 0)));
            if (idx.size() >= 3) b.mesh.AddFace(idx.data(), static_cast<uint32_t>(idx.size()));
            else { r.error = "face needs at least 3 indices"; r.line = lineNo; return false; }
        } else if (cmd == "name") {
            if (t.parts.size() > 1) b.mesh.name = t.parts[1];
        } else {
            r.error = "Unknown command: " + cmd; r.line = lineNo; return false;
        }
        return true;
    };

    for (size_t i = 0; i < lines.size(); ++i) {
        std::string trimmed = lines[i];
        size_t hash = trimmed.find('#'); if (hash != std::string::npos) trimmed = trimmed.substr(0, hash);
        size_t nb = trimmed.find_first_not_of(" \t");
        std::string first;
        if (nb != std::string::npos) { std::istringstream ls(trimmed.substr(nb)); ls >> first; for (auto& c : first) c = static_cast<char>(std::tolower(c)); }

        if (first == "repeat") {
            std::istringstream ls(trimmed.substr(nb)); std::string kw; int count = 0; ls >> kw >> count;
            std::vector<std::string> block;
            size_t j = i + 1;
            for (; j < lines.size(); ++j) {
                std::string tl = lines[j];
                size_t h = tl.find('#'); if (h != std::string::npos) tl = tl.substr(0, h);
                size_t b2 = tl.find_first_not_of(" \t"); std::string w;
                if (b2 != std::string::npos) { std::istringstream l2(tl.substr(b2)); l2 >> w; for (auto& c : w) c = static_cast<char>(std::tolower(c)); }
                if (w == "end") break;
                block.push_back(lines[j]);
            }
            count = std::clamp(count, 0, 100000);
            for (int k = 0; k < count; ++k) {
                vars["$i"] = static_cast<float>(k);
                for (const auto& bl : block) if (!runLine(bl, static_cast<int>(i) + 1)) return r;
            }
            i = j; // skip past 'end'
            continue;
        }
        if (first == "end") continue;
        if (!runLine(lines[i], static_cast<int>(i) + 1)) return r;
    }

    if (b.mesh.pos.empty()) { r.error = "The script produced no geometry."; return r; }
    b.mesh.Finalize();
    r.mesh = std::move(b.mesh);
    r.ok = true;
    return r;
}

const char* LeorixScriptExample() {
    return "# A ring of 8 cubes\n"
           "repeat 8\n"
           "  cube size=0.6\n"
           "  move {2*($i)} 0 0\n"
           "  rotate y {$i*45}\n"
           "end\n"
           "sphere radius=1.2 segments=48 rings=24\n";
}

const char* LeorixScriptReference() {
    return "Primitives (clean topology):\n"
           "  cube size=2\n"
           "  plane size=2\n"
           "  triangle size=2\n"
           "  circle verts=32 radius=1 fill=1\n"
           "  sphere segments=32 rings=16 radius=1\n"
           "  icosphere subdivisions=2 radius=1\n"
           "  cylinder verts=32 radius=1 depth=2 caps=1\n"
           "  cone verts=32 radius=1 depth=2 cap=1\n"
           "  torus major=48 minor=12 majorradius=1 minorradius=0.25\n"
           "  grid x=10 y=10 size=2\n"
           "\nTransforms (affect the last primitive):\n"
           "  move x y z\n  rotate axis(x|y|z) degrees\n  scale x y z\n"
           "\nManual geometry:\n  vertex x y z\n  face i0 i1 i2 [i3 ...]\n"
           "\nLoops:\n  repeat N ... end     ($i = 0..N-1)\n"
           "\nExpressions in {}: + - * / ( ) and $i, e.g. move {$i*1.5} 0 0\n";
}
