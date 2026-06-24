#include "weatherdesktop/Texture.h"

#include <SDL.h>
#include <GL/gl.h>

#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "../third_party/stb_image.h"

// mingw's GL/gl.h is OpenGL 1.1; this enum (GL 1.2) is missing from the header
// even though the 3.3 context supports it at runtime.
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

#ifdef _WIN32
// INITGUID makes the WIC CLSID/format GUIDs get defined as data in this one TU,
// so we don't depend on a separate uuid import lib.
#define INITGUID
#include <initguid.h>
#include <wincodec.h>
#include <string>
#endif

namespace wd {

std::string resourcePath(const std::string& name) {
    std::string base;
    char* sdlBase = SDL_GetBasePath();
    if (sdlBase) {
        base = sdlBase;
        SDL_free(sdlBase);
    }
    return base + "resources/" + name;
}

// Upload an RGBA pixel buffer as a GL texture (GL thread only).
static Texture uploadRGBA(const unsigned char* pixels, int w, int h) {
    Texture out;
    if (!pixels || w <= 0 || h <= 0) return out;
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    out.id = tex;
    out.width = w;
    out.height = h;
    return out;
}

#ifdef _WIN32

static std::wstring widen(const std::string& s) {
    if (s.empty()) return L"";
    int sz = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (sz <= 0) return L"";
    std::wstring w(sz - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], sz);
    return w;
}

// Bundled assets (weather/moon atlases) decode via WIC from a file path.
Texture loadTexture(const std::string& path, bool colorKeyBackground) {
    Texture out;

    IWICImagingFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                CLSCTX_INPROC_SERVER, IID_IWICImagingFactory,
                                reinterpret_cast<void**>(&factory)))) {
        return out;
    }

    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;

    std::wstring wpath = widen(path);
    if (SUCCEEDED(factory->CreateDecoderFromFilename(
            wpath.c_str(), nullptr, GENERIC_READ,
            WICDecodeMetadataCacheOnLoad, &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) &&
        SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(
            frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
            nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
        UINT w = 0, h = 0;
        converter->GetSize(&w, &h);
        if (w > 0 && h > 0) {
            std::vector<unsigned char> pixels(static_cast<size_t>(w) * h * 4);
            if (SUCCEEDED(converter->CopyPixels(
                    nullptr, w * 4, static_cast<UINT>(pixels.size()),
                    pixels.data()))) {
                if (colorKeyBackground && pixels.size() >= 4) {
                    int br = pixels[0], bg = pixels[1], bb = pixels[2];
                    const int tol2 = 44 * 44;
                    for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
                        int dr = pixels[i] - br;
                        int dg = pixels[i + 1] - bg;
                        int db = pixels[i + 2] - bb;
                        if (dr * dr + dg * dg + db * db <= tol2) pixels[i + 3] = 0;
                    }
                }
                out = uploadRGBA(pixels.data(), static_cast<int>(w), static_cast<int>(h));
            }
        }
    }

    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (factory) factory->Release();
    return out;
}

#else  // non-Windows: no file decoder wired up (icons just won't render).

Texture loadTexture(const std::string&, bool) { return Texture{}; }

#endif

// In-memory PNGs (radar tiles) decode with stb_image -- portable and reliable,
// avoiding the flaky WIC-from-stream path. Must be called on the GL thread.
Texture loadTextureFromMemory(const unsigned char* data, size_t size) {
    if (!data || size == 0) return Texture{};
    int w = 0, h = 0, ch = 0;
    unsigned char* px = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &ch, 4);
    if (!px) return Texture{};
    Texture out = uploadRGBA(px, w, h);
    stbi_image_free(px);
    return out;
}

void freeTexture(Texture& tex) {
    if (tex.id) {
        GLuint id = tex.id;
        glDeleteTextures(1, &id);
        tex.id = 0;
    }
}

}  // namespace wd
