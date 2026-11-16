#include "tools/Script.h"
#include "mesh/Primitives.h"
#include "mesh/Curve.h"
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>
#include <unordered_set>

// Script results live here for the duration of one run (single-threaded UI use).
namespace {
constexpr int    kHookEvery     = 1000000;     // instructions between hook calls
constexpr int    kMaxHookCalls  = 100;         // -> ~100M instructions per run
constexpr size_t kMemoryLimit   = size_t(512) << 20;

struct ScriptCtx { ScriptResult* result; int hookCalls = 0; ScriptMaterial material; };
ScriptCtx* Ctx(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "leorix_ctx");
    auto* c = static_cast<ScriptCtx*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return c;
}

// ---- userdata mesh wrapper for manual building
struct ScriptMesh { Mesh mesh; uint32_t poly; };
const char* kMeshMT = "leorix.mesh";

int l_print(lua_State* L) {
    ScriptCtx* c = Ctx(L);
    int n = lua_gettop(L);
    for (int i = 1; i <= n; ++i) {
        size_t len; const char* s = luaL_tolstring(L, i, &len);
        if (i > 1) c->result->output += '\t';
        c->result->output.append(s, len);
        lua_pop(L, 1);
    }
    c->result->output += '\n';
    return 0;
}

// Every mesh records the material() that was active when it was made.
void AddMesh(lua_State* L, const Mesh& m) {
    ScriptCtx* c = Ctx(L);
    c->result->meshes.push_back(m);
    c->result->materials.push_back(c->material);
}

void PushPrimitive(lua_State* L, const Mesh& m) {
    AddMesh(L, m);
    lua_pushinteger(L, static_cast<lua_Integer>(m.VertexCount()));
    lua_pushinteger(L, static_cast<lua_Integer>(m.TriCount()));
}

int l_cube(lua_State* L)   { PrimitiveParams p; p.size = (float)luaL_optnumber(L, 1, 2.0); PushPrimitive(L, BuildPrimitive(Primitive::Cube, p)); return 2; }
int l_plane(lua_State* L)  { PrimitiveParams p; p.size = (float)luaL_optnumber(L, 1, 2.0); PushPrimitive(L, BuildPrimitive(Primitive::Plane, p)); return 2; }
int l_sphere(lua_State* L) { PrimitiveParams p; p.radius = (float)luaL_optnumber(L, 1, 1.0); p.verticesRing = (int)luaL_optinteger(L, 2, 32); p.rings = (int)luaL_optinteger(L, 3, 16); PushPrimitive(L, BuildPrimitive(Primitive::UVSphere, p)); return 2; }
int l_ico(lua_State* L)    { PrimitiveParams p; p.radius = (float)luaL_optnumber(L, 1, 1.0); p.subdivisions = (int)luaL_optinteger(L, 2, 2); PushPrimitive(L, BuildPrimitive(Primitive::IcoSphere, p)); return 2; }
int l_cylinder(lua_State* L){ PrimitiveParams p; p.radius = (float)luaL_optnumber(L, 1, 1.0); p.depth = (float)luaL_optnumber(L, 2, 2.0); p.verticesRing = (int)luaL_optinteger(L, 3, 32); PushPrimitive(L, BuildPrimitive(Primitive::Cylinder, p)); return 2; }
int l_cone(lua_State* L)   { PrimitiveParams p; p.radius = (float)luaL_optnumber(L, 1, 1.0); p.depth = (float)luaL_optnumber(L, 2, 2.0); p.verticesRing = (int)luaL_optinteger(L, 3, 32); PushPrimitive(L, BuildPrimitive(Primitive::Cone, p)); return 2; }
int l_torus(lua_State* L)  { PrimitiveParams p; p.radius = (float)luaL_optnumber(L, 1, 1.0); p.radius2 = (float)luaL_optnumber(L, 2, 0.25); p.verticesRing = (int)luaL_optinteger(L, 3, 48); p.rings = (int)luaL_optinteger(L, 4, 12); PushPrimitive(L, BuildPrimitive(Primitive::Torus, p)); return 2; }
int l_circle(lua_State* L) { PrimitiveParams p; p.radius = (float)luaL_optnumber(L, 1, 1.0); p.verticesRing = (int)luaL_optinteger(L, 2, 32); PushPrimitive(L, BuildPrimitive(Primitive::Circle, p)); return 2; }
int l_grid(lua_State* L)   { PrimitiveParams p; p.gridX = (int)luaL_optinteger(L, 1, 10); p.gridY = (int)luaL_optinteger(L, 2, 10); p.size = (float)luaL_optnumber(L, 3, 2.0); PushPrimitive(L, BuildPrimitive(Primitive::Grid, p)); return 2; }

