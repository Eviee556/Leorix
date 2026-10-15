#include "app/Theme.h"
#include "imgui.h"
#include <cstdio>

static ImVec4 Hex(unsigned rgb, float a = 1.0f) {
    return ImVec4(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, a);
}

void ApplyLeorixTheme(float dpiScale) {
    const ImVec4 bg = Hex(0x14171c), panel = Hex(0x1b1f27), panelAlt = Hex(0x20242e);
    const ImVec4 border = Hex(0x333a47), brass = Hex(0xc98a4b), brassDim = Hex(0x8a5f37);
    const ImVec4 text = Hex(0xe8e3d8), muted = Hex(0x8b92a3);

    ImGuiStyle& s = ImGui::GetStyle();
    ImGui::StyleColorsDark(&s);
    s.WindowRounding = 0.0f; s.FrameRounding = 4.0f; s.GrabRounding = 4.0f; s.TabRounding = 4.0f;
    s.PopupRounding = 5.0f; s.ScrollbarRounding = 5.0f; s.ChildRounding = 4.0f;
    s.WindowBorderSize = 1.0f; s.FrameBorderSize = 1.0f;
    s.WindowPadding = ImVec2(12, 10); s.FramePadding = ImVec2(8, 5); s.ItemSpacing = ImVec2(8, 7);
    s.ScrollbarSize = 11.0f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = text;                 c[ImGuiCol_TextDisabled] = muted;
    c[ImGuiCol_WindowBg] = panel;            c[ImGuiCol_ChildBg] = panel;
    c[ImGuiCol_PopupBg] = panelAlt;          c[ImGuiCol_Border] = border;
    c[ImGuiCol_FrameBg] = panelAlt;          c[ImGuiCol_FrameBgHovered] = Hex(0x282e39);
    c[ImGuiCol_FrameBgActive] = Hex(0x2c323e);
    c[ImGuiCol_TitleBg] = panel;             c[ImGuiCol_TitleBgActive] = panelAlt;
    c[ImGuiCol_TitleBgCollapsed] = panel;    c[ImGuiCol_MenuBarBg] = panel;
    c[ImGuiCol_ScrollbarBg] = panel;         c[ImGuiCol_ScrollbarGrab] = border;
    c[ImGuiCol_ScrollbarGrabHovered] = brassDim; c[ImGuiCol_ScrollbarGrabActive] = brass;
    c[ImGuiCol_CheckMark] = brass;           c[ImGuiCol_SliderGrab] = brassDim;
    c[ImGuiCol_SliderGrabActive] = brass;
    c[ImGuiCol_Button] = panelAlt;           c[ImGuiCol_ButtonHovered] = Hex(0x8a5f37, 0.55f);
    c[ImGuiCol_ButtonActive] = brassDim;
    c[ImGuiCol_Header] = Hex(0xc98a4b, 0.18f); c[ImGuiCol_HeaderHovered] = Hex(0x8a5f37, 0.6f);
    c[ImGuiCol_HeaderActive] = brassDim;
    c[ImGuiCol_Separator] = border;          c[ImGuiCol_SeparatorHovered] = brassDim;
    c[ImGuiCol_SeparatorActive] = brass;
    c[ImGuiCol_ResizeGrip] = Hex(0x333a47, 0.5f); c[ImGuiCol_ResizeGripHovered] = brassDim;
    c[ImGuiCol_ResizeGripActive] = brass;
    c[ImGuiCol_Tab] = panel;                 c[ImGuiCol_TabHovered] = Hex(0x8a5f37, 0.6f);
    c[ImGuiCol_TabSelected] = panelAlt;      c[ImGuiCol_TabSelectedOverline] = brass;
    c[ImGuiCol_TabDimmed] = panel;           c[ImGuiCol_TabDimmedSelected] = panelAlt;
    c[ImGuiCol_TabDimmedSelectedOverline] = brassDim;
    c[ImGuiCol_DockingPreview] = Hex(0xc98a4b, 0.45f);
    c[ImGuiCol_DockingEmptyBg] = bg;
    c[ImGuiCol_TextSelectedBg] = Hex(0xc98a4b, 0.35f);
    c[ImGuiCol_NavCursor] = brass;
    c[ImGuiCol_ModalWindowDimBg] = Hex(0x0a0c10, 0.6f);

    s.ScaleAllSizes(dpiScale);
    s.FontScaleDpi = dpiScale;
}

void LoadLeorixFonts(float) {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::GetStyle().FontSizeBase = 15.0f;
    // Leorix's UI font is Georgia; fall back to ImGui's built-in font if it isn't installed.
    const char* candidates[] = { "C:\\Windows\\Fonts\\georgia.ttf", "C:\\Windows\\Fonts\\segoeui.ttf" };
    for (const char* path : candidates) {
        if (FILE* f = std::fopen(path, "rb")) {
            std::fclose(f);
            if (io.Fonts->AddFontFromFileTTF(path)) return;
        }
    }
    io.Fonts->AddFontDefaultVector();
}
