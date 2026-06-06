// Weather Desktop visual theme - the kengine-site "Korra" palette ported to
// ImGui, shared with filex so the two desktop apps read as one brand.
//
// Dark chocolate browns, warm cream text, brown buttons/borders, an orange
// (#E07942) accent for primary actions, warm tan (#E0A773) for headings, and a
// bright eye-green (#6AD84A) for "live"/active status. Mirrors
// kengine-site/site/style.css.
#pragma once
#include <imgui.h>

namespace wd {
namespace theme {

// 0xRRGGBB -> ImVec4 (optionally with alpha).
inline ImVec4 hex(unsigned v, float a = 1.0f) {
    return ImVec4(((v >> 16) & 0xFF) / 255.0f, ((v >> 8) & 0xFF) / 255.0f,
                  (v & 0xFF) / 255.0f, a);
}

// Korra palette (see style.css :root).
inline const ImVec4 Bg     = hex(0x1B1613);  // matches the weather sprite bg
inline const ImVec4 Panel  = hex(0x241712);  // slightly lifted panel/child bg
inline const ImVec4 Fg     = hex(0xE6D6C4);  // warm cream text
inline const ImVec4 Dim    = hex(0x8A5D3D);  // muted warm brown (disabled)
inline const ImVec4 Amber  = hex(0xE07942);  // orange accent / primary action
inline const ImVec4 Muted  = hex(0x5C3F29);  // darker brown borders/buttons
inline const ImVec4 Accent = hex(0xE0A773);  // warm tan headings
inline const ImVec4 Live   = hex(0x6AD84A);  // eye-green "live" status
inline const ImVec4 Error  = hex(0xE0564A);  // warm red, tuned to the palette

void apply();  // install the palette + spacing into the current ImGui style

}  // namespace theme
}  // namespace wd
