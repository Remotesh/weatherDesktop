#include "weatherdesktop/Theme.h"

namespace wd {
namespace theme {

void apply(ThemeId id) {
    if (id == ThemeId::Light) {
        // Warm "paper" light theme -- keeps the brand hues but inverts value.
        Bg       = hex(0xF3ECE2);
        Panel    = hex(0xFBF6EE);
        Fg       = hex(0x2A211B);
        Dim      = hex(0x8A6A4A);
        Amber    = hex(0xC85A1E);
        Muted    = hex(0xCBB79E);
        Accent   = hex(0x9A5A2A);
        Live     = hex(0x2E8B2E);
        Error    = hex(0xC0392B);
        Surface  = hex(0xEAE0D2);
        SurfaceH = hex(0xE0D3C0);
        SurfaceA = hex(0xD4C3AC);
        ButtonH  = hex(0xE6D8C4);
    } else {  // Dark (Korra)
        Bg       = hex(0x1B1613);
        Panel    = hex(0x241712);
        Fg       = hex(0xE6D6C4);
        Dim      = hex(0x8A5D3D);
        Amber    = hex(0xE07942);
        Muted    = hex(0x5C3F29);
        Accent   = hex(0xE0A773);
        Live     = hex(0x6AD84A);
        Error    = hex(0xE0564A);
        Surface  = hex(0x2A1A13);
        SurfaceH = hex(0x382318);
        SurfaceA = hex(0x42291C);
        ButtonH  = hex(0x6E4A30);
    }

    ImGuiStyle& s = ImGui::GetStyle();
    ImVec4* c = s.Colors;

    c[ImGuiCol_Text]                 = Fg;
    c[ImGuiCol_TextDisabled]         = Dim;
    c[ImGuiCol_WindowBg]             = Bg;
    c[ImGuiCol_ChildBg]              = Bg;  // match window so sprites blend (dark)
    c[ImGuiCol_PopupBg]              = Panel;
    c[ImGuiCol_Border]               = Muted;
    c[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);

    c[ImGuiCol_FrameBg]              = Surface;
    c[ImGuiCol_FrameBgHovered]       = SurfaceH;
    c[ImGuiCol_FrameBgActive]        = SurfaceA;

    c[ImGuiCol_TitleBg]              = Bg;
    c[ImGuiCol_TitleBgActive]        = Bg;
    c[ImGuiCol_TitleBgCollapsed]     = Bg;
    c[ImGuiCol_MenuBarBg]            = Panel;

    c[ImGuiCol_ScrollbarBg]          = Bg;
    c[ImGuiCol_ScrollbarGrab]        = Muted;
    c[ImGuiCol_ScrollbarGrabHovered] = Dim;
    c[ImGuiCol_ScrollbarGrabActive]  = Amber;

    c[ImGuiCol_CheckMark]            = Amber;
    c[ImGuiCol_SliderGrab]           = Amber;
    c[ImGuiCol_SliderGrabActive]     = Accent;

    c[ImGuiCol_Button]               = Muted;
    c[ImGuiCol_ButtonHovered]        = ButtonH;
    c[ImGuiCol_ButtonActive]         = Dim;

    c[ImGuiCol_Header]               = SurfaceH;
    c[ImGuiCol_HeaderHovered]        = Muted;
    c[ImGuiCol_HeaderActive]         = Dim;

    c[ImGuiCol_Separator]            = Muted;
    c[ImGuiCol_SeparatorHovered]     = Amber;
    c[ImGuiCol_SeparatorActive]      = Amber;

    c[ImGuiCol_ResizeGrip]           = Muted;
    c[ImGuiCol_ResizeGripHovered]    = Dim;
    c[ImGuiCol_ResizeGripActive]     = Amber;

    c[ImGuiCol_TableHeaderBg]        = Panel;
    c[ImGuiCol_TableBorderStrong]    = Muted;
    c[ImGuiCol_TableBorderLight]     = SurfaceH;
    c[ImGuiCol_TableRowBg]           = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]        = ImVec4(Amber.x, Amber.y, Amber.z, 0.05f);

    c[ImGuiCol_PlotHistogram]        = Amber;
    c[ImGuiCol_PlotHistogramHovered] = Accent;
    c[ImGuiCol_PlotLines]            = Accent;

    c[ImGuiCol_TextSelectedBg]       = ImVec4(Amber.x, Amber.y, Amber.z, 0.35f);
    c[ImGuiCol_NavHighlight]         = Amber;

    // Geometry: gentle rounding + 1px borders for a crisp BBS-card look.
    s.WindowRounding      = 5.0f;
    s.ChildRounding       = 4.0f;
    s.FrameRounding       = 3.0f;
    s.PopupRounding       = 4.0f;
    s.GrabRounding        = 3.0f;
    s.ScrollbarRounding   = 4.0f;
    s.WindowBorderSize    = 1.0f;
    s.ChildBorderSize     = 1.0f;
    s.FrameBorderSize     = 1.0f;
    s.WindowPadding       = ImVec2(16, 14);
    s.FramePadding        = ImVec2(9, 6);
    s.ItemSpacing         = ImVec2(9, 9);
    s.ItemInnerSpacing    = ImVec2(7, 6);
    s.ScrollbarSize       = 13.0f;
    s.GrabMinSize         = 12.0f;
}

}  // namespace theme
}  // namespace wd
