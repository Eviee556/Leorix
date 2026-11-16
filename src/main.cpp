// Leorix Native - entry point: SDL3 window, Vulkan swapchain, Dear ImGui frame loop.
#include "app/App.h"
#include "app/Theme.h"
#include "mesh/IO.h"
#include "mesh/EditOps.h"
#include "mesh/Primitives.h"
#include "tools/Script.h"
#include "render/PathTracer.h"
#include "tools/Sculpt.h"
#include "tools/VisualScript.h"
#include <cmath>
#include "gpu/VulkanContext.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_vulkan.h"
#include <SDL3/SDL.h>
#include <filesystem>
#include <chrono>
#include <thread>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_vulkan.h>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

static VulkanContext g_vk;
static ImGui_ImplVulkanH_Window g_window;
static const uint32_t kMinImageCount = 2;
static bool g_swapchainRebuild = false;

static void FrameRender(ImGui_ImplVulkanH_Window* wd, ImDrawData* drawData) {
    VkSemaphore acquired = wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;
    VkSemaphore complete = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
    VkResult err = vkAcquireNextImageKHR(g_vk.device, wd->Swapchain, UINT64_MAX, acquired, VK_NULL_HANDLE, &wd->FrameIndex);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) g_swapchainRebuild = true;
    if (err == VK_ERROR_OUT_OF_DATE_KHR) return;
    if (err != VK_SUBOPTIMAL_KHR) CheckVk(err);

    ImGui_ImplVulkanH_Frame* fd = &wd->Frames[wd->FrameIndex];
    CheckVk(vkWaitForFences(g_vk.device, 1, &fd->Fence, VK_TRUE, UINT64_MAX));
    CheckVk(vkResetFences(g_vk.device, 1, &fd->Fence));
    CheckVk(vkResetCommandPool(g_vk.device, fd->CommandPool, 0));
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    CheckVk(vkBeginCommandBuffer(fd->CommandBuffer, &begin));

    VkRenderPassBeginInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = wd->RenderPass;
    rp.framebuffer = fd->Framebuffer;
    rp.renderArea.extent.width = wd->Width;
    rp.renderArea.extent.height = wd->Height;
    rp.clearValueCount = 1;
    rp.pClearValues = &wd->ClearValue;
    vkCmdBeginRenderPass(fd->CommandBuffer, &rp, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(drawData, fd->CommandBuffer);
    vkCmdEndRenderPass(fd->CommandBuffer);

    VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &acquired;
    submit.pWaitDstStageMask = &stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &fd->CommandBuffer;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &complete;
    CheckVk(vkEndCommandBuffer(fd->CommandBuffer));
    CheckVk(vkQueueSubmit(g_vk.queue, 1, &submit, fd->Fence));
}

static void FramePresent(ImGui_ImplVulkanH_Window* wd) {
    if (g_swapchainRebuild) return;
    VkSemaphore complete = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
    VkPresentInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &complete;
    info.swapchainCount = 1;
    info.pSwapchains = &wd->Swapchain;
    info.pImageIndices = &wd->FrameIndex;
    VkResult err = vkQueuePresentKHR(g_vk.queue, &info);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) g_swapchainRebuild = true;
    if (err == VK_ERROR_OUT_OF_DATE_KHR) return;
    if (err != VK_SUBOPTIMAL_KHR) CheckVk(err);
    wd->SemaphoreIndex = (wd->SemaphoreIndex + 1) % wd->SemaphoreCount;
}

static int Fail(const std::string& message) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Leorix could not start", message.c_str(), nullptr);
    return 1;
}

