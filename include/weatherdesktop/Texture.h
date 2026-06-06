#pragma once

#include <string>

namespace wd {

// Absolute path to a file bundled under resources/ next to the executable.
std::string resourcePath(const std::string& name);

struct Texture {
    unsigned int id = 0;  // OpenGL texture name (0 = not loaded)
    int width = 0;
    int height = 0;
    bool valid() const { return id != 0; }
};

// Decode a PNG (via WIC on Windows) and upload it as an RGBA OpenGL texture.
// Returns an invalid Texture (id == 0) on any failure - callers should degrade
// gracefully (just skip drawing the icon).
//
// When colorKeyBackground is true, the top-left pixel is treated as the
// background color and all near-matching pixels are made transparent - this is
// how the (alpha-less) weather sprite sheet drops its dark cell background.
Texture loadTexture(const std::string& path, bool colorKeyBackground = false);

// Delete a previously loaded GL texture (safe to call on an invalid Texture).
void freeTexture(Texture& tex);

}  // namespace wd