// ---- manual mesh
int l_mesh_new(lua_State* L) {
    void* ud = lua_newuserdatauv(L, sizeof(ScriptMesh), 0);
    new (ud) ScriptMesh();
    luaL_getmetatable(L, kMeshMT);
    lua_setmetatable(L, -2);
    return 1;
}
ScriptMesh* CheckMesh(lua_State* L) { return static_cast<ScriptMesh*>(luaL_checkudata(L, 1, kMeshMT)); }
int l_mesh_vert(lua_State* L) {
    ScriptMesh* m = CheckMesh(L);
    float x = (float)luaL_checknumber(L, 2), y = (float)luaL_checknumber(L, 3), z = (float)luaL_checknumber(L, 4);
    m->mesh.positions.push_back({x, y, z});
    lua_pushinteger(L, static_cast<lua_Integer>(m->mesh.positions.size() - 1)); // 0-based index
    return 1;
}
int l_mesh_face(lua_State* L) {
    ScriptMesh* m = CheckMesh(L);
    int n = lua_gettop(L) - 1;
    if (n < 3) return luaL_error(L, "face() needs at least 3 vertices");
    std::vector<uint32_t> idx(n);
    const lua_Integer count = (lua_Integer)m->mesh.positions.size();
    std::unordered_set<uint32_t> distinct;
    for (int i = 0; i < n; ++i) {
        lua_Integer v = luaL_checkinteger(L, i + 2);
        if (v < 0 || v >= count)
            return luaL_error(L, "face(): vertex index %I does not exist (this mesh has %I vertices, numbered 0 to %I)",
                              v, count, count - 1);
        idx[i] = (uint32_t)v;
        distinct.insert(idx[i]);
    }
    if (distinct.size() < 3) return luaL_error(L, "face() needs at least 3 different vertices");
    for (int i = 1; i + 1 < n; ++i) m->mesh.tris.push_back({idx[0], idx[i], idx[i + 1], m->poly});
    ++m->poly;
    return 0;
}
int l_mesh_build(lua_State* L) {
    ScriptMesh* m = CheckMesh(L);
    if (m->mesh.positions.empty() || m->mesh.tris.empty()) return luaL_error(L, "mesh has no geometry to build");
    m->mesh.polyCount = m->poly;
    m->mesh.ComputeBounds();
    m->mesh.ComputeNormals();
    m->mesh.name = "Script mesh";
    AddMesh(L, m->mesh);
    return 0;
}
int l_mesh_gc(lua_State* L) { CheckMesh(L)->~ScriptMesh(); return 0; }

// ---- look: material{}, camera{}, world{}, curve()
float Num(lua_State* L, int t, const char* k, float d, float lo = -1e6f, float hi = 1e6f) {
    lua_getfield(L, t, k);
    float v = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : d;
    lua_pop(L, 1);
    return std::isfinite(v) ? std::fmin(std::fmax(v, lo), hi) : d;
}
// Reads {a, b, c} at t[k] into out (left alone if missing).
void Vec3Field(lua_State* L, int t, const char* k, float* out, float lo = -1e6f, float hi = 1e6f) {
    lua_getfield(L, t, k);
    if (lua_istable(L, -1))
        for (int i = 0; i < 3; ++i) {
            lua_rawgeti(L, -1, i + 1);
            if (lua_isnumber(L, -1)) { float v = (float)lua_tonumber(L, -1); if (std::isfinite(v)) out[i] = std::fmin(std::fmax(v, lo), hi); }
            lua_pop(L, 1);
        }
    lua_pop(L, 1);
}
bool Has(lua_State* L, int t, const char* k) { lua_getfield(L, t, k); bool h = !lua_isnil(L, -1); lua_pop(L, 1); return h; }

