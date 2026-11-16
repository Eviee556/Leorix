#include "tools/Script.h"
#include "mesh/Primitives.h"
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

struct ScriptCtx { ScriptResult* result; int hookCalls = 0; };
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

void PushPrimitive(lua_State* L, const Mesh& m) {
    Ctx(L)->result->meshes.push_back(m);
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
    Ctx(L)->result->meshes.push_back(m->mesh);
    return 0;
}
int l_mesh_gc(lua_State* L) { CheckMesh(L)->~ScriptMesh(); return 0; }

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
        {nullptr, nullptr}};
    lua_pushglobaltable(L);
    luaL_setfuncs(L, globals, 0);
    lua_pushnumber(L, 3.14159265358979323846);
    lua_setfield(L, -2, "PI");
    lua_pop(L, 1);
}
// Count hook: runaway loops end with an error instead of freezing the UI thread.
void InstructionHook(lua_State* L, lua_Debug*) {
    if (++Ctx(L)->hookCalls >= kMaxHookCalls)
        luaL_error(L, "script took too long (stopped after %d million instructions)", kHookEvery / 1000000 * kMaxHookCalls);
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