static int SelfTest() {
    std::printf("== Leorix mesh self-test ==\n");
    int failures = 0;
    auto check = [&](bool ok, const char* what) { if (!ok) { std::printf("  FAIL: %s\n", what); ++failures; } };
    // Expected counts are Blender's Add Mesh defaults (README "Primitive topology").
    struct Expect { Primitive t; size_t verts; unsigned polys; };
    PrimitiveParams pp;
    for (Expect e : {Expect{Primitive::Plane, 4, 1}, {Primitive::Cube, 8, 6}, {Primitive::Circle, 32, 1},
                     {Primitive::UVSphere, 482, 512}, {Primitive::IcoSphere, 42, 80}, {Primitive::Cylinder, 64, 34},
                     {Primitive::Cone, 33, 33}, {Primitive::Torus, 512, 512}, {Primitive::Grid, 121, 100}}) {
        Mesh m = BuildPrimitive(e.t, pp);
        std::printf("  %-11s verts=%5zu tris=%5zu polys=%u\n", PrimitiveName(e.t), m.VertexCount(), m.TriCount(), m.polyCount);
        check(m.VertexCount() == e.verts && m.polyCount == e.polys, PrimitiveName(e.t));
    }
    ScriptResult r = RunMeshScript(DefaultScriptExample());
    std::printf("  script ok=%d meshes=%zu\n", r.ok ? 1 : 0, r.meshes.size());
    check(r.ok, "script");
    {   // scripting guards: bad face indices, runaway loops, memory cap, UV-preserving merge
        auto fails = [](const std::string& code, const char* text) {
            ScriptResult s = RunMeshScript(code);
            return !s.ok && s.output.find(text) != std::string::npos;
        };
        const std::string tri = "local m = mesh(); m:vert(0,0,0); m:vert(1,0,0); m:vert(0,1,0)\n";
        check(fails(tri + "m:face(0, 1, 99)", "does not exist"), "script face index out of range");
        check(fails(tri + "m:face(0, 1, -1)", "does not exist"), "script face negative index");
        check(fails(tri + "m:face(0, 1, 1)", "3 different vertices"), "script face duplicate index");
        auto t0 = std::chrono::steady_clock::now();
        bool stopped = fails("while true do end", "took too long");
        double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("  script runaway loop stopped=%d in %.2fs\n", stopped ? 1 : 0, secs);
        check(stopped && secs < 10.0, "script instruction limit");
        t0 = std::chrono::steady_clock::now();
        bool nested = fails("while true do pcall(function() while true do end end) end", "took too long");
        secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        check(nested && secs < 10.0, "script limit survives pcall");
        check(fails("local s = string.rep('x', 600 * 1024 * 1024)", "out of memory"), "script memory limit");
        ScriptResult u = RunMeshScript("plane()\n" + tri + "m:face(0, 1, 2); m:build()");
        Mesh plane = BuildPrimitive(Primitive::Plane, pp);
        plane.EnsureUVs();
        Mesh merged = MergeScriptMeshes(u.meshes);
        bool keptPlaneUVs = merged.HasLoopUVs() && plane.HasLoopUVs();
        for (size_t i = 0; keptPlaneUVs && i < plane.loopUVs.size(); ++i)
            keptPlaneUVs = merged.loopUVs[i].u == plane.loopUVs[i].u && merged.loopUVs[i].v == plane.loopUVs[i].v;
        check(u.ok && u.meshes.size() == 2 && keptPlaneUVs && merged.VertexCount() == 7, "script merge keeps per-mesh UVs");
    }
    // OBJ round trip must keep quads (README "Mesh formats").
    Mesh cube = BuildPrimitive(Primitive::Cube, pp), back; std::string error;
    std::string path = (std::filesystem::temp_directory_path() / "leorix_selftest.obj").string();
    check(ExportMesh(path, cube, error) && ImportMesh(path, back, error), "obj export/import");
    check(back.VertexCount() == cube.VertexCount() && back.polyCount == cube.polyCount, "obj round trip keeps quads");
    std::filesystem::remove(path);
    // Edit Mode operators: counts match Blender, and loopUVs stays one-per-corner (PARITY.md rule).
    auto uvOk = [](const Mesh& m) { return m.HasLoopUVs(); };
    Mesh ex = BuildPrimitive(Primitive::Cube, pp); ex.EnsureUVs();
    EditSelection one; one.faces.insert(0);
    check(ExtrudeFaces(ex, one).ok && ex.VertexCount() == 12 && ex.polyCount == 10 && uvOk(ex), "extrude one cube face");
    Mesh del = BuildPrimitive(Primitive::Cube, pp); del.EnsureUVs();
    check(DeleteSelected(del, one).ok && del.VertexCount() == 8 && del.polyCount == 5 && uvOk(del), "delete one cube face");
    Mesh mg = BuildPrimitive(Primitive::Cube, pp); mg.EnsureUVs();
    EditSelection face0verts;
    for (const Tri& t : mg.tris) if (t.poly == 0) face0verts.verts.insert({t.a, t.b, t.c});
    EditOpResult mr = MergeAtCenter(mg, face0verts);
    check(mr.ok && mg.VertexCount() == 5 && mg.polyCount == 5 && uvOk(mg) && mr.sel.verts.size() == 1, "merge cube face to a point");
    bool inRange = true;
    for (const Mesh* m : {&ex, &del, &mg})
        for (const Tri& t : m->tris) inRange &= t.a < m->VertexCount() && t.b < m->VertexCount() && t.c < m->VertexCount() && t.poly < m->polyCount;
    check(inRange, "edit ops keep indices in range");
    // A selection left over from before a modifier removed vertices must not read past the mesh.
    EditSelection stale;
    stale.verts = {500, 7};
    stale.edges.insert(Mesh::EdgeKey(1000, 1001));
    stale.edges.insert(Mesh::EdgeKey(0, 900));
    stale.faces.insert(999);
    Mesh st1 = BuildPrimitive(Primitive::Cube, pp); st1.EnsureUVs();
    Mesh st2 = st1, st3 = st1;
    MergeAtCenter(st1, stale); ExtrudeFaces(st2, stale); DeleteSelected(st3, stale);
    bool staleOk = true;
    for (const Mesh* m : {&st1, &st2, &st3})
        for (const Tri& t : m->tris) staleOk &= t.a < m->VertexCount() && t.b < m->VertexCount() && t.c < m->VertexCount();
    check(staleOk, "edit ops ignore a stale selection");
    // Undo moves whole objects out of and back into the scene; they must come back by identity.
    {
        Scene sc;
        MeshObject* a = sc.AddMesh(BuildPrimitive(Primitive::Cube, pp), "A");
        MeshObject* b = sc.AddMesh(BuildPrimitive(Primitive::Plane, pp), "B");
        auto held = sc.Take(sc.IndexOf(a));
        bool ok = held.get() == a && sc.objects.size() == 1 && sc.IndexOf(b) == 0 && sc.IndexOf(a) == -1;
        sc.Insert(0, std::move(held));
        ok &= sc.IndexOf(a) == 0 && sc.IndexOf(b) == 1 && sc.Active() == a;
        check(ok, "scene take/insert keeps object identity");
    }
    // Normal-map tangents: on a +Z plane with u = x, v = y, T must be +X and B +Y; mirroring U
    // flips T but B must still follow +V.
    for (int mirror = 0; mirror < 2; ++mirror) {
        Mesh p;
        p.positions = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
        p.tris = {{0, 1, 2, 0}, {0, 2, 3, 0}};
        p.polyCount = 1;
        auto uvOf = [&](uint32_t i) { return Float2{mirror ? 1 - p.positions[i].x : p.positions[i].x, p.positions[i].y}; };
        for (const Tri& t : p.tris) for (uint32_t i : {t.a, t.b, t.c}) p.loopUVs.push_back(uvOf(i));
        p.ComputeNormals();
        std::vector<Float3> T, B;
        ComputeLoopTangents(p, T, B);
        bool ok = T.size() == 6;
        for (size_t i = 0; ok && i < T.size(); ++i) {
            const Float3& n = p.normals[i < 3 ? p.tris[0].a : p.tris[1].a];
            float tn = T[i].x * n.x + T[i].y * n.y + T[i].z * n.z;
            ok = std::fabs(tn) < 1e-4f && std::fabs(T[i].x - (mirror ? -1.0f : 1.0f)) < 1e-4f && std::fabs(B[i].y - 1.0f) < 1e-4f;
        }
        check(ok, mirror ? "loop tangents (mirrored U)" : "loop tangents orthogonal to normal, along +U");
    }

    // Path tracer: a floor lit only by a small emissive quad. Light sampling (NEE+MIS) must agree
    // with brute-force BSDF sampling, with far less noise at the same sample count.
    {
        Scene scene;
        MeshObject* floorObj = scene.AddMesh(BuildPrimitive(Primitive::Plane, pp), "Floor");
        floorObj->transform.scale = {4, 1, 4};
        MeshObject* lamp = scene.AddMesh(BuildPrimitive(Primitive::Plane, pp), "Lamp");
        lamp->transform.position = {0, 1.5f, 0};
        lamp->transform.scale = {0.3f, 1, 0.3f};
        lamp->material.emissionColor[0] = lamp->material.emissionColor[1] = lamp->material.emissionColor[2] = 1.0f;
        lamp->material.emissiveStrengthRT = 12.0f;
        RayTraceWorld world;
        world.sunStrength = 0.0f; world.skyStrength = 0.0f;
        auto render = [&](int spp, bool nee, int seed, int threads = 1) {
            RayTraceSettings st;
            st.width = 48; st.height = 32; st.samples = spp; st.maxBounces = 2;
            st.denoise = false; st.clampEnabled = false; st.lightSampling = nee; st.seed = seed;
            st.threads = threads;
            PathTracer pt;
            pt.Start(scene, scene.Camera(), 0.0f, 1.2f, 5.0f, {0, 0, 0}, false, st, world);
            while (pt.Running()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            std::vector<float> v;
            for (size_t i = 0; i < pt.Pixels().size(); i += 4) v.push_back(pt.Pixels()[i]);
            return v;
        };
        auto mean = [](const std::vector<float>& v) { double s = 0; for (float x : v) s += x; return s / v.size(); };
        auto noise = [](const std::vector<float>& a, const std::vector<float>& b) {
            double s = 0; for (size_t i = 0; i < a.size(); ++i) s += (a[i] - b[i]) * (a[i] - b[i]); return s / a.size();
        };
        auto t0 = std::chrono::steady_clock::now();
        auto ref = render(1024, false, 1);
        auto neeA = render(16, true, 2), neeB = render(16, true, 3);
        auto bfA = render(16, false, 2), bfB = render(16, false, 3);
        // Rows are seeded by index, so a multithreaded render must equal a single-threaded one
        // exactly; it can only differ if the render is reported finished with rows still unwritten.
        check(render(16, true, 2, 8) == render(16, true, 2, 1), "multithreaded render complete when reported done");
        {
            // The UI's case: 640x320, sky + sun, all cores, denoise on. Rows are claimed top-down,
            // so unfinished rows showed up as streaks along the bottom of saved renders.
            RayTraceSettings st;
            st.width = 640; st.height = 320; st.samples = 32; st.maxBounces = 2;
            PathTracer pt;
            pt.Start(scene, scene.Camera(), 0.0f, 0.5f, 6.0f, {0, 0, 0}, false, st, RayTraceWorld{});
            while (pt.Running()) std::this_thread::yield();   // read the moment it says done, as the UI does
            const auto& px = pt.Pixels();
            auto rowMean = [&](int y) { double s = 0; for (int x = 0; x < 640; ++x) for (int c = 0; c < 3; ++c) s += px[((size_t)y * 640 + x) * 4 + c]; return s / (640 * 3); };
            bool opaque = true, steady = true;
            for (size_t i = 3; i < px.size(); i += 4) opaque = opaque && px[i] == 255;
            for (int y = 320 - 32; y < 320; ++y) steady = steady && std::fabs(rowMean(y) - rowMean(y - 32)) < 0.15 * rowMean(y - 32) + 4.0;
            check(opaque && steady, "640x320 multithreaded render has no unfinished bottom rows");
        }
        double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        double mRef = mean(ref), mNee = mean(neeA), nNee = noise(neeA, neeB), nBf = noise(bfA, bfB);
        std::printf("  tracer mean ref=%.2f nee=%.2f  noise nee=%.2f brute=%.2f  (%.2fs)\n", mRef, mNee, nNee, nBf, secs);
        check(mRef > 5.0 && std::fabs(mNee - mRef) < 0.03 * mRef, "NEE mean matches brute-force reference");
        check(nNee < 0.5 * nBf, "NEE lowers noise");

        // Normal maps: a flat map must change nothing, a tilted one must change the shading.
        auto setNormalMap = [&](uint8_t r, uint8_t g, uint8_t b) {
            TextureData& tex = floorObj->material.cpu[(int)MapSlot::Normal];
            tex.width = tex.height = 1; tex.pixels = {r, g, b, 255};
        };
        auto flatOff = render(64, true, 4);
        setNormalMap(128, 128, 255);
        auto flatOn = render(64, true, 4);
        setNormalMap(230, 128, 160);
        auto tilted = render(64, true, 4);
        std::printf("  normal map mean none=%.2f flat=%.2f tilted=%.2f\n", mean(flatOff), mean(flatOn), mean(tilted));
        check(std::fabs(mean(flatOn) - mean(flatOff)) < 0.02 * mean(flatOff), "flat normal map is neutral");
        check(std::fabs(mean(tilted) - mean(flatOff)) > 0.05 * mean(flatOff), "tilted normal map changes shading");
    }
    // Sculpt: a Draw dab moves verts inside the radius and nothing outside; Smooth flattens the bump.
    {
        Mesh g = BuildPrimitive(Primitive::Grid, pp);
        g.ComputeNormals();
        const std::vector<Float3> flat = g.positions;
        const Float3 c = flat[flat.size() / 2];
        const float radius = 0.5f;
        SculptSettings ss;
        SculptDab dab; dab.center = c; dab.radius = radius; dab.strength = 0.5f;
        size_t moved = ApplySculptDab(g, nullptr, ss, Icon::SculptDraw, dab, nullptr);
        auto dist = [](Float3 a, Float3 b) { return std::sqrt((a.x-b.x)*(a.x-b.x) + (a.y-b.y)*(a.y-b.y) + (a.z-b.z)*(a.z-b.z)); };
        bool insideMoved = true, outsideStill = true;
        for (size_t i = 0; i < flat.size(); ++i) {
            float d = dist(flat[i], c), m = dist(flat[i], g.positions[i]);
            if (d < radius * 0.9f && m < 1e-6f) insideMoved = false;
            if (d > radius && m != 0.0f) outsideStill = false;
        }
        std::printf("  sculpt draw moved=%zu\n", moved);
        check(moved > 0 && insideMoved, "sculpt draw moves verts inside the radius");
        check(outsideStill, "sculpt draw leaves verts outside the radius alone");
        size_t centre = flat.size() / 2;
        float peak = dist(g.positions[centre], flat[centre]);
        g.ComputeNormals();
        SculptDab sm = dab; sm.strength = 1.0f; sm.smooth = true;
        for (int i = 0; i < 3; ++i) ApplySculptDab(g, nullptr, ss, Icon::SculptSmooth, sm, nullptr);
        float after = dist(g.positions[centre], flat[centre]);
        std::printf("  sculpt smooth peak %.4f -> %.4f\n", peak, after);
        check(after < peak * 0.9f, "sculpt smooth reduces displacement");
        Mesh u = BuildPrimitive(Primitive::Grid, pp);
        check(ApplySculptDab(u, nullptr, ss, Icon::SculptMask, dab, nullptr) == 0 && dist(u.positions[centre], flat[centre]) == 0.0f,
              "unsupported sculpt brush leaves mesh untouched");
    }
    // Material undo: params copy leaves textures alone, and a step round-trips through undo/redo.
    {
        Material live, snap;
        live.cpu[(int)MapSlot::Normal].width = live.cpu[(int)MapSlot::Normal].height = 1;
        live.cpu[(int)MapSlot::Normal].pixels = {1, 2, 3, 4};
        live.paths[(int)MapSlot::Normal] = "n.png";
        live.roughness = 0.25f;
        CopyMaterialParams(snap, live);
        check(snap.roughness == 0.25f && !snap.HasCpu(MapSlot::Normal) && snap.paths[(int)MapSlot::Normal].empty(),
              "material params copy skips textures");
        check(live.HasCpu(MapSlot::Normal) && live.paths[(int)MapSlot::Normal] == "n.png", "material params copy keeps source");
        UndoStack stack;
        live.roughness = 0.9f;
        auto before = std::make_shared<Material>(), after = std::make_shared<Material>();
        CopyMaterialParams(*before, snap);
        CopyMaterialParams(*after, live);
        stack.Push("Edit Roughness", [&live, before] { CopyMaterialParams(live, *before); },
                                     [&live, after] { CopyMaterialParams(live, *after); });
        stack.Undo();
        check(live.roughness == 0.25f && live.HasCpu(MapSlot::Normal), "material undo restores params, keeps maps");
        stack.Redo();
        check(live.roughness == 0.9f, "material redo");
    }
    // Visual Scripting: blocks and wires build the same meshes as the primitives.
    {
        auto build = [](VisualGraph& g) { return EvaluateGraph(g); };
        std::string why;
        VisualGraph g; int box = g.Add(BlockType::Box), show = g.Add(BlockType::ShowInScene);
        g.Connect(box, show, 0, why);
        BuildResult r = build(g);
        check(r.ok && r.mesh.VertexCount() == 8 && r.mesh.polyCount == 6, "visual: box -> show gives 8 verts / 6 faces");
        int line = g.Add(BlockType::CopyLine);
        g.Connect(box, line, 0, why); g.Connect(line, show, 0, why);   // replaces the old wire
        r = build(g);
        check(r.ok && r.mesh.VertexCount() == 24 && r.mesh.polyCount == 18 && r.mesh.HasLoopUVs(), "visual: copy in a line x3 gives 24 / 18");
        int grid = g.Add(BlockType::CopyGrid);
        g.Connect(box, grid, 0, why); g.Connect(grid, show, 0, why);
        r = build(g);
        check(r.ok && r.mesh.VertexCount() == 48 && r.mesh.polyCount == 36, "visual: copy in a grid 2x3 gives 6 copies");
        int mv = g.Add(BlockType::Move); g.Find(mv)->values[2] = 5.0f;
        g.Connect(box, mv, 0, why); g.Connect(mv, show, 0, why);
        r = build(g);
        bool lifted = r.ok;
        for (const Float3& p : r.mesh.positions) lifted &= p.y >= 3.99f && p.y <= 6.01f;
        check(lifted, "visual: move offsets positions");
        int a = g.Add(BlockType::Number), b = g.Add(BlockType::Number), m = g.Add(BlockType::Math);
        g.Find(a)->values[0] = 6; g.Find(b)->values[0] = 3;
        g.Connect(a, m, 0, why); g.Connect(b, m, 1, why);
        float out = 0; bool mathOk = true;
        const float expect[] = {9, 3, 18, 2};
        for (int op = 0; op < kMathOpCount; ++op) { g.Find(m)->op = op; mathOk &= EvaluateNumber(g, m, out, why) && out == expect[op]; }
        g.Find(b)->values[0] = 0; g.Find(m)->op = 3;
        mathOk &= !EvaluateNumber(g, m, out, why);
        check(mathOk, "visual: math add / take away / times / divide by, and divide by zero refused");
        check(!g.Connect(m, mv, 0, why), "visual: number wire into a shape slot is refused");
        check(!g.Connect(box, mv, 1, why), "visual: shape wire into a number slot is refused");
        int m2 = g.Add(BlockType::Math);
        g.Connect(m, m2, 0, why);
        check(!g.Connect(m2, m, 0, why) && why.find("Loops") != std::string::npos, "visual: loops are refused");
        int rnd = g.Add(BlockType::Random); float r1 = 0, r2 = 0;
        EvaluateNumber(g, rnd, r1, why); EvaluateNumber(g, rnd, r2, why);
        g.Find(rnd)->values[2] = 7; float r3 = 0; EvaluateNumber(g, rnd, r3, why);
        check(r1 == r2 && r1 >= 0 && r1 <= 10 && r3 != r1, "visual: random number repeats for the same seed");
        g.Find(line)->values[1] = 5000; g.Connect(line, show, 0, why);
        r = build(g);
        check(r.ok && r.mesh.VertexCount() == 8 * (size_t)kMaxCopies && !r.notes.empty(), "visual: copies capped at 1000 with a note");
        VisualGraph empty; empty.Add(BlockType::ShowInScene);
        check(!build(empty).ok && !empty.blocks[0].error.empty(), "visual: empty Show in Scene explains itself");
        VisualGraph starter; starter.LoadStarter();
        check(build(starter).ok, "visual: starter example builds");

        // Per-copy Random: Random -> Box size -> Copy in a Line. Each copy's width differs when on.
        VisualGraph pc; int rn = pc.Add(BlockType::Random), bx = pc.Add(BlockType::Box), ln = pc.Add(BlockType::CopyLine), sh = pc.Add(BlockType::ShowInScene);
        pc.Find(rn)->values[0] = 1; pc.Find(rn)->values[1] = 5; pc.Find(ln)->values[2] = 20;
        pc.Connect(rn, bx, 0, why); pc.Connect(bx, ln, 0, why); pc.Connect(ln, sh, 0, why);
        auto widths = [&](VisualGraph& gr) {
            std::vector<float> w; BuildResult br = build(gr);
            for (size_t c = 0; br.ok && c + 8 <= br.mesh.positions.size(); c += 8) {
                float lo = 1e9f, hi = -1e9f;
                for (size_t k = c; k < c + 8; ++k) { lo = std::min(lo, br.mesh.positions[k].x); hi = std::max(hi, br.mesh.positions[k].x); }
                w.push_back(hi - lo);
            }
            return w;
        };
        std::vector<float> wOn = widths(pc);
        check(wOn.size() == 3 && std::fabs(wOn[0] - wOn[1]) > 0.01f && std::fabs(wOn[1] - wOn[2]) > 0.01f, "visual: random gives each copy its own pick");
        check(widths(pc) == wOn, "visual: per-copy random repeats between builds");
        pc.Find(rn)->values[3] = 0;
        std::vector<float> wOff = widths(pc);
        check(wOff.size() == 3 && std::fabs(wOff[0] - wOff[1]) < 1e-4f && std::fabs(wOff[1] - wOff[2]) < 1e-4f, "visual: random shared by all copies when switched off");
        pc.Find(rn)->values[3] = 1;

        // Save / load round trip, then malformed files.
        std::string saved = SaveGraph(pc), msg;
        VisualGraph back; back.LoadStarter();
        check(LoadGraph(back, saved, msg) && back.blocks.size() == pc.blocks.size() && back.wires.size() == pc.wires.size() &&
              widths(back) == wOn && back.nextId == pc.nextId, "visual: save / load round trip");
        VisualGraph keep; keep.LoadStarter();
        const size_t keepBlocks = keep.blocks.size();
        bool badOk = !LoadGraph(keep, "not json {", msg) && !LoadGraph(keep, "[1,2,3]", msg) &&
                     !LoadGraph(keep, "{\"format\":\"other\",\"blocks\":[]}", msg) &&
                     !LoadGraph(keep, "{\"format\":\"leorix-blocks\",\"blocks\":7}", msg) && keep.blocks.size() == keepBlocks;
        check(badOk, "visual: malformed files are refused and leave the graph alone");
        bool odd = LoadGraph(keep, R"({"format":"leorix-blocks","blocks":[
            {"id":1,"type":"Box","values":[1e9]}, {"id":2,"type":"Teleporter"}, {"id":1,"type":"Ball"}, {"id":"x","type":"Box"},
            {"id":3,"type":"Show in Scene","values":"oops"}, {"id":4,"type":"Math","op":99}],
            "wires":[{"from":1,"to":3,"input":0}, {"from":2,"to":3,"input":0}, {"from":1,"to":1,"input":5}, {"from":4,"to":3,"input":0}, 5]})", msg);
        check(odd && keep.blocks.size() == 3 && keep.wires.size() == 1 && keep.Find(1)->values[0] == 100.0f &&
              keep.Find(4)->op == kMathOpCount - 1 && keep.nextId == 5 && msg.find("Skipped") != std::string::npos && build(keep).ok,
              "visual: odd files load what they can, clamp values and skip the rest");

        // Time, Mirror, Copy Upwards, Scatter Randomly
        VisualGraph tg; int tm = tg.Add(BlockType::Time); tg.Find(tm)->values[0] = 2; tg.frame = 25; tg.fps = 24;
        float tv = 0;
        check(EvaluateNumber(tg, tm, tv, why) && std::fabs(tv - 2.0f) < 1e-5f, "visual: time block counts seconds from frame 1");
        VisualGraph mg2; int mb = mg2.Add(BlockType::Box), mm = mg2.Add(BlockType::Move), mi = mg2.Add(BlockType::Mirror), ms = mg2.Add(BlockType::ShowInScene);
        mg2.Find(mm)->values[1] = 5;
        mg2.Connect(mb, mm, 0, why); mg2.Connect(mm, mi, 0, why); mg2.Connect(mi, ms, 0, why);
        BuildResult mres = build(mg2);
        float minX = 1e9f; for (const Float3& q : mres.mesh.positions) minX = std::min(minX, q.x);
        check(mres.ok && mres.mesh.VertexCount() == 16 && mres.mesh.polyCount == 12 && minX < -3.9f && mres.mesh.HasLoopUVs(), "visual: mirror adds a flipped copy");
        for (BlockType rt : {BlockType::StackUp, BlockType::Scatter}) {
            VisualGraph rg; int rb = rg.Add(BlockType::Box), rr = rg.Add(rt), rs = rg.Add(BlockType::ShowInScene);
            rg.Connect(rb, rr, 0, why); rg.Connect(rr, rs, 0, why);
            BuildResult rres = build(rg);
            size_t want = 8 * (size_t)std::lround(rg.Find(rr)->values[1]);
            check(rres.ok && rres.mesh.VertexCount() == want, rt == BlockType::StackUp ? "visual: copy upwards count" : "visual: scatter randomly count");
        }
    }
    // Edit Mode Shift+D and P
    {
        PrimitiveParams cp = DefaultParams(Primitive::Cube);
        EditSelection f0; f0.faces.insert(0);
        Mesh dup = BuildPrimitive(Primitive::Cube, cp); dup.EnsureUVs();
        EditOpResult dr = DuplicateFaces(dup, f0);
        check(dr.ok && dup.VertexCount() == 12 && dup.polyCount == 7 && dup.HasLoopUVs() && dr.sel.faces.size() == 1 && dr.sel.verts.size() == 4,
              "duplicate one cube face");
        Mesh sep = BuildPrimitive(Primitive::Cube, cp), part; sep.EnsureUVs();
        EditOpResult sr = SeparateFaces(sep, f0, part);
        check(sr.ok && sep.VertexCount() == 8 && sep.polyCount == 5 && part.VertexCount() == 4 && part.polyCount == 1 &&
              sep.HasLoopUVs() && part.HasLoopUVs(), "separate one cube face");
        Mesh all = BuildPrimitive(Primitive::Cube, cp), none; EditSelection every;
        for (uint32_t f = 0; f < all.polyCount; ++f) every.faces.insert(f);
        check(!SeparateFaces(all, every, none).ok && all.polyCount == 6, "separate refuses to empty the mesh");
        bool ok = true;
        for (const Mesh* m : {&dup, &sep, &part})
            for (const Tri& t : m->tris) ok &= t.a < m->VertexCount() && t.b < m->VertexCount() && t.c < m->VertexCount() && t.poly < m->polyCount;
        check(ok, "duplicate / separate keep indices in range");
    }
    // Lua timeline globals
    {
        ScriptResult tr = RunMeshScript("cube(frame + time * fps)", 3.0f, 24);   // 3 + 2/24*24 = 5
        float hi = -1e9f;
        if (tr.ok && !tr.meshes.empty()) for (const Float3& q : tr.meshes[0].positions) hi = std::max(hi, q.x);
        check(tr.ok && std::fabs(hi - 2.5f) < 1e-4f, "script: frame, time and fps globals");
    }
    std::printf(failures ? "self-test FAILED (%d)\n" : "self-test passed\n", failures);
    return failures ? 1 : 0;
}