int l_material(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    ScriptMaterial m;   // unspecified fields go back to the defaults
    Vec3Field(L, 1, "color", m.color, 0, 1);
    m.roughness = Num(L, 1, "roughness", m.roughness, 0, 1);
    m.metallic = Num(L, 1, "metallic", m.metallic, 0, 1);
    Vec3Field(L, 1, "emission", m.emission, 0, 1);
    m.strength = Num(L, 1, "strength", m.strength, 0, 1000);
    if (Has(L, 1, "glass")) { m.transmission = 1.0f; m.ior = Num(L, 1, "glass", 1.45f, 1.0f, 3.0f); }
    lua_getfield(L, 1, "smooth"); m.smooth = lua_toboolean(L, -1); lua_pop(L, 1);
    Ctx(L)->material = m;
    return 0;
}

int l_camera(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    ScriptView& v = Ctx(L)->result->view;
    v.hasCamera = true;
    Vec3Field(L, 1, "eye", &v.eye.x);
    Vec3Field(L, 1, "target", &v.target.x);
    v.lens = Num(L, 1, "lens", v.lens, 8, 300);
    v.fStop = Num(L, 1, "fstop", 0, 0, 64);
    v.focus = Num(L, 1, "focus", 0, 0, 1e5f);
    return 0;
}

int l_world(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    ScriptView& v = Ctx(L)->result->view;
    static const char* names[] = {"StudioSoftbox", "NoonSky", "GoldenHour", "Overcast", "Night",
                                  "InteriorWindow", "Warehouse", "ForestClearing"};
    lua_getfield(L, 1, "preset");
    if (lua_type(L, -1) == LUA_TSTRING) {
        const char* n = lua_tostring(L, -1);
        v.hdriPreset = -1;
        for (int i = 0; i < 8; ++i) if (std::strcmp(n, names[i]) == 0) v.hdriPreset = i;
        if (v.hdriPreset < 0) return luaL_error(L, "world(): unknown preset '%s'", n);
    } else if (lua_isnumber(L, -1)) {
        v.hdriPreset = (int)std::fmin(std::fmax(lua_tonumber(L, -1), -1), 7);
    }
    lua_pop(L, 1);
    v.hdriStrength = Num(L, 1, "strength", v.hdriStrength, 0, 100);
    v.hdriRotation = Num(L, 1, "rotation", v.hdriRotation);
    v.sunElevation = Num(L, 1, "sun_elevation", v.sunElevation, -90, 90);
    v.sunAzimuth = Num(L, 1, "sun_azimuth", v.sunAzimuth);
    v.sunStrength = Num(L, 1, "sun_strength", v.sunStrength, 0, 1000);
    Vec3Field(L, 1, "sun_color", v.sunColor, 0, 10);
    Vec3Field(L, 1, "sky_color", v.skyColor, 0, 10);
    Vec3Field(L, 1, "horizon_color", v.horizonColor, 0, 10);
    v.skyStrength = Num(L, 1, "sky_strength", v.skyStrength, 0, 100);
    v.exposure = Num(L, 1, "exposure", v.exposure, 0.01f, 100);
    return 0;
}

