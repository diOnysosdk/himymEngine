#include "rev_runtime.h"
#include <windows.h>
#include <gl/gl.h>
#include <gdiplus.h>
#include <cstdio>
#include <cstring>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_RGBA
#define GL_RGBA 0x1908
#endif

namespace rev {
namespace runtime {

// ------------------------------------------------------------------
// ComputeEffectOpacity
// ------------------------------------------------------------------
float ComputeEffectOpacity(int effect_type,
                           float fade_in_start,  float fade_in_end,
                           float fade_out_start, float fade_out_end,
                           float time)
{
    if (effect_type == 1) { // fade_in_out
        if (time < fade_in_start) return 0.0f;
        float in_dur = fade_in_end - fade_in_start;
        if (in_dur > 0.0f && time < fade_in_end)
            return (time - fade_in_start) / in_dur;
        float out_dur = fade_out_end - fade_out_start;
        if (out_dur > 0.0f && time >= fade_out_start)
            return (time > fade_out_end) ? 0.0f
                                         : 1.0f - (time - fade_out_start) / out_dur;
    }
    return 1.0f;
}

// ------------------------------------------------------------------
// LoadImageTexture  (file path -> GL texture)
// ------------------------------------------------------------------
bool LoadImageTexture(const char* path, ImageTexture* tex)
{
    wchar_t wpath[512];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 512);

    Gdiplus::Bitmap* bitmap = new Gdiplus::Bitmap(wpath);
    if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok) {
        delete bitmap;
        return false;
    }

    tex->width  = (int)bitmap->GetWidth();
    tex->height = (int)bitmap->GetHeight();

    Gdiplus::Rect rect(0, 0, tex->width, tex->height);
    Gdiplus::BitmapData bitmapData;
    if (bitmap->LockBits(&rect, Gdiplus::ImageLockModeRead,
                         PixelFormat32bppARGB, &bitmapData) != Gdiplus::Ok) {
        delete bitmap;
        return false;
    }

    unsigned char* pixels = new unsigned char[tex->width * tex->height * 4];
    unsigned char* src    = (unsigned char*)bitmapData.Scan0;
    for (int i = 0; i < tex->width * tex->height; i++) {
        pixels[i*4 + 0] = src[i*4 + 2]; // R
        pixels[i*4 + 1] = src[i*4 + 1]; // G
        pixels[i*4 + 2] = src[i*4 + 0]; // B
        pixels[i*4 + 3] = src[i*4 + 3]; // A
    }

    glGenTextures(1, &tex->texture_id);
    glBindTexture(GL_TEXTURE_2D, tex->texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex->width, tex->height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    delete[] pixels;
    bitmap->UnlockBits(&bitmapData);
    delete bitmap;
    return true;
}

// ------------------------------------------------------------------
// LoadImageTextureFromMemory  (packed-asset builds)
// ------------------------------------------------------------------
bool LoadImageTextureFromMemory(const unsigned char* data, size_t size,
                                ImageTexture* tex)
{
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!hMem) return false;
    void* pMem = GlobalLock(hMem);
    if (!pMem) { GlobalFree(hMem); return false; }
    memcpy(pMem, data, size);
    GlobalUnlock(hMem);

    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(hMem, TRUE, &stream) != S_OK) {
        GlobalFree(hMem);
        return false;
    }

    Gdiplus::Bitmap* bitmap = new Gdiplus::Bitmap(stream);
    // NOTE: do NOT release stream until after LockBits — GDI+ decodes PNG lazily.
    if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok) {
        delete bitmap;
        stream->Release();
        return false;
    }

    tex->width  = (int)bitmap->GetWidth();
    tex->height = (int)bitmap->GetHeight();

    Gdiplus::Rect rect(0, 0, tex->width, tex->height);
    Gdiplus::BitmapData bitmapData;
    if (bitmap->LockBits(&rect, Gdiplus::ImageLockModeRead,
                         PixelFormat32bppARGB, &bitmapData) != Gdiplus::Ok) {
        delete bitmap;
        stream->Release();
        return false;
    }

    unsigned char* pixels = new unsigned char[tex->width * tex->height * 4];
    unsigned char* src    = (unsigned char*)bitmapData.Scan0;
    for (int i = 0; i < tex->width * tex->height; i++) {
        pixels[i*4 + 0] = src[i*4 + 2]; // R
        pixels[i*4 + 1] = src[i*4 + 1]; // G
        pixels[i*4 + 2] = src[i*4 + 0]; // B
        pixels[i*4 + 3] = src[i*4 + 3]; // A
    }

    glGenTextures(1, &tex->texture_id);
    glBindTexture(GL_TEXTURE_2D, tex->texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex->width, tex->height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    delete[] pixels;
    bitmap->UnlockBits(&bitmapData);
    delete bitmap;
    stream->Release(); // safe: all pixel data is now in GL
    return true;
}

