#include "weatherdesktop/Theme.h"

namespace wd {
namespace theme {

void apply() {
    ImGuiStyle &s = ImGui::GetStyle();
    ImVec4 *c = s.Colors;

    c[ImGuiCol_Text]                 = Fg;
    c[ImGuiCol_TextDisabled]         = Dim;
    c[ImGuiCol_WindowBg]             = Bg;
    c[ImGuiCol_ChildBg]              = Bg;  // match window so sprites blend
    c[ImGuiCol_PopupBg]              = Panel;
    c[ImGuiCol_Border]               = Muted;
    c[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);

    c[ImGuiCol_FrameBg]              = hex(0x2A1A13);
    c[ImGuiCol_FrameBgHovered]       = hex(0x382318);
    c[ImGuiCol_FrameBgActive]        = hex(0x42291C);

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

    // Default buttons are brown; primary actions push amber on top.
    c[ImGuiCol_Button]               = Muted;
    c[ImGuiCol_ButtonHovered]        = hex(0x6E4A30);
    c[ImGuiCol_ButtonActive]         = Dim;

    c[ImGuiCol_Header]               = hex(0x382318);
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
    c[ImGuiCol_TableBorderLight]     = hex(0x382318);
    c[ImGuiCol_TableRowBg]           = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]        = hex(0xE07942, 0.04f);

    c[ImGuiCol_PlotHistogram]        = Amber;
    c[ImGuiCol_PlotHistogramHovered] = Accent;
    c[ImGuiCol_PlotLines]            = Accent;

    c[ImGuiCol_TextSelectedBg]       = hex(0xE07942, 0.35f);
    c[ImGuiCol_NavHighlight]         = Amber;

    // Geometry: gentle rounding + 1px brown borders for a crisp BBS-card look.
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