// leorix --convert in.obj out.glb : headless format conversion, also used to test the importers.
static int Convert(const char* in, const char* out) {
    Mesh m; std::string error;
    if (!ImportMesh(in, m, error)) { std::printf("import failed: %s\n", error.c_str()); return 1; }
    std::printf("imported %s: %zu verts %zu tris %u polys\n", in, m.VertexCount(), m.TriCount(), m.polyCount);
    if (!ExportMesh(out, m, error)) { std::printf("export failed: %s\n", error.c_str()); return 1; }
    std::printf("exported %s\n", out);
    return 0;
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) if (std::string(argv[i]) == "--selftest") return SelfTest();
    for (int i = 1; i + 2 < argc; ++i) if (std::string(argv[i]) == "--convert") return Convert(argv[i + 1], argv[i + 2]);

    std::string demoMesh; int demoMode = -1; int demoShading = -1;
    float demoSun = -1000.0f, demoExposure = -1.0f; bool demoUV = false;
    std::string demoRender, demoHdri; int demoPreset = -1; int demoSamples = 0;
    std::vector<std::string> demoTextures;
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--open") demoMesh = argv[i + 1];
        if (std::string(argv[i]) == "--mode") demoMode = std::atoi(argv[i + 1]);
        if (std::string(argv[i]) == "--tex") demoTextures.push_back(argv[i + 1]);   // repeatable
        if (std::string(argv[i]) == "--shading") demoShading = std::atoi(argv[i + 1]); // 0..3
        if (std::string(argv[i]) == "--samples") demoSamples = std::atoi(argv[i + 1]);
        if (std::string(argv[i]) == "--hdripreset") demoPreset = std::atoi(argv[i + 1]);
        if (std::string(argv[i]) == "--hdri") demoHdri = argv[i + 1];
        if (std::string(argv[i]) == "--render") demoRender = argv[i + 1];
        if (std::string(argv[i]) == "--uv") demoUV = std::atoi(argv[i + 1]) != 0;
        if (std::string(argv[i]) == "--sun") demoSun = (float)std::atof(argv[i + 1]);
        if (std::string(argv[i]) == "--exposure") demoExposure = (float)std::atof(argv[i + 1]);
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) return Fail(std::string("SDL_Init failed: ") + SDL_GetError());

    float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    if (scale <= 0.0f) scale = 1.0f;
    SDL_WindowFlags flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    SDL_Window* window = SDL_CreateWindow("Leorix", static_cast<int>(1500 * scale), static_cast<int>(950 * scale), flags);
    if (!window) return Fail(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
    SDL_SetWindowMinimumSize(window, 900, 600);

    std::vector<const char*> exts;
    uint32_t extCount = 0;
    const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&extCount);
    for (uint32_t i = 0; i < extCount; ++i) exts.push_back(sdlExts[i]);

    std::string error;
    if (!g_vk.Init(exts, error)) return Fail(error);

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(window, g_vk.instance, nullptr, &surface))
        return Fail(std::string("Could not create a Vulkan surface: ") + SDL_GetError());

    VkBool32 supported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(g_vk.physical, g_vk.queueFamily, surface, &supported);
    if (!supported) return Fail("The selected GPU cannot present to this window.");

    int w, h;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    ImGui_ImplVulkanH_Window* wd = &g_window;
    wd->Surface = surface;
    const VkFormat formats[] = { VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM };
    wd->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(g_vk.physical, surface, formats, 2, VK_COLORSPACE_SRGB_NONLINEAR_KHR);
    VkPresentModeKHR modes[] = { VK_PRESENT_MODE_FIFO_KHR }; // vsync: smooth UI without burning the GPU
    wd->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(g_vk.physical, surface, modes, 1);
    ImGui_ImplVulkanH_CreateOrResizeWindow(g_vk.instance, g_vk.physical, g_vk.device, wd, g_vk.queueFamily, nullptr, w, h, kMinImageCount, 0);
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable;
    io.ConfigDpiScaleFonts = true;
    io.ConfigDpiScaleViewports = true;
    ApplyLeorixTheme(scale);
    LoadLeorixFonts(scale);

    ImGui_ImplSDL3_InitForVulkan(window);
    ImGui_ImplVulkan_InitInfo init{};
    init.ApiVersion = VK_API_VERSION_1_3;
    init.Instance = g_vk.instance;
    init.PhysicalDevice = g_vk.physical;
    init.Device = g_vk.device;
    init.QueueFamily = g_vk.queueFamily;
    init.Queue = g_vk.queue;
    init.PipelineCache = g_vk.pipelineCache;
    init.DescriptorPool = VK_NULL_HANDLE;   // backend creates a pool with the descriptor types it needs
    init.DescriptorPoolSize = 256;          // room for brush previews, thumbnails and render views
    init.MinImageCount = kMinImageCount;
    init.ImageCount = wd->ImageCount;
    init.PipelineInfoMain.RenderPass = wd->RenderPass;
    init.PipelineInfoMain.Subpass = 0;
    init.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init.CheckVkResultFn = CheckVk;
    ImGui_ImplVulkan_Init(&init);

    auto app = std::make_unique<App>(g_vk, window);
    for (int i = 1; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "--brush") app->QueueBrushImport(argv[i + 1]); // leorix.exe --brush stamp.png
    if (!demoMesh.empty()) app->OpenDemo(demoMesh, demoMode); // for automated screenshots
    if (!demoTextures.empty()) app->QueueTextureFiles(demoTextures);
    if (demoShading >= 0) app->SetShading(demoShading);
    if (demoUV) app->ShowUVEditor();
    if (!demoHdri.empty()) app->QueueHdri(demoHdri);
    if (demoPreset >= 0) app->ApplyHdriPreset(demoPreset);
    if (demoSamples > 0) app->SetRenderSamples(demoSamples);
    if (!demoRender.empty()) app->RenderToFileAndQuit(demoRender);
    if (demoSun > -999.0f) app->SetSunElevation(demoSun);
    if (demoExposure > 0.0f) app->SetExposure(demoExposure);
    const float clear[4] = { 0x14 / 255.0f, 0x17 / 255.0f, 0x1c / 255.0f, 1.0f }; // --bg #14171c

    bool done = false;
    while (!done && !app->quitRequested) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT) done = true;
            if (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && e.window.windowID == SDL_GetWindowID(window)) done = true;
        }
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) { SDL_Delay(10); continue; }

        int fw, fh;
        SDL_GetWindowSizeInPixels(window, &fw, &fh);
        if (fw > 0 && fh > 0 && (g_swapchainRebuild || wd->Width != fw || wd->Height != fh)) {
            ImGui_ImplVulkan_SetMinImageCount(kMinImageCount);
            ImGui_ImplVulkanH_CreateOrResizeWindow(g_vk.instance, g_vk.physical, g_vk.device, wd, g_vk.queueFamily, nullptr, fw, fh, kMinImageCount, 0);
            wd->FrameIndex = 0;
            g_swapchainRebuild = false;
        }

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        app->Frame();
        ImGui::Render();

        ImDrawData* dd = ImGui::GetDrawData();
        bool minimized = dd->DisplaySize.x <= 0.0f || dd->DisplaySize.y <= 0.0f;
        for (int i = 0; i < 4; ++i) wd->ClearValue.color.float32[i] = clear[i];
        if (!minimized) FrameRender(wd, dd);
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }
        if (!minimized) FramePresent(wd);
    }

    CheckVk(vkDeviceWaitIdle(g_vk.device));
    app.reset();                            // release GPU textures while the ImGui backend is still alive
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    ImGui_ImplVulkanH_DestroyWindow(g_vk.instance, g_vk.device, wd, nullptr);
    vkDestroySurfaceKHR(g_vk.instance, surface, nullptr);
    g_vk.Shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
