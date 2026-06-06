#include "weatherdesktop/Texture.h"

#include <SDL.h>
#include <GL/gl.h>

#include <vector>

// mingw's GL/gl.h is OpenGL 1.1; this enum (GL 1.2) is missing from the header
// even though the 3.3 context supports it at runtime.
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

#ifdef _WIN32
// INITGUID makes the WIC CLSID/format GUIDs (CLSID_WICImagingFactory,
// GUID_WICPixelFormat32bppRGBA, ...) get defined as data in this one TU, so we
// don't depend on a separate uuid import lib.
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

#ifdef _WIN32

static std::wstring widen(const std::string& s) {
    if (s.empty()) return L"";
    int sz = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (sz <= 0) return L"";
    std::wstring w(sz - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], sz);
    return w;
}

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
                    // Treat the top-left pixel as the background; punch any
                    // near-matching pixel to fully transparent.
                    int br = pixels[0], bg = pixels[1], bb = pixels[2];
                    const int tol2 = 44 * 44;  // squared RGB distance tolerance
                    for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
                        int dr = pixels[i] - br;
                        int dg = pixels[i + 1] - bg;
                        int db = pixels[i + 2] - bb;
                        if (dr * dr + dg * dg + db * db <= tol2) {
                            pixels[i + 3] = 0;
                        }
                    }
                }
                GLuint tex = 0;
                glGenTextures(1, &tex);
                glBindTexture(GL_TEXTURE_2D, tex);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA,
                             GL_UNSIGNED_BYTE, pixels.data());
                glBindTexture(GL_TEXTURE_2D, 0);
                out.id = tex;
                out.width = static_cast<int>(w);
                out.height = static_cast<int>(h);
            }
        }
    }

    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (factory) factory->Release();
    return out;
}

#else  // non-Windows: no PNG decoder wired up yet, icons just won't render.

Texture loadTexture(const std::string&) { return Texture{}; }

#endif

void freeTexture(Texture& tex) {
    if (tex.id) {
        GLuint id = tex.id;
        glDeleteTextures(1, &id);
        tex.id = 0;
    }
}

}  // namespace wd
