// Leorix Native - entry point: SDL3 window, Vulkan swapchain, Dear ImGui frame loop.
#include "app/App.h"
#include "app/Theme.h"
#include "mesh/IO.h"
#include "mesh/EditOps.h"
#include "mesh/Primitives.h"
#include "mesh/Unwrap.h"
#include "mesh/UVEdit.h"
#include "mesh/Multires.h"
#include "tools/Script.h"
#include "render/PathTracer.h"
#include "render/HdriPresets.h"
#include <fstream>
#include <sstream>
#include "tools/Sculpt.h"
#include "tools/VisualScript.h"
#include "tools/Snap.h"
#include "anim/Armature.h"
#include "anim/WeightPaint.h"
#include "anim/ArmatureLink.h"
#include "physics/Physics.h"
#include "anim/Animation.h"
#include "paint/Project.h"
#include "miniz.h"
#include <cstring>
#define TINYGLTF_NO_STB_IMAGE        // match IO.cpp, which holds the implementation
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include "tiny_gltf.h"
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

static bool SceneFromScript(const std::string& code, Scene& scene, RayTraceWorld& w, HdriImage& hdri,
                            RayTraceSettings& st, std::string& log);

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
    // Lights are picked by power: a huge dim emitter next to a small bright lamp must not bias the
    // result, and a material that opts out of light sampling must still add its glow by BSDF hits.
    {
        Scene scene;
        MeshObject* fl = scene.AddMesh(BuildPrimitive(Primitive::Plane, pp), "Floor");
        fl->transform.scale = {4, 1, 4};
        MeshObject* lamp = scene.AddMesh(BuildPrimitive(Primitive::Plane, pp), "Lamp");
        lamp->transform.position = {0, 1.5f, 0};
        lamp->transform.scale = {0.2f, 1, 0.2f};
        for (int c = 0; c < 3; ++c) lamp->material.emissionColor[c] = 1.0f;
        lamp->material.emissiveStrengthRT = 30.0f;
        MeshObject* card = scene.AddMesh(BuildPrimitive(Primitive::Plane, pp), "Card");
        card->transform.position = {0, 6.0f, 0};
        card->transform.scale = {12, 1, 12};
        for (int c = 0; c < 3; ++c) card->material.emissionColor[c] = 0.05f;
        RayTraceWorld world; world.sunStrength = 0.0f; world.skyStrength = 0.0f;
        auto render = [&](int spp, bool nee, int seed) {
            RayTraceSettings st;
            st.width = 40; st.height = 24; st.samples = spp; st.maxBounces = 2;
            st.denoise = false; st.clampEnabled = false; st.lightSampling = nee; st.seed = seed; st.threads = 4;
            PathTracer pt;
            pt.Start(scene, scene.Camera(), 0.0f, 1.2f, 5.0f, {0, 0, 0}, false, st, world);
            while (pt.Running()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            double sum = 0; for (size_t i = 0; i < pt.Pixels().size(); i += 4) sum += pt.Pixels()[i];
            return sum / (pt.Pixels().size() / 4);
        };
        double ref = render(2048, false, 1), nee = render(64, true, 2);
        card->material.sampleAsLight = false;
        double optOut = render(256, true, 3);
        std::printf("  power-sampled lights mean ref=%.2f nee=%.2f card-not-sampled=%.2f\n", ref, nee, optOut);
        check(std::fabs(nee - ref) < 0.04 * ref, "light sampling by power matches the reference");
        check(std::fabs(optOut - ref) < 0.04 * ref, "a light opted out of sampling still lights by BSDF hits");
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
    // Snapping: rotate/scale increments and geometry targets
    {
        const float deg = 3.14159265f / 180.0f;
        check(std::fabs(SnapAngle(11.5f * deg, false) - 15 * deg) < 1e-5f && std::fabs(SnapAngle(11.5f * deg, true) - 10 * deg) < 1e-5f &&
              std::fabs(SnapAngle(-22.0f * deg, false) + 15 * deg) < 1e-5f, "snap: rotate 15 / 5 degree steps");
        check(std::fabs(SnapScale(1.234f) - 1.2f) < 1e-5f && std::fabs(SnapScale(0.01f) - 0.1f) < 1e-5f, "snap: scale 0.1 steps");
        Scene sc;
        sc.AddMesh(BuildPrimitive(Primitive::Cube, DefaultParams(Primitive::Cube)), "Target")->transform.position = {5, 0, 0};
        sc.AddMesh(BuildPrimitive(Primitive::Cube, DefaultParams(Primitive::Cube)), "Moving");   // skipped
        auto near = [](Float3 a, Float3 b) { return std::fabs(a.x - b.x) + std::fabs(a.y - b.y) + std::fabs(a.z - b.z) < 1e-4f; };
        Float3 got;
        check(SnapToGeometry(sc, 1, SnapTo::Vertex, {5.9f, 0.8f, 1.2f}, got) && near(got, {6, 1, 1}), "snap: nearest vertex");
        check(SnapToGeometry(sc, 1, SnapTo::EdgeCenter, {5.9f, 0.1f, 1.2f}, got) && near(got, {6, 0, 1}), "snap: edge center");
        check(SnapToGeometry(sc, 1, SnapTo::Edge, {6.3f, 1.4f, 0.1f}, got) && near(got, {6, 1, 0.1f}), "snap: closest point on edge");
        check(SnapToGeometry(sc, 1, SnapTo::Face, {6.5f, 0.2f, 0.3f}, got) && near(got, {6, 0.2f, 0.3f}), "snap: closest point on face");
        check(!SnapToGeometry(sc, 1, SnapTo::Increment, {0, 0, 0}, got), "snap: increment is not a geometry target");
    }
    // Skinned GLB export: 2-bone rig with keys reloads with skin, joints, weights and animation
    {
        Mesh body = BuildPrimitive(Primitive::Cube, DefaultParams(Primitive::Cube));
        Armature arm;
        arm.AddBone("Root", {0, -1, 0}, {0, 0, 0});
        arm.AddBone("Tip", {0, 0, 0}, {0, 1, 0}, 0);
        arm.BindAutomaticWeights(body);
        arm.bones[0].poseLocation = {0.2f, 0, 0};
        arm.bones[0].poseRotation = {0.3f, 0, 0.1f};
        arm.bones[1].poseRotation = {0.1f, 0.4f, 0.5f};
        arm.bones[1].poseScale = {1.1f, 1, 1};
        Action act;
        act.Find(ChannelTarget::Bone, 1, ChannelProp::Rotation, 2).Insert(1, 0.0f);
        act.Find(ChannelTarget::Bone, 1, ChannelProp::Rotation, 2).Insert(10, 1.0f);
        std::string glb = (std::filesystem::temp_directory_path() / "leorix_selftest_skin.glb").string();
        std::string err;
        check(ExportMeshAnimated(glb, body, &arm, &act, 24, err), "skinned glb export");
        tinygltf::Model gm; tinygltf::TinyGLTF loader; std::string gerr, gwarn;
        bool loaded = loader.LoadBinaryFromFile(&gm, &gerr, &gwarn, glb);
        check(loaded && gm.skins.size() == 1 && gm.skins[0].joints.size() == 2 && !gm.animations.empty() &&
              gm.animations[0].channels.size() == 1 && gm.nodes[0].skin == 0, "skinned glb reloads with 1 skin, 2 joints, 1 animation");
        bool sums = loaded && !gm.meshes.empty();
        if (sums) {
            const tinygltf::Primitive& pr = gm.meshes[0].primitives[0];
            auto it = pr.attributes.find("WEIGHTS_0");
            sums = it != pr.attributes.end();
            if (sums) {
                const tinygltf::Accessor& a = gm.accessors[it->second];
                const tinygltf::BufferView& bv = gm.bufferViews[a.bufferView];
                const float* w = reinterpret_cast<const float*>(gm.buffers[bv.buffer].data.data() + bv.byteOffset + a.byteOffset);
                sums = a.count == body.VertexCount();
                for (size_t v = 0; sums && v < a.count; ++v) sums = std::fabs(w[v * 4] + w[v * 4 + 1] + w[v * 4 + 2] + w[v * 4 + 3] - 1) < 1e-4f;
            }
        }
        check(sums, "skinned glb weights sum to 1");
        // joint world * inverse bind must equal Armature::Evaluate, or the pose would come out wrong
        bool same = loaded && gm.skins.size() == 1;
        if (same) {
            auto rotate = [](const std::vector<double>& q, Float3 v) {   // v' = q v q*
                double x = q[0], y = q[1], z = q[2], w = q[3];
                double tx = 2 * (y * v.z - z * v.y), ty = 2 * (z * v.x - x * v.z), tz = 2 * (x * v.y - y * v.x);
                return Float3{float(v.x + w * tx + y * tz - z * ty), float(v.y + w * ty + z * tx - x * tz), float(v.z + w * tz + x * ty - y * tx)};
            };
            auto local = [&](int node, Float3 p) {   // T * R * S
                const tinygltf::Node& n = gm.nodes[node];
                p = {float(p.x * n.scale[0]), float(p.y * n.scale[1]), float(p.z * n.scale[2])};
                p = rotate(n.rotation, p);
                return Float3{float(p.x + n.translation[0]), float(p.y + n.translation[1]), float(p.z + n.translation[2])};
            };
            std::vector<Mat4> mats; arm.Evaluate(mats);
            Float3 probe{0.3f, 0.7f, -0.4f};
            for (int b = 0; b < 2; ++b) {
                Float3 p{probe.x - arm.bones[b].head.x, probe.y - arm.bones[b].head.y, probe.z - arm.bones[b].head.z};   // IBM
                if (b == 1) p = local(gm.skins[0].joints[1], p);
                p = local(gm.skins[0].joints[0], p);
                Float3 want = mats[b].Apply(probe);
                same = same && std::fabs(p.x - want.x) + std::fabs(p.y - want.y) + std::fabs(p.z - want.z) < 1e-4f;
            }
        }
        check(same, "skinned glb joints reproduce the armature pose");
        std::filesystem::remove(glb);
    }
    // Rigging: two-bone IK, constraints and naming presets
    {
        auto near3 = [](Float3 a, Float3 b, float tol) {
            return std::fabs(a.x - b.x) < tol && std::fabs(a.y - b.y) < tol && std::fabs(a.z - b.z) < tol;
        };
        Armature arm;
        int up = arm.AddBone("upper", {0, 0, 0}, {0, 1, 0});
        int lo = arm.AddBone("lower", {0, 1, 0}, {0, 2, 0}, up);
        int tgt = arm.AddBone("target", {1, 1, 0}, {1, 1.2f, 0});
        BoneConstraint ik; ik.type = BoneConstraint::IK; ik.target = tgt;
        arm.bones[lo].constraints.push_back(ik);
        std::vector<Mat4> m;
        arm.Evaluate(m);
        Float3 end = m[lo].Apply(arm.bones[lo].tail), joint = m[up].Apply(arm.bones[up].tail);
        check(near3(end, {1, 1, 0}, 1e-3f), "ik: reaches a reachable target");
        check(near3(m[lo].Apply(arm.bones[lo].head), joint, 1e-3f), "ik: chain stays connected");
        arm.bones[tgt].poseLocation = {2, -1, 0};            // target head now at (3, 0, 0), out of reach
        arm.Evaluate(m);
        check(near3(m[lo].Apply(arm.bones[lo].tail), {2, 0, 0}, 1e-3f), "ik: unreachable target gives full extension toward it");

        Armature one;
        one.AddBone("b", {0, 0, 0}, {0, 1, 0});
        BoneConstraint lim; lim.type = BoneConstraint::LimitRotation;
        lim.minRot = {-0.5f, -0.5f, -0.5f}; lim.maxRot = {0.5f, 0.5f, 0.5f};
        one.bones[0].constraints.push_back(lim);
        one.bones[0].poseRotation = {0, 0, 1.5f};
        one.Evaluate(m);
        check(near3(m[0].Apply({0, 1, 0}), Mat4::RotateXYZ({0, 0, 0.5f}).Apply({0, 1, 0}), 1e-4f), "limit rotation clamps");
        one.bones[0].constraints[0] = BoneConstraint{};
        one.bones[0].constraints[0].type = BoneConstraint::TrackTo;
        one.bones[0].constraints[0].point = {3, 0, 0};
        one.Evaluate(m);
        check(near3(m[0].Apply({0, 1, 0}), {1, 0, 0}, 1e-4f), "track to aims the bone at the target");

        Armature rig;
        AddHumanoidRig(rig, {-0.5f, 0, -0.2f}, {0.5f, 2, 0.2f}, RigNaming::Plain);
        auto has = [&](const char* n) { for (const Bone& b : rig.bones) if (b.name == n) return true; return false; };
        bool parentsOk = true;
        for (int i = 0; i < (int)rig.bones.size(); ++i) parentsOk &= rig.bones[i].parent < i;
        check(rig.bones.size() == 21 && parentsOk && has("upper_arm.L") && has("shin.R"), "humanoid rig: 21 bones, plain names");
        int renamed = ApplyRigNaming(rig, RigNaming::UE5);
        check(renamed == 20 && has("upperarm_l") && has("calf_r") && has("pelvis"), "rig naming: plain -> UE5");
        ApplyRigNaming(rig, RigNaming::Mixamo);
        check(has("mixamorig:LeftForeArm") && has("mixamorig:RightUpLeg"), "rig naming: UE5 -> Mixamo");

        // IK is solved against the constrained parent chain: Copy Location on the root moves the shoulder first
        Armature moved;
        int r0 = moved.AddBone("root", {0, 0, 0}, {0, 0.5f, 0});
        int u1 = moved.AddBone("upper", {0, 0.5f, 0}, {0, 1.5f, 0}, r0);
        int l2 = moved.AddBone("lower", {0, 1.5f, 0}, {0, 2.5f, 0}, u1);
        BoneConstraint cl; cl.type = BoneConstraint::CopyLocation; cl.point = {0.5f, 0, 0};
        moved.bones[r0].constraints.push_back(cl);
        BoneConstraint ik2; ik2.type = BoneConstraint::IK; ik2.point = {1, 1.5f, 0};
        moved.bones[l2].constraints.push_back(ik2);
        moved.Evaluate(m);
        check(near3(m[l2].Apply(moved.bones[l2].tail), {1, 1.5f, 0}, 1e-3f) &&
              near3(m[u1].Apply(moved.bones[u1].head), {0.5f, 0.5f, 0}, 1e-3f) &&
              near3(m[l2].Apply(moved.bones[l2].head), m[u1].Apply(moved.bones[u1].tail), 1e-3f),
              "ik: reaches the target after Copy Location moved the root");
        // two IKs on one chain run in order and keep it connected (no twist from an overwritten correction)
        Armature two;
        int a0 = two.AddBone("a", {0, 0, 0}, {0, 1, 0});
        int b1 = two.AddBone("b", {0, 1, 0}, {0, 2, 0}, a0);
        int c2 = two.AddBone("c", {0, 2, 0}, {0, 3, 0}, b1);
        BoneConstraint ikb; ikb.type = BoneConstraint::IK; ikb.point = {1, 1, 0};
        two.bones[b1].constraints.push_back(ikb);
        two.bones[c2].constraints.push_back(ikb);
        two.Evaluate(m);
        check(near3(m[b1].Apply(two.bones[b1].head), m[a0].Apply(two.bones[a0].tail), 1e-3f) &&
              near3(m[c2].Apply(two.bones[c2].head), m[b1].Apply(two.bones[b1].tail), 1e-3f) &&
              near3(m[c2].Apply(two.bones[c2].tail), {1, 1, 0}, 1e-3f), "ik: two IKs on one chain stay connected");

        // An IK-posed rig exports as posed: each joint chain * inverse bind equals Armature::Evaluate
        Mesh body = BuildPrimitive(Primitive::Cube, DefaultParams(Primitive::Cube));
        arm.bones[tgt].poseLocation = {0, 0, 0};
        arm.BindAutomaticWeights(body);
        Action act;
        act.Find(ChannelTarget::Bone, tgt, ChannelProp::Location, 1).Insert(1, 0.0f);
        act.Find(ChannelTarget::Bone, tgt, ChannelProp::Location, 1).Insert(5, -0.5f);
        std::string glb = (std::filesystem::temp_directory_path() / "leorix_selftest_ik.glb").string();
        std::string err;
        bool ok = ExportMeshAnimated(glb, body, &arm, &act, 24, err);
        tinygltf::Model gm; tinygltf::TinyGLTF loader; std::string gerr, gwarn;
        ok = ok && loader.LoadBinaryFromFile(&gm, &gerr, &gwarn, glb) && gm.skins.size() == 1 && gm.skins[0].joints.size() >= 3;
        check(ok && gm.animations.size() == 1 && gm.animations[0].channels.size() == 9, "ik export: every joint baked when constrained");
        if (ok) {
            auto rotate = [](const std::vector<double>& q, Float3 v) {
                double x = q[0], y = q[1], z = q[2], w = q[3];
                double tx = 2 * (y * v.z - z * v.y), ty = 2 * (z * v.x - x * v.z), tz = 2 * (x * v.y - y * v.x);
                return Float3{float(v.x + w * tx + y * tz - z * ty), float(v.y + w * ty + z * tx - x * tz), float(v.z + w * tz + x * ty - y * tx)};
            };
            auto local = [&](int node, Float3 p) {
                const tinygltf::Node& n = gm.nodes[node];
                p = {float(p.x * n.scale[0]), float(p.y * n.scale[1]), float(p.z * n.scale[2])};
                p = rotate(n.rotation, p);
                return Float3{float(p.x + n.translation[0]), float(p.y + n.translation[1]), float(p.z + n.translation[2])};
            };
            arm.Evaluate(m);
            Float3 probe{0.3f, 0.7f, -0.4f};
            for (int b = 0; b < (int)arm.bones.size() && ok; ++b) {
                Float3 p{probe.x - arm.bones[b].head.x, probe.y - arm.bones[b].head.y, probe.z - arm.bones[b].head.z};
                for (int k = b; k >= 0; k = arm.bones[k].parent) p = local(gm.skins[0].joints[k], p);
                ok = near3(p, m[b].Apply(probe), 1e-4f);
            }
        }
        check(ok, "ik export: joints reproduce the constrained pose");
        std::filesystem::remove(glb);

        check(near3(AxisMask({1, 2, 3}, 0), {1, 0, 0}, 1e-6f) && near3(AxisMask({1, 2, 3}, 2), {0, 0, 3}, 1e-6f) &&
              near3(AxisMask({1, 2, 3}, -1), {1, 2, 3}, 1e-6f), "snap: axis constraint survives geometry snapping");
    }
    // Multires (Subdivision Surface) keeps the model's UVs, so painting and textures still work on it
    {
        Mesh cube = BuildPrimitive(Primitive::Cube, DefaultParams(Primitive::Cube));
        SmartUVProject(cube);
        Multires mr; mr.Rebuild(cube, 2);
        Mesh e = mr.Evaluate();
        bool inside = !e.tris.empty() && e.HasLoopUVs();
        for (const Float2& q : e.loopUVs) inside &= q.u >= -1e-5f && q.u <= 1 + 1e-5f && q.v >= -1e-5f && q.v <= 1 + 1e-5f;
        check(inside && e.tris.size() == 6 * 16 * 2, "multires keeps loop UVs (96 quads, all in 0..1)");
    }
    // Smart UV Project, UV island editing and painting on the model
    {
        Mesh cube = BuildPrimitive(Primitive::Cube, DefaultParams(Primitive::Cube));
        UnwrapResult sr = SmartUVProject(cube);
        bool inside = cube.HasLoopUVs();
        for (const Float2& q : cube.loopUVs) inside &= q.u >= 0 && q.u <= 1 && q.v >= 0 && q.v <= 1;
        UVLayout lay; lay.Build(cube);
        std::vector<int> isl;
        int count = UVIslands(cube, isl);
        check(sr.ok && sr.islands == 6 && inside && lay.islands == 6 && count == 6 && cube.seamEdges.empty(),
              "smart UV project: cube gives 6 separate islands inside 0..1, seams untouched");

        std::vector<uint8_t> sel;
        SelectUVIsland(cube, isl, 0, sel);
        std::vector<Float2> base = cube.loopUVs;
        TransformUVSelection(cube, base, sel, {0.1f, 0.0f}, 0.0f, {1, 1}, {0, 0});
        bool moved = true, rest = true; size_t n = 0;
        for (size_t c = 0; c < base.size(); ++c) {
            float du = cube.loopUVs[c].u - base[c].u, dv = cube.loopUVs[c].v - base[c].v;
            if (sel[c]) { ++n; moved &= std::fabs(du - 0.1f) < 1e-5f && std::fabs(dv) < 1e-5f; }
            else rest &= du == 0.0f && dv == 0.0f;
        }
        check(n == 6 && moved && rest, "moving a UV island shifts only its corners");
        TransformUVSelection(cube, base, sel, {0, 0}, 3.14159265f, {1, 1}, {0.5f, 0.5f});
        Float2 c0 = cube.loopUVs[0];
        check(std::fabs(c0.u - (1.0f - base[0].u)) < 1e-4f && std::fabs(c0.v - (1.0f - base[0].v)) < 1e-4f,
              "rotating UVs 180 degrees about the centre");

        // a 2 x 2 plane on y = 0 whose UVs follow x and z; a ray straight down onto (0.5, 0, -0.5)
        Mesh plane;
        plane.positions = {{-1, 0, -1}, {1, 0, -1}, {1, 0, 1}, {-1, 0, 1}};
        plane.tris = {{0, 1, 2, 0}, {0, 2, 3, 0}};
        plane.polyCount = 1;
        for (const Tri& t : plane.tris)
            for (uint32_t v : {t.a, t.b, t.c}) plane.loopUVs.push_back({(plane.positions[v].x + 1) * 0.5f, (plane.positions[v].z + 1) * 0.5f});
        Float3 ro{0.5f, 1.0f, -0.5f}, rd{0, -1, 0};
        float tHit = -ro.y / rd.y;
        Float3 hit{ro.x + rd.x * tHit, ro.y + rd.y * tHit, ro.z + rd.z * tHit};
        Float2 uv{};
        bool got = SurfaceUV(plane, 0, hit, uv);
        check(got && std::fabs(uv.u - 0.75f) < 1e-5f && std::fabs(uv.v - 0.25f) < 1e-5f, "paint on model: hit point to UV");
        check(std::fabs(TexelsPerUnit(plane, 0, 256, 256) - 128.0f) < 1e-3f, "paint on model: texel density");

        Layer layer; layer.Fill(0xFF000000u, 256, 256);
        StrokeUndo su; su.Begin(0, 0, 256, 256);
        const float px = uv.u * 256, py = (1.0f - uv.v) * 256;
        su.BackupRect(layer, (int)px - 8, (int)py - 8, (int)px + 8, (int)py + 8);
        BrushSettings bs; bs.size = 10; bs.hardness = 100;
        DirtyRect dr;
        StampBrush(layer, 256, 256, px, py, bs, 0xFFFFFFFFu, false, dr);
        auto at = [&](int x, int y) { return layer.pixels[((size_t)y * 256 + x) * 4]; };
        check(at((int)px, (int)py) == 255 && at(64, 64) == 0 && su.tiles.size() == 1 && su.tiles[0] == 3,
              "paint on model: dab lands in the right tile");
    }
    // Curves: Bezier evaluation, handle types, tube sweep, frames, taper, cyclic, extrude/delete, save/load
    {
        auto near3 = [](Float3 a, Float3 b, float tol) { return std::fabs(a.x - b.x) < tol && std::fabs(a.y - b.y) < tol && std::fabs(a.z - b.z) < tol; };
        CurvePath c;
        c.points = {{0, 0, 0}, {3, 0, 0}};
        c.handleType = {HandleType::Free, HandleType::Free};
        c.handleL = {{-1, 0, 0}, {3, 3, 0}};
        c.handleR = {{0, 3, 0}, {4, 0, 0}};
        c.resolution = 2;
        std::vector<Float3> ev = c.Evaluate();
        check(ev.size() == 3 && near3(ev[0], {0, 0, 0}, 1e-5f) && near3(ev[1], {1.5f, 2.25f, 0}, 1e-5f) && near3(ev[2], {3, 0, 0}, 1e-5f),
              "curve: bezier endpoints and midpoint");

        CurvePath z;
        z.points = {{0, 0, 0}, {1, 1, 0}, {2, 0, 0}, {3, 1, 0}};
        z.Sync();
        bool smooth = true;
        for (int i = 1; i < 3; ++i) {
            Float3 a{z.points[i].x - z.handleL[i].x, z.points[i].y - z.handleL[i].y, z.points[i].z - z.handleL[i].z};
            Float3 b{z.handleR[i].x - z.points[i].x, z.handleR[i].y - z.points[i].y, z.handleR[i].z - z.points[i].z};
            float cr = a.x * b.y - a.y * b.x, dt = a.x * b.x + a.y * b.y;
            smooth = smooth && std::fabs(cr) < 1e-5f && dt > 0;
        }
        check(smooth, "curve: auto handles are smooth (collinear through the point)");
        z.SetElem(1 * 3 + 1, {0.5f, 1.5f, 0});   // drag point 1's left handle
        Float3 hl = z.handleL[1], hr = z.handleR[1], p1 = z.points[1];
        float cr = (p1.x - hl.x) * (hr.y - p1.y) - (p1.y - hl.y) * (hr.x - p1.x);
        check(z.handleType[1] == HandleType::Aligned && std::fabs(cr) < 1e-5f && near3(hl, {0.5f, 1.5f, 0}, 1e-6f),
              "curve: dragging an auto handle makes it aligned and keeps the other side opposite");
        z.handleType[2] = HandleType::Vector;
        z.Sync();
        check(near3(z.handleR[2], {2 + 1.0f / 3, 1.0f / 3, 0}, 1e-5f), "curve: vector handle points a third of the way to the next point");

        CurvePath t;
        t.points = {{0, 0, 0}, {2, 0, 0}, {4, 1, 0}};
        t.resolution = 4; t.bevelDepth = 0.2f; t.bevelResolution = 2;   // 8 sides
        t.radius = {1.0f, 0.5f, 2.0f};
        Mesh tube = CurveToMesh(t);
        check(tube.positions.size() == 9 * 8 && tube.polyCount == 8 * 8 + 2 && tube.tris.size() == 64 * 2 + 2 * 6 && tube.HasLoopUVs(),
              "curve: open tube sweep has rings x sides verts, quads plus two caps");
        std::vector<Float3> tp = t.Evaluate();
        auto ringR = [&](int ring) { Float3 v = tube.positions[ring * 8]; Float3 c0 = tp[ring];
            return std::sqrt((v.x - c0.x) * (v.x - c0.x) + (v.y - c0.y) * (v.y - c0.y) + (v.z - c0.z) * (v.z - c0.z)); };
        check(std::fabs(ringR(0) - 0.2f) < 1e-4f && std::fabs(ringR(4) - 0.1f) < 1e-4f && std::fabs(ringR(8) - 0.4f) < 1e-4f,
              "curve: taper scales the tube radius per point");

        CurvePath ring;
        for (int i = 0; i < 4; ++i) { float a = i * 1.5707963f; ring.points.push_back({std::cos(a), 0, std::sin(a)}); }
        ring.cyclic = true; ring.resolution = 4; ring.bevelDepth = 0.1f; ring.bevelResolution = 2;
        std::vector<Float3> rp = ring.Evaluate();
        Mesh torus = CurveToMesh(ring);
        check(rp.size() == 17 && near3(rp.front(), rp.back(), 1e-6f) && torus.positions.size() == 16 * 8 && torus.polyCount == 16 * 8,
              "curve: cyclic curve closes, its tube has no caps");

        CurvePath helix;
        for (int i = 0; i < 20; ++i) { float a = i * 0.6f; helix.points.push_back({std::cos(a), i * 0.15f, std::sin(a)}); }
        std::vector<Float3> hp = helix.Evaluate();
        std::vector<Float3> hn = CurveFrameNormals(hp, false);
        bool steady = hn.size() == hp.size();
        for (size_t i = 1; steady && i < hn.size(); ++i) steady = hn[i].x * hn[i - 1].x + hn[i].y * hn[i - 1].y + hn[i].z * hn[i - 1].z > 0.9f;
        check(steady, "curve: parallel-transport frames never flip along a helix");

        CurvePath e = t;
        check(ExtrudeCurveEnd(e, 2) == 3 && e.points.size() == 4 && near3(e.points[3], {6, 2, 0}, 1e-6f) && e.radius.size() == 4 &&
              ExtrudeCurveEnd(e, 1) == -1 && ExtrudeCurveEnd(e, 0) == 0 && e.points.size() == 5,
              "curve: extrude adds points past either end only");
        CurvePath two = c;
        check(DeleteCurvePoints(e, {1, 2}) == 2 && e.points.size() == 3 && DeleteCurvePoints(two, {0}) == 0 && two.points.size() == 2,
              "curve: delete removes points but keeps at least two");

        namespace fs = std::filesystem;
        const std::string proj = (fs::temp_directory_path() / "leorix_selftest_curve.leorix").string();
        Scene sc;
        MeshObject* co = sc.AddMesh(Mesh{}, "Tube");
        co->isCurve = true;
        co->curve = t;
        co->curve.Sync();
        co->curve.SetElem(2, {0.5f, 1.0f, 0});   // point 0 right handle -> aligned
        co->curve.handleType[2] = HandleType::Free;
        co->curve.handleL[2] = {3, 3, 3};
        co->curve.cyclic = false;
        co->mesh = CurveToMesh(co->curve);
        PaintProject pp; pp.New(16, 16);
        BrushSettings br;
        PlaybackState pb;
        bool saved = SaveLeorixProject(proj, pp, br, {}, &sc, &pb).ok;
        Scene back; PaintProject pp2; PlaybackState pb2;
        bool loaded = saved && LoadLeorixProject(proj, pp2, br, g_vk, nullptr, &back, &pb2).ok && back.objects.size() == 1;
        bool same = loaded;
        if (same) {
            const CurvePath& b = back.objects[0]->curve;
            same = back.objects[0]->isCurve && b.points.size() == 3 && b.bevelDepth == 0.2f && b.bevelResolution == 2 &&
                   b.handleType[0] == HandleType::Aligned && b.handleType[2] == HandleType::Free &&
                   near3(b.handleL[2], {3, 3, 3}, 1e-6f) && near3(b.handleR[0], {0.5f, 1.0f, 0}, 1e-6f) &&
                   b.radius[1] == 0.5f && back.objects[0]->mesh.tris.size() == co->mesh.tris.size();
        }
        check(same, "curve: handles, types, taper and bevel survive a project round trip");
        fs::remove(proj);

        // NURBS: order 2 is a polyline, endpoint knots reach the ends, the circle is exact
        CurvePath poly; poly.nurbs = true; poly.order = 2; poly.resolution = 4;
        poly.points = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {3, 1, 0}};
        std::vector<Float3> ps = poly.Evaluate();
        bool onPoly = ps.size() == 13;
        for (int s = 0; onPoly && s < 13; ++s) {
            int seg = std::min(s / 4, 2); float f = (s - seg * 4) / 4.0f;
            Float3 a = poly.points[seg], b = poly.points[seg + 1];
            onPoly = near3(ps[s], {a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, a.z + (b.z - a.z) * f}, 1e-5f);
        }
        check(onPoly, "nurbs: order 2 is the polyline through its points");
        CurvePath ep; ep.nurbs = true; ep.order = 4;
        ep.points = {{0, 0, 0}, {1, 2, 0}, {3, -1, 1}, {4, 0, 0}, {5, 1, 2}};
        ep.weight = {1, 2, 0.5f, 1, 1};
        std::vector<Float3> es = ep.Evaluate();
        check(near3(es.front(), ep.points.front(), 1e-5f) && near3(es.back(), ep.points.back(), 1e-5f), "nurbs: endpoint knots pass through the end points");
        CurvePath uni = ep; uni.endpoint = false;
        check(!near3(uni.Evaluate().front(), ep.points.front(), 1e-3f), "nurbs: uniform knots stay inside the end points");
        CurvePath cyc = ep; cyc.cyclic = true;
        std::vector<Float3> cc = cyc.Evaluate();
        check(cc.size() == 5 * 12 + 1 && near3(cc.front(), cc.back(), 1e-6f), "nurbs: cyclic curve closes");
        CurvePath circ; SetNurbsCircle(circ, 1.0f); circ.resolution = 16;
        std::vector<Float3> cp = circ.Evaluate();
        bool round = cp.size() > 64;
        for (const Float3& p : cp) round = round && std::fabs(std::sqrt(p.x * p.x + p.z * p.z) - 1) < 1e-4f && std::fabs(p.y) < 1e-6f;
        check(round, "nurbs: circle points all lie at radius 1");

        // box select: points and Bezier handles inside the box; NURBS has no handles
        CurvePath bx; bx.points = {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}}; bx.Sync();
        auto inX = [](const Float3& p) { return p.x >= 0.9f && p.x <= 2.1f; };
        check(CurveElemsInside(bx, inX) == std::vector<int>{3, 5, 6, 7}, "curve: box select picks the points and handles inside");
        bx.nurbs = true;
        check(CurveElemsInside(bx, inX) == std::vector<int>{3, 6}, "nurbs: box select picks only points");

        // bevel profile: the cross-section is the profile curve's shape
        CurvePath rail; rail.points = {{0, 0, 0}, {0, 1, 0}, {0, 2, 0}}; rail.resolution = 4;   // 9 rings
        CurvePath vee; vee.nurbs = true; vee.order = 2; vee.resolution = 1;
        vee.points = {{-0.1f, 0, 0}, {0, 0, 0.1f}, {0.1f, 0, 0}};                              // open, 3 points
        Mesh pm = CurveToMesh(rail, &vee);
        check(pm.positions.size() == 3 * 9 && pm.polyCount == 2 * 8, "curve: open profile sweep is profile points x rings, no caps");
        CurvePath ringProf; SetNurbsCircle(ringProf, 0.1f); ringProf.resolution = 1;           // closed, 8 points
        Mesh rm = CurveToMesh(rail, &ringProf);
        bool onRadius = rm.positions.size() == 8 * 9 && rm.polyCount == 8 * 8 + 2;
        for (const Float3& p : rm.positions) onRadius = onRadius && std::fabs(std::sqrt(p.x * p.x + p.z * p.z) - 0.1f) < 1e-4f;
        check(onRadius, "curve: closed profile sweep is profile points x rings plus caps, at the profile's radius");

        // project round trip: NURBS fields, explicit knots and the profile reference
        const std::string proj2 = (fs::temp_directory_path() / "leorix_selftest_nurbs.leorix").string();
        Scene ns;
        MeshObject* pr = ns.AddMesh(Mesh{}, "Profile");
        pr->isCurve = true; SetNurbsCircle(pr->curve, 0.2f); pr->curve.bevelDepth = 0.05f;
        pr->mesh = CurveToMesh(pr->curve);
        MeshObject* nc = ns.AddMesh(Mesh{}, "Rail");
        nc->isCurve = true; nc->curve = ep; nc->curve.order = 3; nc->curve.bevelObject = (int)pr->id; nc->curve.Sync();
        nc->mesh = CurveToMesh(nc->curve, &pr->curve);
        bool nsaved = SaveLeorixProject(proj2, pp, br, {}, &ns, &pb).ok;
        Scene nb; PaintProject pp3; PlaybackState pb3;
        bool nsame = nsaved && LoadLeorixProject(proj2, pp3, br, g_vk, nullptr, &nb, &pb3).ok && nb.objects.size() == 2;
        if (nsame) {
            const CurvePath& r = nb.objects[1]->curve;
            const CurvePath& q = nb.objects[0]->curve;
            std::vector<Float3> a = q.Evaluate(), b = pr->curve.Evaluate();
            nsame = r.nurbs && r.order == 3 && r.endpoint && r.bevelObject == (int)nb.objects[0]->id && r.weight == nc->curve.weight &&
                    q.nurbs && q.knots == pr->curve.knots && a.size() == b.size() && near3(a[5], b[5], 1e-5f) &&
                    nb.objects[1]->mesh.positions.size() == nc->mesh.positions.size();
        }
        check(nsame, "nurbs: order, weights, knots and bevel profile survive a project round trip");
        fs::remove(proj2);
        // the profile is held by id, so deleting an earlier object doesn't re-point it
        {
            Scene ds;
            ds.AddMesh(Mesh{}, "Spacer");
            MeshObject* dp = ds.AddMesh(Mesh{}, "Profile"); dp->isCurve = true; dp->curve = pr->curve;
            MeshObject* dr = ds.AddMesh(Mesh{}, "Rail"); dr->isCurve = true; dr->curve = nc->curve; dr->curve.bevelObject = (int)dp->id;
            ds.Remove(0);
            const MeshObject* f = ds.FindById((uint32_t)dr->curve.bevelObject);
            check(f == dp, "curve: bevel profile survives deleting an earlier object");
        }
        // order 6 with explicit knots: points + 6 knots survive a round trip
        {
            Scene ks;
            MeshObject* k6 = ks.AddMesh(Mesh{}, "Order6");
            k6->isCurve = true;
            k6->curve.nurbs = true; k6->curve.order = 6; k6->curve.endpoint = false;
            for (int i = 0; i < 7; ++i) k6->curve.points.push_back({(float)i, (float)(i % 2), 0});
            k6->curve.Sync();
            k6->curve.knots.clear();
            for (int i = 0; i < 13; ++i) k6->curve.knots.push_back((float)i * 0.5f);
            Scene kb; PaintProject kp; PlaybackState kpb;
            bool kok = SaveLeorixProject(proj2, pp, br, {}, &ks, &pb).ok &&
                       LoadLeorixProject(proj2, kp, br, g_vk, nullptr, &kb, &kpb).ok && kb.objects.size() == 1 &&
                       kb.objects[0]->curve.knots == k6->curve.knots;
            check(kok, "nurbs: order 6 keeps its points + 6 explicit knots through a project round trip");
            fs::remove(proj2);
        }
    }
    // Polish: deflated undo tiles, stable modifier targets, Live Unwrap
    {
        Layer flat; flat.Fill(0xFF336699u, 256, 256);
        StrokeUndo su; su.Begin(0, 0, 256, 256);
        su.BackupRect(flat, 10, 10, 20, 20);
        const std::vector<uint8_t> raw = flat.pixels;
        DirtyRect dr; BrushSettings bs; bs.size = 8;
        StampBrush(flat, 256, 256, 15, 15, bs, 0xFFFFFFFFu, false, dr);
        const std::vector<uint8_t> painted = flat.pixels;
        su.CaptureAfter(flat);
        check(su.packed && su.Bytes() < (size_t)2 * StrokeUndo::kTile * StrokeUndo::kTile * 4 / 10, "undo tiles: deflated well below raw size");
        su.Restore(flat, true);
        bool backOk = flat.pixels == raw;
        su.Restore(flat, false);
        check(backOk && flat.pixels == painted, "undo tiles: deflated tiles restore before and after exactly");

        Scene sc;
        MeshObject* first = sc.AddMesh(BuildPrimitive(Primitive::Cube, DefaultParams(Primitive::Cube)), "First");
        MeshObject* path = sc.AddMesh(Mesh{}, "Path");
        path->isCurve = true; path->curve.points = {{0, 0, 0}, {1, 0, 0}, {2, 1, 0}};
        MeshObject* bent = sc.AddMesh(BuildPrimitive(Primitive::Cube, DefaultParams(Primitive::Cube)), "Bent");
        Modifier cd; cd.type = ModifierType::CurveDeform; cd.curveTarget = (int)path->id; bent->modifiers.push_back(cd);
        Modifier sw; sw.type = ModifierType::Shrinkwrap; sw.shrinkTarget = (int)first->id; path->modifiers.push_back(sw);
        check(first->id && path->id && bent->id && first->id != path->id && path->id != bent->id, "object ids: unique and non-zero");
        namespace fs = std::filesystem;
        const std::string proj = (fs::temp_directory_path() / "leorix_selftest_targets.leorix").string();
        PaintProject pp; pp.New(16, 16); BrushSettings br;
        bool saved = SaveLeorixProject(proj, pp, br, {}, &sc, nullptr).ok;
        sc.Remove(0);   // delete an earlier object: list indices shift, ids do not
        check(sc.IndexOfId(bent->modifiers[0].curveTarget) == sc.IndexOf(path) && sc.StackWithIndices(bent->modifiers)[0].curveTarget == 0,
              "object ids: Curve Deform keeps its curve after an earlier object is deleted");
        check(sc.IndexOfId(path->modifiers[0].shrinkTarget) == -1, "object ids: a deleted Shrinkwrap target resolves to none");
        Scene back; PaintProject pp2;
        bool loaded = saved && LoadLeorixProject(proj, pp2, br, g_vk, nullptr, &back, nullptr).ok && back.objects.size() == 3;
        check(loaded && back.IndexOfId(back.objects[2]->modifiers[0].curveTarget) == 1 &&
              back.IndexOfId(back.objects[1]->modifiers[0].shrinkTarget) == 0 && back.objects[1]->name == "Path",
              "object ids: project round trip keeps Curve Deform and Shrinkwrap targets");
        std::error_code ec; fs::remove(proj, ec);

        Mesh cube = BuildPrimitive(Primitive::Cube, DefaultParams(Primitive::Cube));
        UnwrapMesh(cube);
        const std::vector<Float2> uv0 = cube.loopUVs;
        auto sameUV = [&](const std::vector<Float2>& a) {
            if (a.size() != uv0.size()) return false;
            for (size_t i = 0; i < a.size(); ++i) if (a[i].u != uv0[i].u || a[i].v != uv0[i].v) return false;
            return true;
        };
        std::unordered_set<uint64_t> ring;
        for (const Tri& t : cube.tris) { ring.insert(Mesh::EdgeKey(t.a, t.b)); if (ring.size() >= 4) break; }
        UnwrapResult noLive = MarkSeams(cube, ring, true, false);
        bool untouched = !noLive.ok && sameUV(cube.loopUVs) && cube.seamEdges.size() == ring.size();
        cube.seamEdges.clear();
        UnwrapResult live = MarkSeams(cube, ring, true, true);
        check(untouched && live.ok && cube.seamEdges.size() == ring.size() && cube.loopUVs.size() == uv0.size() && !sameUV(cube.loopUVs),
              "live unwrap: marking seams re-unwraps only when Live Unwrap is on");
    }
    // .leorix projects keep the 3D scene: mesh, transform, material, modifier, armature, constraint, keys
    {
        namespace fs = std::filesystem;
        const std::string proj = (fs::temp_directory_path() / "leorix_selftest_scene.leorix").string();
        Scene sc;
        MeshObject* o = sc.AddMesh(BuildPrimitive(Primitive::Cube, DefaultParams(Primitive::Cube)), "Rigged");
        o->transform.position = {1, 2, 3};
        o->material.roughness = 0.25f;
        o->material.cpu[(int)MapSlot::Roughness].width = 2; o->material.cpu[(int)MapSlot::Roughness].height = 2;
        o->material.cpu[(int)MapSlot::Roughness].pixels.assign(16, 77);
        o->material.paths[(int)MapSlot::Roughness] = "rough.png";
        o->mesh.seamEdges.insert(Mesh::EdgeKey(0, 1));
        Modifier mod; mod.type = ModifierType::Array; mod.arrayCount = 3; o->modifiers.push_back(mod);
        o->armature.AddBone("upper", {0, 0, 0}, {0, 1, 0});
        o->armature.AddBone("lower", {0, 1, 0}, {0, 2, 0}, 0);
        BoneConstraint ik; ik.type = BoneConstraint::IK; ik.point = {1, 1, 0};
        o->armature.bones[1].constraints.push_back(ik);
        o->armature.BindAutomaticWeights(o->mesh);
        o->action.Find(ChannelTarget::Bone, 1, ChannelProp::Rotation, 2).Insert(10, 0.5f, Interp::Back, Easing::InOut);
        o->action.Find(ChannelTarget::Object, -1, ChannelProp::Location, 0).Insert(1, 2.0f);
        sc.AddMesh(BuildPrimitive(Primitive::Plane, DefaultParams(Primitive::Plane)), "Floor");
        sc.cameras[0].focalLengthMm = 85;
        PlaybackState pb; pb.fps = 30;
        PaintProject pp; pp.New(16, 16);
        BrushSettings br;
        check(SaveLeorixProject(proj, pp, br, {}, &sc, &pb).ok, "project: save with scene");

        Scene back; PlaybackState pb2; PaintProject pp2;
        ProjectSaveInfo r = LoadLeorixProject(proj, pp2, br, g_vk, nullptr, &back, &pb2);
        bool same = r.ok && back.objects.size() == 2 && pb2.fps == 30 && back.cameras[0].focalLengthMm == 85;
        if (same) {
            const MeshObject& b = *back.objects[0];
            const Armature& a = b.armature;
            const Channel* ch = b.action.Get(ChannelTarget::Bone, 1, ChannelProp::Rotation, 2);
            same = b.name == "Rigged" && b.mesh.positions.size() == o->mesh.positions.size() &&
                   b.mesh.tris.size() == o->mesh.tris.size() && b.mesh.polyCount == o->mesh.polyCount &&
                   b.mesh.loopUVs.size() == o->mesh.loopUVs.size() && b.mesh.IsSeam(0, 1) &&
                   b.transform.position.z == 3 && b.material.roughness == 0.25f &&
                   b.material.cpu[(int)MapSlot::Roughness].pixels == o->material.cpu[(int)MapSlot::Roughness].pixels &&
                   b.material.paths[(int)MapSlot::Roughness] == "rough.png" &&
                   b.modifiers.size() == 1 && b.modifiers[0].arrayCount == 3 &&
                   a.bones.size() == 2 && a.bones[1].parent == 0 && a.bones[1].constraints.size() == 1 &&
                   a.bones[1].constraints[0].type == BoneConstraint::IK && a.bones[1].constraints[0].point.x == 1 &&
                   a.Bound() && a.weights.size() == o->armature.weights.size() &&
                   ch && ch->keys.size() == 1 && ch->keys[0].frame == 10 && ch->keys[0].interp == Interp::Back &&
                   ch->keys[0].easing == Easing::InOut && b.action.channels.size() == 2 && back.objects[1]->name == "Floor";
        }
        check(same, "project: scene round trip (mesh, material map, modifier, armature, constraint, keys, camera)");

        // a project saved without a scene leaves the current one alone
        check(SaveLeorixProject(proj, pp, br).ok, "project: save without scene");
        r = LoadLeorixProject(proj, pp2, br, g_vk, nullptr, &back, &pb2);
        check(r.ok && back.objects.size() == 2, "project: old-style project keeps the open scene");

        // damaged manifests: wrong types, bad indices, absurd sizes, truncated mesh data
        auto writeZip = [&](const std::string& manifest, const std::string& blobName, const std::string& blob) {
            mz_zip_archive z{};
            if (!mz_zip_writer_init_file(&z, proj.c_str(), 0)) return false;
            mz_zip_writer_add_mem(&z, "manifest.json", manifest.data(), manifest.size(), 0);
            if (!blobName.empty()) mz_zip_writer_add_mem(&z, blobName.c_str(), blob.data(), blob.size(), 0);
            mz_zip_writer_finalize_archive(&z); mz_zip_writer_end(&z);
            return true;
        };
        const char* bad[] = {
            R"J([1,2,3])J",
            R"J({"canvas":"big","project":5,"brush":[1],"maps":{"x":1}})J",
            R"J({"canvas":{"width":1e12,"height":-4},"maps":[{"kind":7,"layers":[{"opacity":"x","file":9},3]},null]})J",
            R"J({"scene":{"objects":[{"name":1,"data":"objects/O1.bin","transform":{"position":["a",null,1e40]},
                "modifiers":[{"type":99,"arrayCount":1e9,"subdivLevels":-3}],
                "armature":{"bones":[{"parent":5,"constraints":[{"type":42,"target":77}]},{"parent":0}]},
                "action":{"channels":[{"target":1,"bone":9,"keys":[[1,2]]},{"keys":[["a"],[3,4,99,99],5]}]},
                "multires":{"levels":99}},7,{"data":"missing.bin"}],"cameras":[{"camera":{"clipStart":-1}}],
                "activeObject":12,"activeCamera":-5,"playback":{"fps":0}}})J"};
        const uint32_t magic = 0x424F584Cu, ver = 1;
        std::string blob(8, '\0');   // right header, then cut short
        std::memcpy(&blob[0], &magic, 4); std::memcpy(&blob[4], &ver, 4);
        bool survived = true;
        for (int k = 0; k < 4; ++k) {
            Scene s2; PlaybackState p2; PaintProject q;
            if (!writeZip(bad[k], "objects/O1.bin", blob)) { survived = false; break; }
            ProjectSaveInfo rr = LoadLeorixProject(proj, q, br, g_vk, nullptr, &s2, &p2);
            if (k == 0) survived = survived && !rr.ok;   // not an object: refused outright
            else survived = survived && rr.ok && !q.maps.empty() && q.width >= 1 && q.width <= 16384 && !s2.cameras.empty();
        }
        // a well-formed blob whose triangle points past the vertex list is refused
        {
            std::string b2;
            auto put = [&](const void* p, size_t n) { b2.append((const char*)p, n); };
            const uint32_t one = 1, zero = 0;
            put(&magic, 4); put(&ver, 4);
            put(&one, 4); Float3 v{0, 0, 0}; put(&v, sizeof v);          // 1 position
            put(&zero, 4); put(&zero, 4); put(&zero, 4);                  // normals, uvs, loopUVs
            put(&one, 4); Tri t{0, 0, 5, 0}; put(&t, sizeof t);           // 1 bad triangle
            put(&one, 4);                                                 // polyCount
            for (int k = 0; k < 5; ++k) put(&zero, 4);                    // seams, sharp, rest, weights, disp
            Scene s2; PaintProject q;
            writeZip(R"J({"scene":{"objects":[{"data":"objects/O1.bin"}]}})J", "objects/O1.bin", b2);
            ProjectSaveInfo rr = LoadLeorixProject(proj, q, br, g_vk, nullptr, &s2, nullptr);
            survived = survived && rr.ok && s2.objects.empty() && rr.message.find("skipped") != std::string::npos;
        }
        check(survived, "project: damaged manifests and mesh data load without crashing");
        std::error_code ec; fs::remove(proj, ec);
    }
    // Weight painting: Add / Subtract keep the sum at 1, Smooth averages neighbours, X mirror, Clean
    {
        auto sum = [](const VertexWeights& w) { float t = 0; for (int s = 0; s < 4; ++s) if (w.bone[s] >= 0) t += w.weight[s]; return t; };
        auto both = [](float a) { VertexWeights w; w.bone[0] = 0; w.weight[0] = a; w.bone[1] = 1; w.weight[1] = 1 - a; return w; };
        Armature arm;
        arm.AddBone("a", {0, 0, 0}, {0, 1, 0});
        arm.AddBone("b", {0, 1, 0}, {0, 2, 0});
        std::vector<Float3> pos = {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {1, 1, 0}};
        std::vector<Tri> tris = {{0, 1, 3, 0}, {1, 2, 3, 1}};
        arm.restPositions = pos;
        arm.weights = {both(1), both(0.2f), both(0), both(0.5f)};
        SculptSettings fall; fall.falloffCurve = 8;   // Constant: the full amount inside the radius
        WeightDab d; d.center = pos[1]; d.radius = 0.1f; d.strength = 1; d.bone = 0;
        d.mode = WeightBrush::Add; d.value = 0.3f;
        size_t hit = ApplyWeightDab(arm, pos, tris, fall, d);
        check(hit == 1 && std::fabs(BoneWeight(arm.weights[1], 0) - 0.5f) < 1e-5f && std::fabs(sum(arm.weights[1]) - 1) < 1e-5f,
              "weight paint: Add raises the bone and keeps the sum at 1");
        d.mode = WeightBrush::Subtract; d.value = 0.4f;
        ApplyWeightDab(arm, pos, tris, fall, d);
        check(std::fabs(BoneWeight(arm.weights[1], 0) - 0.1f) < 1e-5f && std::fabs(sum(arm.weights[1]) - 1) < 1e-5f,
              "weight paint: Subtract lowers the bone and keeps the sum at 1");
        d.mode = WeightBrush::Smooth;   // neighbours over the edges 1-0, 1-3, 1-2, 3-1: (1 + 0.5 + 0 + 0.5) / 4
        ApplyWeightDab(arm, pos, tris, fall, d);
        check(std::fabs(BoneWeight(arm.weights[1], 0) - 0.5f) < 1e-5f && std::fabs(sum(arm.weights[1]) - 1) < 1e-5f,
              "weight paint: Smooth averages the neighbours");
        check(std::fabs(BoneWeight(arm.weights[0], 0) - 1) < 1e-6f && std::fabs(BoneWeight(arm.weights[3], 0) - 0.5f) < 1e-6f,
              "weight paint: vertices outside the radius are untouched");

        Armature m;
        m.AddBone("arm.L", {1, 0, 0}, {2, 0, 0});
        m.AddBone("arm.R", {-1, 0, 0}, {-2, 0, 0});
        std::vector<Float3> mp = {{1.5f, 0.2f, 0}, {-1.5f, 0.2f, 0}};
        m.restPositions = mp;
        m.weights = {both(0.5f), both(0.5f)};
        check(MirrorBone(m, 0) == 1 && MirrorBone(m, 1) == 0, "weight paint: .L/.R twin bones");
        WeightDab md; md.center = mp[0]; md.radius = 0.1f; md.strength = 1; md.bone = 0; md.value = 0.3f; md.mirrorX = true;
        ApplyWeightDab(m, mp, {}, fall, md);
        check(std::fabs(BoneWeight(m.weights[0], 0) - 0.8f) < 1e-5f && std::fabs(BoneWeight(m.weights[1], 1) - 0.8f) < 1e-5f,
              "weight paint: X mirror paints the twin bone on the mirrored vertex");
        {   // posed (asymmetric) brush positions: the twin is still found on the rest positions
            m.weights = {both(0.5f), both(0.5f)};
            std::vector<Float3> posed = {{1.7f, 0.5f, 0.1f}, {-1.2f, 0.1f, 0}};
            WeightDab pd = md; pd.center = posed[0];
            ApplyWeightDab(m, posed, {}, fall, pd, &mp);
            check(std::fabs(BoneWeight(m.weights[1], 1) - 0.8f) < 1e-5f, "weight paint: X mirror finds the twin while posed");
        }
        {   // a vertex on the centre line is its own mirror and keeps the painted bone
            Armature cl;
            cl.AddBone("arm.L", {1, 0, 0}, {2, 0, 0});
            cl.AddBone("arm.R", {-1, 0, 0}, {-2, 0, 0});
            std::vector<Float3> cp = {{0, 0.2f, 0}};
            cl.restPositions = cp;
            cl.weights = {both(0.5f)};
            WeightDab cd = md; cd.center = cp[0]; cd.mode = WeightBrush::Replace; cd.value = 1;
            ApplyWeightDab(cl, cp, {}, fall, cd);
            check(std::fabs(BoneWeight(cl.weights[0], 0) - 1) < 1e-5f, "weight paint: centre-line vertex keeps the active bone under X mirror");
        }

        Armature c;
        c.AddBone("a", {0, 0, 0}, {0, 1, 0});
        c.AddBone("b", {0, 1, 0}, {0, 2, 0});
        c.restPositions = {{0, 0, 0}};
        c.weights = {both(0.995f)};
        size_t removed = CleanWeights(c, 0.01f);
        check(removed == 1 && c.weights[0].bone[1] == -1 && std::fabs(BoneWeight(c.weights[0], 0) - 1) < 1e-6f,
              "weight paint: Clean drops weights under 0.01 and renormalizes");
        c.weights = {both(0.6f)}; c.weights[0].weight[1] = 0.2f;   // sums to 0.8
        check(NormalizeAllWeights(c) == 1 && std::fabs(sum(c.weights[0]) - 1) < 1e-6f, "weight paint: Normalize All");
    }
    // Rigid bodies: free fall timing, resting, bounce, stacking, determinism, project round trip
    {
        const float top = 0.005f;   // a flat plane collides as a 1 cm slab
        auto ground = [](Scene& sc) {
            MeshObject* g = sc.AddMesh(BuildPrimitive(Primitive::Plane, DefaultParams(Primitive::Plane)), "Ground");
            g->transform.scale = {10, 1, 10};
            g->rigidBody.enabled = true; g->rigidBody.passive = true;
        };
        auto ballAt = [&](Scene& sc, float bounce) {
            MeshObject* b = sc.AddMesh(BuildPrimitive(Primitive::UVSphere, DefaultParams(Primitive::UVSphere)), "Ball");
            b->transform.position = {0, top + 1 + 2, 0};   // bottom 2 m above the ground
            b->rigidBody.enabled = true; b->rigidBody.shape = RigidShape::Sphere; b->rigidBody.bounciness = bounce;
            return b;
        };
        PhysicsSettings ps; ps.fps = 480; ps.frameStart = 1; ps.frameEnd = 480 * 3;
        auto landing = [&](const std::vector<RigidTransform>& c, int from) {
            for (int i = from; i < (int)c.size(); ++i) if (c[i].position.y - 1 <= top + 0.003f) return i;
            return -1;
        };
        Scene sc; ground(sc);
        MeshObject* ball = ballAt(sc, 0);
        check(BakeRigidBodies(sc, ps) == 1 && ball->rigidBody.cache.size() == 480 * 3, "physics: bake fills one entry per frame");
        {   // a huge frame range is capped so a bake can't freeze the app
            Scene sl; ground(sl);
            MeshObject* lb = ballAt(sl, 0);
            PhysicsSettings pl; pl.fps = 24; pl.frameStart = 1; pl.frameEnd = 1000000;
            BakeRigidBodies(sl, pl);
            check(lb->rigidBody.cache.size() == (size_t)kMaxBakeFrames, "physics: bake range is capped");
        }
        {   // curve edits re-evaluate the meshes bent along a curve
            Scene cs;
            MeshObject* bent = cs.AddMesh(BuildPrimitive(Primitive::Cube, DefaultParams(Primitive::Cube)), "Bent");
            MeshObject* plain = cs.AddMesh(BuildPrimitive(Primitive::Cube, DefaultParams(Primitive::Cube)), "Plain");
            MeshObject* path = cs.AddMesh(Mesh{}, "Path");
            MeshObject* other = cs.AddMesh(Mesh{}, "Other");
            Modifier cd; cd.type = ModifierType::CurveDeform; cd.curveTarget = (int)path->id; bent->modifiers.push_back(cd);
            cd.curveTarget = (int)other->id; plain->modifiers.push_back(cd);
            bent->stackDirty = plain->stackDirty = false;
            check(cs.MarkCurveDependentsDirty(*path) == 1 && bent->stackDirty && !plain->stackDirty, "curves: edits mark only that curve's Curve Deform users dirty");
        }
        const auto& c = ball->rigidBody.cache;
        int land = landing(c, 0);
        float tWant = std::sqrt(2 * 2 / 9.81f);
        std::printf("  physics: landed at %.4f s (want %.4f), rests at %.4f\n", land / 480.0f, tWant, c.back().position.y - 1 - top);
        check(land > 0 && std::fabs(land / 480.0f - tWant) < 0.02f * tWant, "physics: sphere lands at sqrt(2h/g)");
        check(std::fabs(c.back().position.y - 1 - top) < 0.01f && std::fabs(c.back().position.x) < 1e-4f, "physics: sphere rests on the ground");

        Scene sb; ground(sb);
        MeshObject* bb = ballAt(sb, 0.5f);
        BakeRigidBodies(sb, ps);
        const auto& cb = bb->rigidBody.cache;
        int l1 = landing(cb, 0);
        float peak = 0;
        for (int i = l1 + 1; l1 > 0 && i < l1 + 480 && i < (int)cb.size(); ++i) peak = std::max(peak, cb[i].position.y - 1 - top);
        std::printf("  physics: bounce 0.5 from 2 m peaks at %.3f m\n", peak);
        check(std::fabs(peak - 0.5f) < 0.1f, "physics: bounciness 0.5 rebounds to about a quarter of the height");

        Scene ss; ground(ss);
        MeshObject* boxes[3];
        for (int k = 0; k < 3; ++k) {
            boxes[k] = ss.AddMesh(BuildPrimitive(Primitive::Cube, DefaultParams(Primitive::Cube)), "Box");
            boxes[k]->transform.scale = {0.5f, 0.5f, 0.5f};                    // 1 m cubes
            boxes[k]->transform.position = {0, top + 0.5f + k, 0};
            boxes[k]->rigidBody.enabled = true;
            boxes[k]->rigidBody.shape = k == 1 ? RigidShape::ConvexHull : RigidShape::Box;
        }
        check(BakeRigidBodies(ss, ps) == 3, "physics: bake a stack of three boxes");
        float drift = 0;
        for (int k = 0; k < 3; ++k) {
            const RigidTransform& e = boxes[k]->rigidBody.cache.back();
            const Float3& r = boxes[k]->rigidBody.rest.position;
            drift = std::max({drift, std::fabs(e.position.x - r.x), std::fabs(e.position.y - r.y), std::fabs(e.position.z - r.z),
                              std::fabs(e.rotation.x), std::fabs(e.rotation.y), std::fabs(e.rotation.z)});
        }
        std::printf("  physics: stack drift after 3 s %.5f\n", drift);
        check(drift < 0.01f, "physics: a stack of three boxes stays put");

        std::vector<RigidTransform> first = boxes[2]->rigidBody.cache;
        ApplyRigidBodyCache(ss, 480 * 3);   // sit on the last frame, so the rebake must start from the rest pose
        BakeRigidBodies(ss, ps);
        const auto& second = boxes[2]->rigidBody.cache;
        check(second.size() == first.size() &&
              std::memcmp(second.data(), first.data(), first.size() * sizeof(RigidTransform)) == 0, "physics: two bakes are identical");
        ApplyRigidBodyCache(ss, 2);
        check(boxes[2]->transform.position.y == second[1].position.y, "physics: playback reads the cache");
        FreeRigidBodyBake(ss);
        check(!boxes[2]->rigidBody.Baked() && boxes[2]->transform.position.y == top + 2.5f, "physics: free bake puts objects back");

        // settings survive a project round trip; a baked object is saved at its rest pose
        namespace fs = std::filesystem;
        const std::string proj = (fs::temp_directory_path() / "leorix_selftest_physics.leorix").string();
        ball->rigidBody.mass = 3; ball->rigidBody.friction = 0.2f; ball->rigidBody.bounciness = 0.4f;
        ApplyRigidBodyCache(sc, 480 * 3);
        PaintProject pp; pp.New(16, 16); BrushSettings br; PlaybackState pb;
        bool saved = SaveLeorixProject(proj, pp, br, {}, &sc, &pb).ok;
        Scene back; PaintProject pp2;
        bool same = saved && LoadLeorixProject(proj, pp2, br, g_vk, nullptr, &back, nullptr).ok && back.objects.size() == 2;
        if (same) {
            const RigidBody& g = back.objects[0]->rigidBody; const RigidBody& b = back.objects[1]->rigidBody;
            same = g.enabled && g.passive && b.enabled && !b.passive && b.shape == RigidShape::Sphere && b.mass == 3 &&
                   b.friction == 0.2f && b.bounciness == 0.4f && back.objects[1]->transform.position.y == top + 3;
        }
        check(same, "physics: rigid body settings survive a project round trip");
        std::error_code ec; fs::remove(proj, ec);
    }
    // One armature deforming several meshes, and control rigs driving bones through object targets
    {
        auto far3 = [](Float3 a, Float3 b) { return std::fabs(a.x - b.x) + std::fabs(a.y - b.y) + std::fabs(a.z - b.z); };
        auto maxMove = [&](const MeshObject& o) {
            float m = 0;
            if (o.skinned.size() != o.mesh.positions.size()) return -1.0f;
            for (size_t i = 0; i < o.skinned.size(); ++i) m = std::max(m, far3(o.skinned[i], o.mesh.positions[i]));
            return m;
        };
        Scene sc;
        sc.AddMesh(BuildPrimitive(Primitive::Plane, DefaultParams(Primitive::Plane)), "Spacer");   // deleted later: indices shift
        MeshObject* rig = sc.AddMesh(Mesh{}, "Rig");
        rig->transform.position = {1, 0, 0};
        int root = rig->armature.AddBone("root", {0, 0, 0}, {0, 1, 0});
        int tip = rig->armature.AddBone("tip", {0, 1, 0}, {0, 2, 0}, root);
        Mesh cyl = BuildPrimitive(Primitive::Cylinder, DefaultParams(Primitive::Cylinder));
        MeshObject* a = sc.AddMesh(cyl, "BodyA");
        MeshObject* b = sc.AddMesh(cyl, "BodyB");
        a->transform.position = b->transform.position = {1, 1, 0};
        b->transform.scale = {0.5f, 1, 0.5f};
        check(BindToArmatureObject(sc, *a, *rig) && BindToArmatureObject(sc, *b, *rig) && BoneSource(sc, *a) == rig,
              "linked armature: two meshes bind to one armature object");
        check(maxMove(*a) >= 0 && maxMove(*a) < 1e-4f, "linked armature: rest pose leaves the mesh in place");
        rig->armature.bones[tip].poseRotation = {0, 0, 0.8f};
        SkinWithLinks(sc, *rig);
        check(maxMove(*a) > 0.2f && maxMove(*b) > 0.1f, "linked armature: both meshes deform when a bone rotates");
        // The same mesh with the bones as its own (moved into its space) must deform identically.
        MeshObject ref;
        ref.mesh = cyl;
        ref.armature.AddBone("root", {0, -1, 0}, {0, 0, 0});
        ref.armature.AddBone("tip", {0, 0, 0}, {0, 1, 0}, 0);
        ref.armature.BindAutomaticWeights(ref.mesh);
        ref.armature.bones[1].poseRotation = {0, 0, 0.8f};
        ref.armature.Skin(ref.skinned);
        float diff = 0;
        for (size_t i = 0; i < ref.skinned.size() && i < a->skinned.size(); ++i) diff = std::max(diff, far3(ref.skinned[i], a->skinned[i]));
        check(ref.skinned.size() == a->skinned.size() && diff < 1e-4f, "linked armature: matches the same rig owned by the mesh");

        namespace fs = std::filesystem;
        const std::string proj = (fs::temp_directory_path() / "leorix_selftest_linked.leorix").string();
        PaintProject pp; pp.New(16, 16); BrushSettings br;
        bool saved = SaveLeorixProject(proj, pp, br, {}, &sc, nullptr).ok;
        sc.Remove(0);
        SyncRigs(sc);
        check(BoneSource(sc, *a) == rig && maxMove(*a) > 0.2f, "linked armature: survives deleting an earlier object");
        Scene back; PaintProject pp2;
        bool loaded = saved && LoadLeorixProject(proj, pp2, br, g_vk, nullptr, &back, nullptr).ok && back.objects.size() == 4;
        if (loaded) {
            for (auto& o : back.objects) SkinObject(back, *o);
            loaded = BoneSource(back, *back.objects[2]) == back.objects[1].get() && back.objects[3]->armatureObject == back.objects[1]->id &&
                     maxMove(*back.objects[2]) > 0.2f;
        }
        check(loaded, "linked armature: the binding survives a project round trip");
        std::error_code ec; fs::remove(proj, ec);

        Scene cs;
        MeshObject* body = cs.AddMesh(Mesh{}, "Armature");
        AddHumanoidRig(body->armature, {-0.5f, 0, -0.2f}, {0.5f, 2, 0.2f}, RigNaming::Plain);
        std::vector<MeshObject*> ctrls = AddControlRig(cs, *body);
        bool allCtrl = ctrls.size() == 10;
        for (MeshObject* c : ctrls) allCtrl = allCtrl && c->isControl && c->mesh.tris.empty() && c->mesh.positions.size() >= 2;
        check(allCtrl && AddControlRig(cs, *body).empty(), "control rig: 10 controls, added once");
        auto find = [&](const std::string& n) { for (auto& o : cs.objects) if (o->name == n) return o.get(); return (MeshObject*)nullptr; };
        int fore = -1, hips = -1;
        for (int i = 0; i < (int)body->armature.bones.size(); ++i) {
            if (body->armature.bones[i].name == "forearm.L") fore = i;
            if (body->armature.bones[i].name == "hips") hips = i;
        }
        MeshObject* hand = find("CTRL_hand.L");
        MeshObject* hipCtrl = find("CTRL_hips");
        std::vector<Mat4> m;
        if (hand && hipCtrl && fore >= 0 && hips >= 0) {
            body->armature.Evaluate(m);
            const Bone& fb = body->armature.bones[fore];
            check(far3(m[fore].Apply(fb.tail), fb.tail) < 1e-3f, "control rig: the rest pose is unchanged");
            hand->transform.position = {hand->transform.position.x - 0.25f, hand->transform.position.y - 0.3f, hand->transform.position.z + 0.2f};
            check(SyncRigs(cs) >= 1, "control rig: moving a control is picked up");
            body->armature.Evaluate(m);
            check(far3(m[fore].Apply(fb.tail), hand->transform.position) < 1e-3f, "control rig: moving the hand control moves the IK chain end to it");
            hipCtrl->transform.position.z += 0.5f;
            SyncRigs(cs);
            body->armature.Evaluate(m);
            check(far3(m[hips].Apply(body->armature.bones[hips].head), hipCtrl->transform.position) < 1e-3f, "control rig: the hip control moves the hips");
            const std::string cproj = (fs::temp_directory_path() / "leorix_selftest_controls.leorix").string();
            Scene cb; PaintProject cpp2;
            bool ok = SaveLeorixProject(cproj, pp, br, {}, &cs, nullptr).ok &&
                      LoadLeorixProject(cproj, cpp2, br, g_vk, nullptr, &cb, nullptr).ok && cb.objects.size() == 11;
            if (ok) {
                const BoneConstraint& ik = cb.objects[0]->armature.bones[fore].constraints.at(0);
                const MeshObject* t = cb.FindById(ik.targetObject);
                ok = t && t->isControl && t->name == "CTRL_hand.L" && cb.FindById(ik.poleObject) && !t->mesh.positions.empty();
                SyncRigs(cb);
                cb.objects[0]->armature.Evaluate(m);
                ok = ok && far3(m[fore].Apply(fb.tail), t->transform.position) < 1e-3f;
            }
            check(ok, "control rig: controls and their targets survive a project round trip");
            fs::remove(cproj, ec);
            // deleting every control and adding the rig again works (dangling constraints are dropped)
            for (int i = (int)cs.objects.size() - 1; i >= 0; --i) if (cs.objects[i]->isControl) cs.Remove(i);
            check(AddControlRig(cs, *body).size() == 10, "control rig: can be added again after its controls are deleted");
        } else check(false, "control rig: controls and bones found");
    }
    // Cloth: hanging, draping on a box, determinism, project round trip
    {
        PhysicsSettings ps; ps.fps = 24; ps.frameStart = 1; ps.frameEnd = 48;   // 2 s
        Scene sc;
        MeshObject* cl = sc.AddMesh(BuildPrimitive(Primitive::Grid, DefaultParams(Primitive::Grid)), "Cloth");   // 2 x 2 m, flat
        cl->cloth.enabled = true;
        float zMax = -1e30f;
        for (const Float3& p : cl->mesh.positions) zMax = std::max(zMax, p.z);
        for (uint32_t i = 0; i < cl->mesh.positions.size(); ++i) if (cl->mesh.positions[i].z > zMax - 1e-4f) cl->cloth.pins.push_back(i);
        const std::vector<Float3> rest = cl->mesh.positions;
        check(BakeCloth(sc, ps) == 1 && cl->cloth.cache.size() == 48, "cloth: bake fills one entry per frame");
        const std::vector<Float3>& last = cl->cloth.cache.back();
        float lowest = 1e30f, strain = 0;
        for (const Float3& p : last) lowest = std::min(lowest, p.y);
        for (const Tri& t : cl->mesh.tris)
            for (auto [a, b] : {std::pair{t.a, t.b}, {t.b, t.c}, {t.c, t.a}}) {
                Float3 r{rest[a].x - rest[b].x, rest[a].y - rest[b].y, rest[a].z - rest[b].z};
                Float3 d{last[a].x - last[b].x, last[a].y - last[b].y, last[a].z - last[b].z};
                float lr = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z), ld = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
                strain = std::max(strain, std::fabs(ld - lr) / lr);
            }
        std::printf("  cloth: hanging lowest point %.3f m, worst edge strain %.3f\n", lowest, strain);
        check(lowest < -1.5f && strain < 0.05f, "cloth: a pinned cloth hangs without stretching");
        bool pinsStay = true;
        for (uint32_t p : cl->cloth.pins) pinsStay &= std::fabs(last[p].z - rest[p].z) < 1e-5f && std::fabs(last[p].y - rest[p].y) < 1e-5f;
        check(pinsStay, "cloth: pinned vertices stay put");

        std::vector<std::vector<Float3>> first = cl->cloth.cache;
        ApplyRigidBodyCache(sc, 48);
        check(cl->mesh.positions[0].y == last[0].y, "cloth: playback writes the cached shape");
        BakeCloth(sc, ps);
        bool identical = first.size() == cl->cloth.cache.size();
        for (size_t f = 0; identical && f < first.size(); ++f)
            identical = std::memcmp(first[f].data(), cl->cloth.cache[f].data(), first[f].size() * sizeof(Float3)) == 0;
        check(identical, "cloth: two bakes are identical");

        // settings and pins survive a project round trip, and a baked cloth is saved in its rest shape
        namespace fs = std::filesystem;
        const std::string proj = (fs::temp_directory_path() / "leorix_selftest_cloth.leorix").string();
        cl->cloth.stretch = 0.8f; cl->cloth.bend = 0.3f; cl->cloth.thickness = 0.03f;
        ApplyRigidBodyCache(sc, 48);
        PaintProject pp; pp.New(16, 16); BrushSettings br; PlaybackState pb;
        bool saved = SaveLeorixProject(proj, pp, br, {}, &sc, &pb).ok;
        Scene back; PaintProject pp2;
        bool same = saved && LoadLeorixProject(proj, pp2, br, g_vk, nullptr, &back, nullptr).ok && back.objects.size() == 1;
        if (same) {
            const MeshObject& b = *back.objects[0];
            same = b.cloth.enabled && b.cloth.stretch == 0.8f && b.cloth.bend == 0.3f && b.cloth.thickness == 0.03f &&
                   b.cloth.pins == cl->cloth.pins && b.mesh.positions.size() == rest.size() && b.mesh.positions[0].y == rest[0].y;
        }
        check(same, "cloth: settings and pins survive a project round trip");
        std::error_code ec; fs::remove(proj, ec);
        FreeRigidBodyBake(sc);
        check(!cl->cloth.Baked() && cl->mesh.positions[5].y == rest[5].y, "cloth: free bake restores the rest shape");

        // a cloth dropped on a passive box drapes over it without passing through
        Scene sd;
        MeshObject* box = sd.AddMesh(BuildPrimitive(Primitive::Cube, DefaultParams(Primitive::Cube)), "Box");
        box->transform.scale = {0.5f, 0.5f, 0.5f};   // 1 m cube, top at y = 0.5
        box->rigidBody.enabled = true; box->rigidBody.passive = true; box->rigidBody.shape = RigidShape::Box;
        MeshObject* drape = sd.AddMesh(BuildPrimitive(Primitive::Grid, DefaultParams(Primitive::Grid)), "Drape");
        drape->transform.scale = {0.75f, 1, 0.75f};   // 1.5 m square
        drape->transform.position = {0, 1.0f, 0};
        drape->cloth.enabled = true;
        PhysicsSettings pd = ps; pd.frameEnd = 36;
        BakeRigidBodies(sd, pd);
        check(BakeCloth(sd, pd) == 1, "cloth: bake a cloth over a box");
        const float th = drape->cloth.thickness;
        float deepest = 0, topY = 1e30f;
        for (const Float3& lp : drape->cloth.cache.back()) {
            Float3 p = drape->transform.Apply(lp);
            float inside = std::min({0.5f - std::fabs(p.x), 0.5f - std::fabs(p.y), 0.5f - std::fabs(p.z)});   // > 0 inside the box
            deepest = std::max(deepest, inside);
            if (std::fabs(p.x) < 0.3f && std::fabs(p.z) < 0.3f) topY = std::min(topY, p.y);
        }
        std::printf("  cloth: on a box, deepest point inside %.4f m, centre rests at y %.3f\n", deepest, topY);
        check(deepest <= th && topY > 0.5f && topY < 0.6f, "cloth: drapes over a passive box without passing through");
    }
    // Scene scripts: material/camera/world/curve reach the render scene, and a headless render writes a PNG
    {
        const char* code =
            "material{color={0.9,0.2,0.1}, roughness=0.3, emission={1,0.5,0.2}, strength=4, smooth=true}\n"
            "ico_sphere(1, 2)\n"
            "material{glass=1.5}\n"
            "curve({{-1,0,0},{0,1,0},{1,0,0}}, {radius=0.1})\n"
            "camera{eye={3,2,4}, target={0,0.5,0}, lens=50, fstop=2.8}\n"
            "world{preset='GoldenHour', strength=0.8, rotation=90, exposure=1.2}\n";
        Scene sc; RayTraceWorld w; HdriImage hdri; RayTraceSettings st; std::string log;
        bool ok = SceneFromScript(code, sc, w, hdri, st, log);
        check(ok && sc.objects.size() == 2, "scene script: one object per mesh");
        if (ok && sc.objects.size() == 2) {
            const MeshObject& a = *sc.objects[0]; const MeshObject& b = *sc.objects[1];
            check(a.material.baseColor[0] == 0.9f && a.material.emissionColor[1] == 0.5f && a.material.emissiveStrengthRT == 4.0f &&
                  a.shadeSmooth && b.material.transmission == 1.0f && b.material.transmissionIOR == 1.5f && b.material.baseColor[1] == 0.76f,
                  "scene script: material() applies to the meshes made after it");
            const SceneCamera& c = sc.Camera();   // the eye the path tracer uses (PathTracer::Start)
            float cp = std::cos(c.pitch);
            Float3 e{c.target.x + std::sin(c.yaw) * cp * c.distance, c.target.y + std::sin(c.pitch) * c.distance,
                     c.target.z + std::cos(c.yaw) * cp * c.distance};
            check(std::fabs(e.x - 3) + std::fabs(e.y - 2) + std::fabs(e.z - 4) < 1e-3f && sc.Camera().focalLengthMm == 50.0f &&
                  sc.Camera().dof, "scene script: camera{} eye and target round-trip");
            check(w.hdri && hdri.Loaded() && w.hdriStrength == 0.8f && st.exposure == 1.2f, "scene script: world{} preset and exposure");
            st.width = 32; st.height = 18; st.samples = 2; st.threads = 2;
            PathTracer pt;
            const SceneCamera& cam = sc.Camera();
            pt.Start(sc, cam, cam.yaw, cam.pitch, cam.distance, cam.target, true, st, w);
            while (pt.Running()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            namespace fs = std::filesystem;
            const std::string png = (fs::temp_directory_path() / "leorix_selftest_headless.png").string();
            std::error_code ec;
            check(pt.SavePng(png) && fs::file_size(png, ec) > 100, "headless render writes a PNG");
            fs::remove(png, ec);
        }
        Scene bad; RayTraceWorld w2; HdriImage h2; RayTraceSettings st2;
        check(!SceneFromScript("world{preset='Nowhere'}", bad, w2, h2, st2, log), "scene script: unknown world preset is an error");
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

// Builds a renderable scene from a Lua scene script: one object per mesh, with the script's
// material(), camera{} and world{} settings. Shared by --headless-render and the self-test.
static bool SceneFromScript(const std::string& code, Scene& scene, RayTraceWorld& w, HdriImage& hdri,
                            RayTraceSettings& st, std::string& log) {
    ScriptResult r = RunMeshScript(code);
    log = r.output;
    if (!r.ok) return false;
    for (size_t i = 0; i < r.meshes.size(); ++i) {
        const ScriptMaterial& sm = r.materials[i];
        MeshObject* o = scene.AddMesh(std::move(r.meshes[i]), "Script mesh " + std::to_string(i + 1));
        Material& m = o->material;
        for (int c = 0; c < 3; ++c) { m.baseColor[c] = sm.color[c]; m.emissionColor[c] = sm.emission[c]; }
        m.roughness = sm.roughness; m.metallic = sm.metallic;
        m.emissiveStrengthRT = sm.strength;
        m.transmission = sm.transmission; m.transmissionIOR = sm.ior;
        m.sampleAsLight = sm.sampleLight;
        o->shadeSmooth = sm.smooth;
        o->selected = false;
    }
    const ScriptView& v = r.view;
    SceneCamera& cam = scene.Camera();
    if (v.hasCamera) {   // eye/target -> the orbit form SceneCamera stores (inverse of Eye())
        Float3 d{v.eye.x - v.target.x, v.eye.y - v.target.y, v.eye.z - v.target.z};
        float dist = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        if (dist < 1e-4f) { log += "camera: eye and target are the same point\n"; return false; }
        cam.target = v.target;
        cam.distance = dist;
        cam.pitch = std::asin(std::clamp(d.y / dist, -1.0f, 1.0f));
        cam.yaw = std::atan2(d.x, d.z);   // the tracer's convention: eye = target + (sin yaw, ., cos yaw) * cos pitch * dist
        cam.focalLengthMm = v.lens;
        cam.dof = v.fStop > 0;
        cam.fStop = v.fStop > 0 ? v.fStop : cam.fStop;
        cam.focusDistance = v.focus > 0 ? v.focus : dist;
    }
    const float el = v.sunElevation * 3.14159265f / 180.0f, az = v.sunAzimuth * 3.14159265f / 180.0f;
    w.sunDir = {std::cos(el) * std::sin(az), std::sin(el), std::cos(el) * std::cos(az)};
    w.sunStrength = v.sunStrength;
    w.skyStrength = v.skyStrength;
    for (int c = 0; c < 3; ++c) { w.sunColor[c] = v.sunColor[c]; w.skyColor[c] = v.skyColor[c]; w.horizonColor[c] = v.horizonColor[c]; }
    if (v.hdriPreset >= 0) {
        GenerateHdri((HdriPreset)v.hdriPreset, hdri, 2048);
        w.hdri = &hdri; w.hdriStrength = v.hdriStrength; w.hdriRotation = v.hdriRotation * 3.14159265f / 180.0f;
    }
    st.exposure = v.exposure;
    return true;
}

// leorix --headless-render scene.lua|scene.leorix out.png [--width W --height H --spp N --denoise S --save p.leorix]
// Path-traces without opening a window or touching the GPU.
static int HeadlessRender(int argc, char** argv, const std::string& in, const std::string& out) {
    RayTraceSettings st;
    st.width = 1600; st.height = 900; st.samples = 256; st.maxBounces = 8;
    std::string savePath;
    for (int i = 1; i + 1 < argc; ++i) {
        std::string a = argv[i];
        if (a == "--width") st.width = std::clamp(std::atoi(argv[i + 1]), 16, 8192);
        if (a == "--height") st.height = std::clamp(std::atoi(argv[i + 1]), 16, 8192);
        if (a == "--spp") st.samples = std::clamp(std::atoi(argv[i + 1]), 1, 100000);
        if (a == "--save") savePath = argv[i + 1];
        if (a == "--denoise") { float d = (float)std::atof(argv[i + 1]); st.denoise = d > 0; st.denoiseStrength = std::clamp(d, 0.0f, 4.0f); }
    }
    Scene scene; RayTraceWorld w; HdriImage hdri;
    PaintProject pp; pp.New(16, 16); BrushSettings br; PlaybackState pb;
    std::string ext = std::filesystem::path(in).extension().string();
    if (ext == ".lua") {
        std::ifstream f(in, std::ios::binary);
        if (!f) { std::printf("cannot read %s\n", in.c_str()); return 1; }
        std::stringstream ss; ss << f.rdbuf();
        std::string log;
        bool ok = SceneFromScript(ss.str(), scene, w, hdri, st, log);
        std::printf("%s", log.c_str());
        if (!ok) return 1;
    } else {
        ProjectSaveInfo info = LoadLeorixProject(in, pp, br, g_vk, nullptr, &scene, &pb);
        if (!info.ok) { std::printf("cannot open %s: %s\n", in.c_str(), info.message.c_str()); return 1; }
    }
    if (!scene.HasMesh()) { std::printf("nothing to render\n"); return 1; }
    if (!savePath.empty()) {
        ProjectSaveInfo info = SaveLeorixProject(savePath, pp, br, {}, &scene, &pb);
        std::printf("%s %s\n", info.ok ? "saved" : "could not save", savePath.c_str());
    }
    const SceneCamera& cam = scene.Camera();
    PathTracer pt;
    auto t0 = std::chrono::steady_clock::now();
    pt.Start(scene, cam, cam.yaw, cam.pitch, cam.distance, cam.target, true, st, w);
    int shown = -1;
    while (pt.Running()) {
        int pct = (int)(pt.Progress() * 100);
        if (pct / 10 != shown) { shown = pct / 10; std::printf("  %d%%\n", pct); std::fflush(stdout); }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (!pt.SavePng(out)) { std::printf("could not write %s\n", out.c_str()); return 1; }
    std::printf("rendered %s: %dx%d, %d spp, %zu tris, %.1f s\n", out.c_str(), st.width, st.height, st.samples,
                pt.Stats().triangles, secs);
    return 0;
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) if (std::string(argv[i]) == "--selftest") return SelfTest();
    for (int i = 1; i + 2 < argc; ++i) if (std::string(argv[i]) == "--convert") return Convert(argv[i + 1], argv[i + 2]);
    for (int i = 1; i + 2 < argc; ++i) if (std::string(argv[i]) == "--headless-render") return HeadlessRender(argc, argv, argv[i + 1], argv[i + 2]);

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