// ------------------------------------------------------------------
// RenderTextToTexture
// ------------------------------------------------------------------
bool RenderTextToTexture(const char* text, const char* font_name, float size,
                         float r, float g, float b, TextTexture* tex)
{
    wchar_t wtext[256];
    wchar_t wfont[64];
    MultiByteToWideChar(CP_UTF8, 0, text,      -1, wtext, 256);
    MultiByteToWideChar(CP_UTF8, 0, font_name, -1, wfont,  64);

    Gdiplus::Bitmap  temp_bitmap(1, 1, PixelFormat32bppARGB);
    Gdiplus::Graphics temp_g(&temp_bitmap);
    Gdiplus::Font font_obj(wfont, (Gdiplus::REAL)size,
                           Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::RectF layout(0.0f, 0.0f, 2048.0f, 2048.0f);
    Gdiplus::RectF bounds;
    temp_g.MeasureString(wtext, -1, &font_obj, layout, &bounds);

    int width  = (int)(bounds.Width)  + 8;
    int height = (int)(bounds.Height) + 8;
    if (width <= 0 || height <= 0) return false;

    Gdiplus::Bitmap*   bitmap = new Gdiplus::Bitmap(width, height, PixelFormat32bppARGB);
    Gdiplus::Graphics* gfx    = new Gdiplus::Graphics(bitmap);
    gfx->Clear(Gdiplus::Color(0, 0, 0, 0));
    gfx->SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
    Gdiplus::SolidBrush brush(Gdiplus::Color(255,
        (BYTE)(r * 255.0f), (BYTE)(g * 255.0f), (BYTE)(b * 255.0f)));
    Gdiplus::PointF origin(4.0f, 4.0f);
    gfx->DrawString(wtext, -1, &font_obj, origin, &brush);
    delete gfx;

    tex->width  = width;
    tex->height = height;

    Gdiplus::Rect rect(0, 0, width, height);
    Gdiplus::BitmapData bitmapData;
    if (bitmap->LockBits(&rect, Gdiplus::ImageLockModeRead,
                         PixelFormat32bppARGB, &bitmapData) != Gdiplus::Ok) {
        delete bitmap;
        return false;
    }

    unsigned char* pixels = new unsigned char[width * height * 4];
    unsigned char* src    = (unsigned char*)bitmapData.Scan0;
    for (int i = 0; i < width * height; i++) {
        pixels[i*4 + 0] = src[i*4 + 2]; // R
        pixels[i*4 + 1] = src[i*4 + 1]; // G
        pixels[i*4 + 2] = src[i*4 + 0]; // B
        pixels[i*4 + 3] = src[i*4 + 3]; // A
    }

    glGenTextures(1, &tex->texture_id);
    glBindTexture(GL_TEXTURE_2D, tex->texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    delete[] pixels;
    bitmap->UnlockBits(&bitmapData);
    delete bitmap;
    return true;
}

// ------------------------------------------------------------------
// Cue file parsers
// ------------------------------------------------------------------

static void TrimLeft(char*& p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
}

bool LoadImageCue(const char* cues_path, ImageCue* cue)
{
    FILE* f = nullptr;
    fopen_s(&f, cues_path, "r");
    if (!f) return false;

    char line[1024];
    bool in_section = false;
    bool found      = false;

    while (fgets(line, sizeof(line), f)) {
        char* s = line; TrimLeft(s);
        if (strstr(s, "[image_cues]")) { in_section = true;  continue; }
        if (s[0] == '[' && in_section)  {                    break;    }
        if (!in_section || s[0] == '#' || s[0] == '\0' || s[0] == '\n') continue;

        // Format: asset_key|asset_path|x|y|scale|opacity|cue_start|cue_end|
        //         layer_order|effect_type|fade_in_start|fade_in_end|
        //         fade_out_start|fade_out_end
        char* pipe1 = strchr(s, '|');
        if (!pipe1) continue;
        *pipe1 = '\0';
        strncpy_s(cue->asset_key, s, _TRUNCATE);

        char* pipe2 = strchr(pipe1 + 1, '|');
        if (!pipe2) continue;
        *pipe2 = '\0';
        strncpy_s(cue->asset_path, pipe1 + 1, _TRUNCATE);

        int layer_order = 0;
        if (sscanf_s(pipe2 + 1,
                     "%f|%f|%f|%f|%f|%f|%d|%d|%f|%f|%f|%f",
                     &cue->x, &cue->y, &cue->scale, &cue->opacity,
                     &cue->cue_start, &cue->cue_end,
                     &layer_order, &cue->effect_type,
                     &cue->fade_in_start,  &cue->fade_in_end,
                     &cue->fade_out_start, &cue->fade_out_end) >= 6) {
            cue->layer_order = layer_order;
            found = true;
            break;
        }
    }

    fclose(f);
    return found;
}

bool LoadTextCue(const char* cues_path, TextCue* cue)
{
    FILE* f = nullptr;
    fopen_s(&f, cues_path, "r");
    if (!f) return false;

    char line[2048];
    bool in_section = false;
    bool found      = false;

    while (fgets(line, sizeof(line), f)) {
        char* s = line; TrimLeft(s);
        if (strstr(s, "[text_cues]")) { in_section = true;  continue; }
        if (s[0] == '[' && in_section) {                    break;    }
        if (!in_section || s[0] == '#' || s[0] == '\0' || s[0] == '\n') continue;

        // Format: text|font_name|x|y|size|color_r|color_g|color_b|
        //         effect_type|cue_start|cue_end|
        //         fade_in_start|fade_in_end|fade_out_start|fade_out_end|layer_order
        char* pipe1 = strchr(s, '|');
        if (!pipe1) continue;
        *pipe1 = '\0';
        strncpy_s(cue->text, s, _TRUNCATE);

        char* pipe2 = strchr(pipe1 + 1, '|');
        if (!pipe2) continue;
        *pipe2 = '\0';
        strncpy_s(cue->font_name, pipe1 + 1, _TRUNCATE);

        int layer_order = 0;
        if (sscanf_s(pipe2 + 1,
                     "%f|%f|%f|%f|%f|%f|%d|%f|%f|%f|%f|%f|%f|%d",
                     &cue->x, &cue->y, &cue->size,
                     &cue->color.r, &cue->color.g, &cue->color.b,
                     &cue->effect_type,
                     &cue->cue_start, &cue->cue_end,
                     &cue->fade_in_start,  &cue->fade_in_end,
                     &cue->fade_out_start, &cue->fade_out_end,
                     &layer_order) >= 13) {
            cue->layer_order = layer_order;
            found = true;
            break;
        }
    }

    fclose(f);
    return found;
}

bool LoadMusicCue(const char* cues_path, MusicCue* cue)
{
    FILE* f = nullptr;
    fopen_s(&f, cues_path, "r");
    if (!f) return false;

    char line[1024];
    bool in_section = false;
    bool found      = false;

    while (fgets(line, sizeof(line), f)) {
        char* s = line; TrimLeft(s);
        if (strstr(s, "[music_cues]")) { in_section = true;  continue; }
        if (s[0] == '[' && in_section) {                    break;    }
        if (!in_section || s[0] == '#' || s[0] == '\0' || s[0] == '\n') continue;

        // Format: asset_key|asset_path|cue_start|cue_end
        char* pipe1 = strchr(s, '|');
        if (!pipe1) continue;
        size_t key_len = (size_t)(pipe1 - s);
        if (key_len >= sizeof(cue->asset_key)) key_len = sizeof(cue->asset_key) - 1;
        strncpy_s(cue->asset_key, s, key_len);
        cue->asset_key[key_len] = '\0';

        char* pipe2 = strchr(pipe1 + 1, '|');
        if (!pipe2) continue;
        size_t path_len = (size_t)(pipe2 - (pipe1 + 1));
        if (path_len >= sizeof(cue->asset_path)) path_len = sizeof(cue->asset_path) - 1;
        strncpy_s(cue->asset_path, pipe1 + 1, path_len);
        cue->asset_path[path_len] = '\0';

        if (sscanf_s(pipe2 + 1, "%f|%f", &cue->cue_start, &cue->cue_end) == 2) {
            found = true;
            break;
        }
    }

    fclose(f);
    return found;
}

} // namespace runtime
} // namespace rev