// curve({{x,y,z}, ...}, {radius=, cyclic=, nurbs=, sides=, taper={...}, resolution=}) sweeps a tube
// with Leorix's curve code (Bezier with Auto handles, or NURBS) and adds it like a primitive.
int l_curve(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    CurvePath c;
    lua_Integer n = luaL_len(L, 1);
    if (n < 2 || n > 100000) return luaL_error(L, "curve() needs between 2 and 100000 points");
    for (lua_Integer i = 1; i <= n; ++i) {
        lua_rawgeti(L, 1, i);
        if (!lua_istable(L, -1)) return luaL_error(L, "curve(): point %I is not {x, y, z}", i);
        float p[3] = {0, 0, 0};
        for (int k = 0; k < 3; ++k) { lua_rawgeti(L, -1, k + 1); p[k] = (float)luaL_checknumber(L, -1); lua_pop(L, 1); }
        c.points.push_back({p[0], p[1], p[2]});
        lua_pop(L, 1);
    }
    c.bevelDepth = 0.05f;
    if (lua_istable(L, 2)) {
        c.bevelDepth = Num(L, 2, "radius", c.bevelDepth, 0.0001f, 1e4f);
        c.resolution = (int)Num(L, 2, "resolution", (float)c.resolution, 1, 64);
        c.bevelResolution = (int)Num(L, 2, "sides", (float)c.bevelResolution, 0, 16);
        lua_getfield(L, 2, "cyclic"); c.cyclic = lua_toboolean(L, -1); lua_pop(L, 1);
        lua_getfield(L, 2, "nurbs"); c.nurbs = lua_toboolean(L, -1); lua_pop(L, 1);
        lua_getfield(L, 2, "taper");
        if (lua_istable(L, -1))
            for (lua_Integer i = 1; i <= n; ++i) {
                lua_rawgeti(L, -1, i);
                c.radius.push_back(lua_isnumber(L, -1) ? (float)std::fmax(0.0, lua_tonumber(L, -1)) : 1.0f);
                lua_pop(L, 1);
            }
        lua_pop(L, 1);
    }
    c.Sync();
    Mesh m = CurveToMesh(c);
    if (m.tris.empty()) return luaL_error(L, "curve(): the points are too close together to sweep");
    m.ComputeBounds();
    if (m.normals.size() != m.positions.size()) m.ComputeNormals();
    PushPrimitive(L, m);
    return 2;
}

void OpenMeshLib(lua_State* L) {
    luaL_newmetatable(L, kMeshMT);
    lua_pushvalue(L, -1); lua_setfield(L, -2, "__index");
    const luaL_Reg methods[] = {
        {"vert", l_mesh_vert}, {"face", l_mesh_face}, {"build", l_mesh_build}, {"__gc", l_mesh_gc}, {nullptr, nullptr}};
    luaL_setfuncs(L, methods, 0);
    lua_pop(L, 1);
    const luaL_Reg globals[] = {
        {"print", l_print}, {"mesh", l_mesh_new},
        {"cube", l_cube}, {"plane", l_plane}, {"sphere", l_sphere}, {"ico_sphere", l_ico},
        {"cylinder", l_cylinder}, {"cone", l_cone}, {"torus", l_torus}, {"circle", l_circle}, {"grid", l_grid},
        {"material", l_material}, {"camera", l_camera}, {"world", l_world}, {"curve", l_curve},
        {nullptr, nullptr}};
    lua_pushglobaltable(L);
    luaL_setfuncs(L, globals, 0);
    lua_pushnumber(L, 3.14159265358979323846);
    lua_setfield(L, -2, "PI");
    lua_pop(L, 1);
}
// Count hook: runaway loops end with an error instead of freezing the UI thread.
void InstructionHook(lua_State* L, lua_Debug*) {
    if (++Ctx(L)->hookCalls >= kMaxHookCalls) {
        lua_sethook(L, InstructionHook, LUA_MASKCOUNT, 1);   // re-raise on every instruction so pcall can't swallow it
        luaL_error(L, "script took too long (stopped after %d million instructions)", kHookEvery / 1000000 * kMaxHookCalls);
    }
}

// Allocator with a hard cap: returning null makes Lua raise a memory error the script can't ignore.
struct MemoryBudget { size_t used = 0; };
void* CappedAlloc(void* ud, void* ptr, size_t oldSize, size_t newSize) {
    auto* b = static_cast<MemoryBudget*>(ud);
    if (!ptr) oldSize = 0;   // when ptr is null, oldSize is a type tag, not a size
    if (newSize == 0) { std::free(ptr); b->used -= oldSize; return nullptr; }
    if (newSize > oldSize && b->used - oldSize + newSize > kMemoryLimit) return nullptr;
    void* p = std::realloc(ptr, newSize);
    if (p) b->used = b->used - oldSize + newSize;
    return p;
}
} // namespace

