// Leorix Native - entry point: SDL3 window, Vulkan swapchain, Dear ImGui frame loop.
#include "app/App.h"
#include "app/Theme.h"
#include "mesh/IO.h"
#include "mesh/EditOps.h"
#include "mesh/Primitives.h"
#include "tools/Script.h"
#include "gpu/VulkanContext.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_vulkan.h"
#include <SDL3/SDL.h>
#include <filesystem>
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
