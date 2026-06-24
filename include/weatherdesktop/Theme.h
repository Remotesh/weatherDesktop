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

enum class ThemeId { Dark, Light };

// Current palette -- MUTABLE globals overwritten by apply(); call sites read the
// current value each frame, so switching themes is just a re-apply. Defaults to
// the dark Korra palette.
inline ImVec4 Bg     = hex(0x1B1613);  // matches the weather sprite bg
inline ImVec4 Panel  = hex(0x241712);  // slightly lifted panel/child bg
inline ImVec4 Fg     = hex(0xE6D6C4);  // text
inline ImVec4 Dim    = hex(0x8A5D3D);  // disabled / muted text
inline ImVec4 Amber  = hex(0xE07942);  // accent / primary action
inline ImVec4 Muted  = hex(0x5C3F29);  // borders / buttons
inline ImVec4 Accent = hex(0xE0A773);  // headings
inline ImVec4 Live   = hex(0x6AD84A);  // "live"/good status
inline ImVec4 Error  = hex(0xE0564A);  // warning/error
// Surface shades (frame/button/header backgrounds), also theme-dependent.
inline ImVec4 Surface  = hex(0x2A1A13);
inline ImVec4 SurfaceH = hex(0x382318);
inline ImVec4 SurfaceA = hex(0x42291C);
inline ImVec4 ButtonH  = hex(0x6E4A30);

// Set the palette globals for `id` and install them + spacing into the ImGui style.
void apply(ThemeId id = ThemeId::Dark);

}  // namespace theme
}  // namespace wd