ScriptResult RunMeshScript(const std::string& code, float frame, int fps) {
    ScriptResult result;
    MemoryBudget budget;
    lua_State* L = lua_newstate(CappedAlloc, &budget);
    if (!L) { result.output = "Could not start the scripting engine."; return result; }
    // Only safe standard libraries: base, math, string, table. No io/os/package/loadfile.
    luaL_requiref(L, LUA_GNAME, luaopen_base, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1); lua_pop(L, 1);
    // Remove risky base functions.
    for (const char* n : {"dofile", "loadfile", "load", "collectgarbage", "require"}) { lua_pushnil(L); lua_setglobal(L, n); }

    ScriptCtx ctx{&result};
    lua_pushlightuserdata(L, &ctx);
    lua_setfield(L, LUA_REGISTRYINDEX, "leorix_ctx");
    OpenMeshLib(L);
    // Timeline globals, so a script can animate by being re-run each frame.
    if (fps < 1) fps = 24;
    lua_pushnumber(L, frame); lua_setglobal(L, "frame");
    lua_pushinteger(L, fps); lua_setglobal(L, "fps");
    lua_pushnumber(L, (frame - 1.0f) / (float)fps); lua_setglobal(L, "time");
    lua_sethook(L, InstructionHook, LUA_MASKCOUNT, kHookEvery);

    int status = luaL_loadbuffer(L, code.c_str(), code.size(), "leorix");
    if (status == LUA_OK) status = lua_pcall(L, 0, 0, 0);   // keep the real code: LUA_ERRMEM is reported below
    if (status != LUA_OK) {
        const char* msg = status == LUA_ERRMEM ? "script ran out of memory (limit 512 MB)" : lua_tostring(L, -1);
        result.output += std::string("\nError: ") + (msg ? msg : "unknown error") + "\n";
        result.ok = false;
    } else {
        result.ok = true;
        if (result.meshes.empty()) result.output += "\n(Script ran, but created no mesh. Call a primitive like cube() or m:build().)\n";
        else {
            size_t verts = 0, tris = 0;
            for (const auto& m : result.meshes) { verts += m.VertexCount(); tris += m.TriCount(); }
            result.output += "\nCreated " + std::to_string(result.meshes.size()) + " mesh(es): " +
                             std::to_string(verts) + " verts, " + std::to_string(tris) + " tris.\n";
        }
    }
    lua_close(L);
    return result;
}

Mesh MergeScriptMeshes(std::vector<Mesh> meshes) {
    Mesh combined;
    bool perVertexUVs = true;
    for (const auto& m : meshes) perVertexUVs = perVertexUVs && m.uvs.size() == m.positions.size();
    for (auto& m : meshes) {
        m.EnsureUVs();   // per mesh: its own UVs, or a fallback for this mesh only
        if (m.normals.size() != m.positions.size()) m.ComputeNormals();
        uint32_t base = (uint32_t)combined.positions.size(), polyBase = combined.polyCount;
        combined.positions.insert(combined.positions.end(), m.positions.begin(), m.positions.end());
        combined.normals.insert(combined.normals.end(), m.normals.begin(), m.normals.end());
        if (perVertexUVs) combined.uvs.insert(combined.uvs.end(), m.uvs.begin(), m.uvs.end());
        combined.loopUVs.insert(combined.loopUVs.end(), m.loopUVs.begin(), m.loopUVs.end());
        for (Tri t : m.tris) { t.a += base; t.b += base; t.c += base; t.poly += polyBase; combined.tris.push_back(t); }
        combined.polyCount += m.polyCount;
    }
    combined.name = "Script mesh";
    combined.ComputeBounds();
    return combined;
}

const char* DefaultScriptExample() {
    return
        "-- Leorix Scripting (Lua). Build meshes from parameters.\n"
        "-- Primitives return vertex and triangle counts:\n"
        "local v, t = ico_sphere(1.0, 3)\n"
        "print('ico sphere:', v, 'verts', t, 'tris')\n\n"
        "-- Or build your own mesh by hand -- a pyramid:\n"
        "local m = mesh()\n"
        "local a = m:vert(-1, 0, -1)\n"
        "local b = m:vert( 1, 0, -1)\n"
        "local c = m:vert( 1, 0,  1)\n"
        "local d = m:vert(-1, 0,  1)\n"
        "local e = m:vert( 0, 1.6, 0)\n"
        "m:face(a, b, c, d)   -- base quad\n"
        "m:face(a, e, b)      -- sides\n"
        "m:face(b, e, c)\n"
        "m:face(c, e, d)\n"
        "m:face(d, e, a)\n"
        "m:build()\n";
}
