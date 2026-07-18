#include "rev_runtime.h"
#include <windows.h>
#include <gl/gl.h>
#include <gdiplus.h>
#include <wincodec.h>
#include <cstdio>
#include <cstring>
#include <cmath>

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

static float Clamp01(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

static float Hash01(unsigned int x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return (float)(x & 0x00FFFFFFU) / 16777215.0f;
}

bool BuildTextEffectFrame(const TextCue* cue, float time, TextEffectFrame* out)
{
    if (!cue || !out) return false;

    memset(out, 0, sizeof(*out));
    strncpy_s(out->text, cue->text, _TRUNCATE);
    out->offset_x = 0.0f;
    out->offset_y = 0.0f;
    out->opacity_mul = 1.0f;

    const float in_dur = cue->fade_in_end - cue->fade_in_start;
    const float out_dur = cue->fade_out_end - cue->fade_out_start;
    float in_t = 1.0f;
    float out_t = 0.0f;

    if (in_dur > 0.0f) in_t = Clamp01((time - cue->fade_in_start) / in_dur);
    if (out_dur > 0.0f) out_t = Clamp01((time - cue->fade_out_start) / out_dur);

    if (cue->effect_type == 2) {
        // Classic scroll: enters from below, exits upward.
        out->offset_y = (1.0f - in_t) * 0.08f - out_t * 0.08f;
    } else if (cue->effect_type == 3) {
        // Line-by-line reveal / hide.
        int total_lines = 1;
        for (int i = 0; cue->text[i] != '\0'; ++i) {
            if (cue->text[i] == '\n') total_lines++;
        }

        int visible_lines = (int)(in_t * (float)total_lines + 0.999f);
        if (visible_lines < 0) visible_lines = 0;
        if (visible_lines > total_lines) visible_lines = total_lines;

        if (out_t > 0.0f) {
            int hide_lines = (int)(out_t * (float)total_lines + 0.999f);
            int remain = total_lines - hide_lines;
            if (remain < visible_lines) visible_lines = remain;
            if (visible_lines < 0) visible_lines = 0;
        }

        char result[256] = {};
        int ri = 0;
        int line_idx = 1;
        for (int i = 0; cue->text[i] != '\0' && ri < 255; ++i) {
            if (line_idx > visible_lines) break;
            result[ri++] = cue->text[i];
            if (cue->text[i] == '\n') line_idx++;
        }
        result[ri] = '\0';
        strncpy_s(out->text, result, _TRUNCATE);
    } else if (cue->effect_type == 4) {
        // Character-by-character reveal / hide.
        const int len = (int)strlen(cue->text);
        int visible = (int)(in_t * (float)len + 0.999f);
        if (visible < 0) visible = 0;
        if (visible > len) visible = len;

        if (out_t > 0.0f) {
            int hide = (int)(out_t * (float)len + 0.999f);
            int remain = len - hide;
            if (remain < visible) visible = remain;
            if (visible < 0) visible = 0;
        }

        strncpy_s(out->text, cue->text, visible);
        out->text[visible] = '\0';
        out->opacity_mul = 0.6f + 0.4f * in_t;
    } else if (cue->effect_type == 5) {
        // Sandstorm gather/disperse: stochastic character reveal + jitter.
        char result[256] = {};
        int ri = 0;
        bool has_visible = false;

        for (int i = 0; cue->text[i] != '\0' && ri < 255; ++i) {
            const char c = cue->text[i];
            if (c == '\n') {
                result[ri++] = '\n';
                continue;
            }

            if (c == ' ') {
                result[ri++] = ' ';
                continue;
            }

            float score = Hash01((unsigned int)(i * 131 + 17));
            bool appear = (score <= in_t);
            bool keep = (score > out_t);
            if (appear && keep) {
                result[ri++] = c;
                has_visible = true;
            } else {
                result[ri++] = ' ';
            }
        }
        result[ri] = '\0';
        strncpy_s(out->text, result, _TRUNCATE);

        float gather_amp = (1.0f - in_t) * (1.0f - out_t) * 0.045f;
        float spread_amp = out_t * 0.065f;
        float amp = (gather_amp > spread_amp) ? gather_amp : spread_amp;
        out->offset_x = sinf(time * 21.7f + 1.7f) * amp;
        out->offset_y = cosf(time * 17.9f + 0.4f) * amp;
        out->opacity_mul = (1.0f - out_t) * (0.35f + 0.65f * in_t);

        if (!has_visible && in_t > 0.0f && out_t < 1.0f) {
            // Keep at least one glyph visible during transition to avoid hard pop.
            for (int i = 0; result[i] != '\0'; ++i) {
                if (result[i] != ' ' && result[i] != '\n') {
                    has_visible = true;
                    break;
                }
                if (cue->text[i] != ' ' && cue->text[i] != '\n') {
                    result[i] = cue->text[i];
                    has_visible = true;
                    break;
                }
            }
            strncpy_s(out->text, result, _TRUNCATE);
        }
    }

    // No drawable glyphs left this frame.
    bool has_drawable = false;
    for (int i = 0; out->text[i] != '\0'; ++i) {
        if (out->text[i] != ' ' && out->text[i] != '\n' && out->text[i] != '\r' && out->text[i] != '\t') {
            has_drawable = true;
            break;
        }
    }

    return has_drawable;
}

// ------------------------------------------------------------------
// LoadImageTexture  (file path -> GL texture)
// ------------------------------------------------------------------
static bool UploadRgbaTexture(const unsigned char* pixels, int width, int height,
                              ImageTexture* tex)
{
    if (!pixels || width <= 0 || height <= 0 || !tex) return false;

    tex->width = width;
    tex->height = height;
    glGenTextures(1, &tex->texture_id);
    glBindTexture(GL_TEXTURE_2D, tex->texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    return true;
}

static bool LoadWicImageTextureFromStream(IStream* stream, int frame_index,
                                          ImageTexture* tex, int* out_frame_count)
{
    if (!stream || !tex) return false;

    HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool uninitialize_com = SUCCEEDED(com_result);

    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    bool loaded = false;
    UINT frame_count = 0;

    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                   CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(factory->CreateDecoderFromStream(
            stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder)) &&
        SUCCEEDED(decoder->GetFrameCount(&frame_count)) &&
        SUCCEEDED(decoder->GetFrame(
            frame_count > 0 ? (UINT)(frame_index < 0 ? 0 : frame_index) % frame_count : 0,
            &frame)) &&
        SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(
            frame, GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone, nullptr, 0.0,
            WICBitmapPaletteTypeCustom))) {
        UINT width = 0;
        UINT height = 0;
        if (SUCCEEDED(converter->GetSize(&width, &height)) &&
            width <= static_cast<UINT>(INT_MAX / 4) &&
            height <= static_cast<UINT>(INT_MAX / 4)) {
            UINT stride = width * 4;
            UINT buffer_size = stride * height;
            unsigned char* pixels = new unsigned char[buffer_size];
            if (SUCCEEDED(converter->CopyPixels(nullptr, stride, buffer_size, pixels))) {
                loaded = UploadRgbaTexture(pixels, (int)width, (int)height, tex);
            }
            delete[] pixels;
        }
    }

    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (factory) factory->Release();
    if (uninitialize_com) CoUninitialize();
    if (out_frame_count) *out_frame_count = (int)frame_count;
    return loaded;
}

static bool LoadWicImageTextureFromMemory(const unsigned char* data, size_t size,
                                          int frame_index, ImageTexture* tex,
                                          int* out_frame_count)
{
    if (!data || size == 0 || !tex) return false;

    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!memory) return false;
    void* destination = GlobalLock(memory);
    if (!destination) {
        GlobalFree(memory);
        return false;
    }
    memcpy(destination, data, size);
    GlobalUnlock(memory);

    IStream* stream = nullptr;
    HRESULT result = CreateStreamOnHGlobal(memory, TRUE, &stream);
    if (FAILED(result)) {
        GlobalFree(memory);
        return false;
    }

    bool loaded = LoadWicImageTextureFromStream(stream, frame_index, tex, out_frame_count);
    stream->Release();
    return loaded;
}

static bool LoadWicImageTextureFromFile(const wchar_t* path, int frame_index,
                                        ImageTexture* tex, int* out_frame_count)
{
    if (!path || !tex) return false;

    HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool uninitialize_com = SUCCEEDED(com_result);
    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    bool loaded = false;
    UINT frame_count = 0;

    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                   CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(factory->CreateDecoderFromFilename(
            path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad,
            &decoder)) &&
        SUCCEEDED(decoder->GetFrameCount(&frame_count)) &&
        SUCCEEDED(decoder->GetFrame(
            frame_count > 0 ? (UINT)(frame_index < 0 ? 0 : frame_index) % frame_count : 0,
            &frame)) &&
        SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(
            frame, GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone, nullptr, 0.0,
            WICBitmapPaletteTypeCustom))) {
        UINT width = 0;
        UINT height = 0;
        if (SUCCEEDED(converter->GetSize(&width, &height)) &&
            width <= static_cast<UINT>(INT_MAX / 4) &&
            height <= static_cast<UINT>(INT_MAX / 4)) {
            UINT stride = width * 4;
            UINT buffer_size = stride * height;
            unsigned char* pixels = new unsigned char[buffer_size];
            if (SUCCEEDED(converter->CopyPixels(nullptr, stride, buffer_size, pixels))) {
                loaded = UploadRgbaTexture(pixels, (int)width, (int)height, tex);
            }
            delete[] pixels;
        }
    }

    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (factory) factory->Release();
    if (uninitialize_com) CoUninitialize();
    if (out_frame_count) *out_frame_count = (int)frame_count;
    return loaded;
}

bool LoadImageTexture(const char* path, ImageTexture* tex)
{
    if (!path || !tex) return false;
    wchar_t wpath[512];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 512);

    Gdiplus::Bitmap* bitmap = new Gdiplus::Bitmap(wpath);
    if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok) {
        delete bitmap;
        return LoadWicImageTextureFromFile(wpath, 0, tex, nullptr);
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
    if (!data || size == 0 || !tex) return false;
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
        return LoadWicImageTextureFromMemory(data, size, 0, tex, nullptr);
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

bool LoadImageTextureFrame(const char* path, int frame_index, ImageTexture* tex)
{
    if (!path || !tex) return false;
    wchar_t wpath[512] = {};
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 512);
    return LoadWicImageTextureFromFile(wpath, frame_index, tex, nullptr);
}

bool LoadImageTextureFrameFromMemory(const unsigned char* data, size_t size,
                                     int frame_index, ImageTexture* tex)
{
    return LoadWicImageTextureFromMemory(data, size, frame_index, tex, nullptr);
}

int GetImageFrameCount(const char* path)
{
    if (!path) return 0;
    wchar_t wpath[512] = {};
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 512);
    ImageTexture unused = {};
    int frame_count = 0;
    LoadWicImageTextureFromFile(wpath, 0, &unused, &frame_count);
    if (unused.texture_id != 0) glDeleteTextures(1, &unused.texture_id);
    return frame_count;
}

int GetImageFrameCountFromMemory(const unsigned char* data, size_t size)
{
    ImageTexture unused = {};
    int frame_count = 0;
    LoadWicImageTextureFromMemory(data, size, 0, &unused, &frame_count);
    if (unused.texture_id != 0) glDeleteTextures(1, &unused.texture_id);
    return frame_count;
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
    
    // Create StringFormat once for both measuring and drawing.
    // Keep explicit newlines but disable automatic word wrapping.
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentNear);
    format.SetLineAlignment(Gdiplus::StringAlignmentNear);
    format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    format.SetTrimming(Gdiplus::StringTrimmingNone);
    
    Gdiplus::RectF layout(0.0f, 0.0f, 2048.0f, 2048.0f);
    Gdiplus::RectF bounds;
    temp_g.MeasureString(wtext, -1, &font_obj, layout, &format, &bounds);

    int width  = (int)(bounds.Width)  + 16;
    int height = (int)(bounds.Height) + 8;
    if (width <= 0 || height <= 0) return false;

    Gdiplus::Bitmap*   bitmap = new Gdiplus::Bitmap(width, height, PixelFormat32bppARGB);
    Gdiplus::Graphics* gfx    = new Gdiplus::Graphics(bitmap);
    gfx->Clear(Gdiplus::Color(0, 0, 0, 0));
    gfx->SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
    Gdiplus::SolidBrush brush(Gdiplus::Color(255,
        (BYTE)(r * 255.0f), (BYTE)(g * 255.0f), (BYTE)(b * 255.0f)));
    
    // Use the same StringFormat for drawing
    Gdiplus::RectF draw_rect(4.0f, 4.0f, (float)width - 8.0f, (float)height - 8.0f);
    gfx->DrawString(wtext, -1, &font_obj, draw_rect, &format, &brush);
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

bool CreateTextGlyphAtlas(const char* font_name, float size, TextGlyphAtlas* atlas)
{
    if (!font_name || !font_name[0] || !atlas || size <= 0.0f) return false;
    memset(atlas, 0, sizeof(*atlas));

    wchar_t wfont[64] = {};
    MultiByteToWideChar(CP_UTF8, 0, font_name, -1, wfont, (int)_countof(wfont));
    Gdiplus::Bitmap measure_bitmap(1, 1, PixelFormat32bppARGB);
    Gdiplus::Graphics measure_graphics(&measure_bitmap);
    Gdiplus::Font font(wfont, (Gdiplus::REAL)size,
                       Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentNear);
    format.SetLineAlignment(Gdiplus::StringAlignmentNear);
    format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    format.SetTrimming(Gdiplus::StringTrimmingNone);

    const int padding = 4;
    const int atlas_width = 2048;
    int row_x = padding;
    int row_y = padding;
    int row_height = 0;
    int atlas_height = padding;
    Gdiplus::RectF bounds;

    for (int codepoint = 32; codepoint < 127; ++codepoint) {
        wchar_t glyph_text[2] = {(wchar_t)codepoint, L'\0'};
        measure_graphics.MeasureString(glyph_text, -1, &font,
                                       Gdiplus::RectF(0.0f, 0.0f, 2048.0f, 2048.0f),
                                       &format, &bounds);
        int glyph_width = (int)ceilf(bounds.Width) + padding * 2;
        int glyph_height = (int)ceilf(bounds.Height) + padding * 2;
        if (glyph_width < padding * 2 + 1) glyph_width = padding * 2 + 1;
        if (glyph_height < padding * 2 + 1) glyph_height = padding * 2 + 1;
        if (row_x + glyph_width > atlas_width) {
            row_x = padding;
            row_y += row_height;
            row_height = 0;
        }
        if (row_y + glyph_height > 2048) return false;

        TextGlyph& glyph = atlas->glyphs[codepoint];
        glyph.codepoint = codepoint;
        glyph.u0 = (float)row_x / 2048.0f;
        glyph.v0 = (float)row_y / 2048.0f;
        glyph.u1 = (float)(row_x + glyph_width) / 2048.0f;
        glyph.v1 = (float)(row_y + glyph_height) / 2048.0f;
        glyph.width = (float)glyph_width;
        glyph.height = (float)glyph_height;
        glyph.advance = bounds.Width;
        glyph.bearing_y = bounds.Y;
        row_x += glyph_width;
        if (glyph_height > row_height) row_height = glyph_height;
        if (row_y + row_height > atlas_height) atlas_height = row_y + row_height;
    }

    atlas->width = atlas_width;
    atlas->height = atlas_height;
    atlas->font_size = size;
    atlas->line_height = size * 1.25f;
    if (atlas->line_height < 1.0f) atlas->line_height = 1.0f;
    for (int codepoint = 32; codepoint < 127; ++codepoint) {
        TextGlyph& glyph = atlas->glyphs[codepoint];
        glyph.v0 *= 2048.0f / (float)atlas->height;
        glyph.v1 *= 2048.0f / (float)atlas->height;
    }

    Gdiplus::Bitmap bitmap(atlas->width, atlas->height, PixelFormat32bppARGB);
    Gdiplus::Graphics graphics(&bitmap);
    graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
    Gdiplus::SolidBrush brush(Gdiplus::Color(255, 255, 255, 255));
    row_x = padding;
    row_y = padding;
    row_height = 0;
    for (int codepoint = 32; codepoint < 127; ++codepoint) {
        const TextGlyph& glyph = atlas->glyphs[codepoint];
        int glyph_width = (int)ceilf(glyph.width);
        int glyph_height = (int)ceilf(glyph.height);
        if (row_x + glyph_width > atlas->width) {
            row_x = padding;
            row_y += row_height;
            row_height = 0;
        }
        wchar_t glyph_text[2] = {(wchar_t)codepoint, L'\0'};
        graphics.DrawString(glyph_text, -1, &font,
                            Gdiplus::RectF((Gdiplus::REAL)row_x + padding,
                                           (Gdiplus::REAL)row_y + padding,
                                           (Gdiplus::REAL)glyph_width - padding * 2,
                                           (Gdiplus::REAL)glyph_height - padding * 2),
                            &format, &brush);
        row_x += glyph_width;
        if (glyph_height > row_height) row_height = glyph_height;
    }

    Gdiplus::Rect rect(0, 0, atlas->width, atlas->height);
    Gdiplus::BitmapData bitmap_data;
    if (bitmap.LockBits(&rect, Gdiplus::ImageLockModeRead,
                        PixelFormat32bppARGB, &bitmap_data) != Gdiplus::Ok) {
        return false;
    }
    unsigned char* pixels = new unsigned char[atlas->width * atlas->height * 4];
    unsigned char* src = (unsigned char*)bitmap_data.Scan0;
    for (int i = 0; i < atlas->width * atlas->height; ++i) {
        pixels[i * 4 + 0] = 255;
        pixels[i * 4 + 1] = 255;
        pixels[i * 4 + 2] = 255;
        pixels[i * 4 + 3] = src[i * 4 + 3];
    }
    glGenTextures(1, &atlas->texture_id);
    glBindTexture(GL_TEXTURE_2D, atlas->texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, atlas->width, atlas->height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    delete[] pixels;
    bitmap.UnlockBits(&bitmap_data);
    return atlas->texture_id != 0;
}

float ComputeScrollTextTravel(const TextGlyphAtlas* atlas, const char* text,
                              int direction, float size_scale, float spacing,
                              float wrap_gap, float viewport_width,
                              float viewport_height)
{
    if (!atlas || !text || viewport_width <= 0.0f || viewport_height <= 0.0f) {
        return 1.0f + wrap_gap;
    }

    if (size_scale < 0.0f) size_scale = 0.0f;
    if (spacing < 0.01f) spacing = 0.01f;
    if (wrap_gap < 0.0f) wrap_gap = 0.0f;

    float travel = 0.0f;
    if (direction <= 1) {
        for (const unsigned char* p = (const unsigned char*)text; *p && *p != '\n'; ++p) {
            const TextGlyph* glyph = FindTextGlyph(atlas, *p);
            if (glyph) travel += glyph->advance * spacing * size_scale;
        }
        const TextGlyph* space = FindTextGlyph(atlas, ' ');
        if (space) travel += space->advance * spacing * size_scale * 3.0f;
        travel = travel * 2.0f / viewport_width + wrap_gap;
    } else {
        int line_count = 1;
        for (const char* p = text; *p; ++p) {
            if (*p == '\n') ++line_count;
        }
        travel = (float)line_count * atlas->line_height * size_scale * 2.0f / viewport_height + wrap_gap;
    }

    return travel < 0.001f ? 0.001f : travel;
}

static bool GetPngEncoderClsid(CLSID* clsid)
{
    if (!clsid) return false;
    UINT count = 0, bytes = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &bytes) != Gdiplus::Ok || bytes == 0) return false;
    unsigned char* buffer = new unsigned char[bytes];
    Gdiplus::ImageCodecInfo* codecs = (Gdiplus::ImageCodecInfo*)buffer;
    bool found = false;
    if (Gdiplus::GetImageEncoders(count, bytes, codecs) == Gdiplus::Ok) {
        for (UINT i = 0; i < count; ++i) {
            if (wcscmp(codecs[i].MimeType, L"image/png") == 0) {
                *clsid = codecs[i].Clsid;
                found = true;
                break;
            }
        }
    }
    delete[] buffer;
    return found;
}

bool SaveTextGlyphAtlas(const char* font_name, float size,
                        const char* image_path, const char* metadata_path)
{
    if (!font_name || !font_name[0] || !image_path || !metadata_path || size <= 0.0f) return false;

    wchar_t wfont[64] = {};
    wchar_t wimage[512] = {};
    MultiByteToWideChar(CP_UTF8, 0, font_name, -1, wfont, (int)_countof(wfont));
    MultiByteToWideChar(CP_UTF8, 0, image_path, -1, wimage, (int)_countof(wimage));

    Gdiplus::Bitmap measure_bitmap(1, 1, PixelFormat32bppARGB);
    Gdiplus::Graphics measure_graphics(&measure_bitmap);
    Gdiplus::Font font(wfont, (Gdiplus::REAL)size, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentNear);
    format.SetLineAlignment(Gdiplus::StringAlignmentNear);
    format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    format.SetTrimming(Gdiplus::StringTrimmingNone);

    const int padding = 4;
    const int atlas_width = 2048;
    const int atlas_height = 2048;
    TextGlyph glyphs[128] = {};
    int row_x = padding, row_y = padding, row_height = 0;
    for (int codepoint = 32; codepoint < 127; ++codepoint) {
        wchar_t glyph_text[2] = {(wchar_t)codepoint, L'\0'};
        Gdiplus::RectF bounds;
        measure_graphics.MeasureString(glyph_text, -1, &font,
            Gdiplus::RectF(0.0f, 0.0f, 2048.0f, 2048.0f), &format, &bounds);
        int glyph_width = (int)ceilf(bounds.Width) + padding * 2;
        int glyph_height = (int)ceilf(bounds.Height) + padding * 2;
        if (glyph_width < padding * 2 + 1) glyph_width = padding * 2 + 1;
        if (glyph_height < padding * 2 + 1) glyph_height = padding * 2 + 1;
        if (row_x + glyph_width > atlas_width) {
            row_x = padding;
            row_y += row_height;
            row_height = 0;
        }
        if (row_y + glyph_height > atlas_height) return false;
        TextGlyph& glyph = glyphs[codepoint];
        glyph.codepoint = codepoint;
        glyph.u0 = (float)row_x / atlas_width;
        glyph.v0 = (float)row_y / atlas_height;
        glyph.u1 = (float)(row_x + glyph_width) / atlas_width;
        glyph.v1 = (float)(row_y + glyph_height) / atlas_height;
        glyph.width = (float)glyph_width;
        glyph.height = (float)glyph_height;
        glyph.advance = bounds.Width;
        glyph.bearing_y = bounds.Y;
        row_x += glyph_width;
        if (glyph_height > row_height) row_height = glyph_height;
    }

    Gdiplus::Bitmap bitmap(atlas_width, atlas_height, PixelFormat32bppARGB);
    Gdiplus::Graphics graphics(&bitmap);
    graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
    Gdiplus::SolidBrush brush(Gdiplus::Color(255, 255, 255, 255));
    row_x = padding;
    row_y = padding;
    row_height = 0;
    for (int codepoint = 32; codepoint < 127; ++codepoint) {
        const TextGlyph& glyph = glyphs[codepoint];
        int glyph_width = (int)ceilf(glyph.width);
        int glyph_height = (int)ceilf(glyph.height);
        if (row_x + glyph_width > atlas_width) {
            row_x = padding;
            row_y += row_height;
            row_height = 0;
        }
        wchar_t glyph_text[2] = {(wchar_t)codepoint, L'\0'};
        graphics.DrawString(glyph_text, -1, &font,
            Gdiplus::RectF((Gdiplus::REAL)row_x + padding,
                           (Gdiplus::REAL)row_y + padding,
                           (Gdiplus::REAL)glyph_width - padding * 2,
                           (Gdiplus::REAL)glyph_height - padding * 2),
            &format, &brush);
        row_x += glyph_width;
        if (glyph_height > row_height) row_height = glyph_height;
    }

    CLSID png_clsid;
    if (!GetPngEncoderClsid(&png_clsid) || bitmap.Save(wimage, &png_clsid, nullptr) != Gdiplus::Ok) return false;
    FILE* meta = nullptr;
    fopen_s(&meta, metadata_path, "wb");
    if (!meta) return false;
    fprintf(meta, "REV_GLYPH_ATLAS_V1|%d|%d|%.6f|%.6f\n", atlas_width, atlas_height, size, size * 1.25f);
    for (int codepoint = 32; codepoint < 127; ++codepoint) {
        const TextGlyph& glyph = glyphs[codepoint];
        fprintf(meta, "%d|%.9f|%.9f|%.9f|%.9f|%.6f|%.6f|%.6f|%.6f\n",
                glyph.codepoint, glyph.u0, glyph.v0, glyph.u1, glyph.v1,
                glyph.width, glyph.height, glyph.advance, glyph.bearing_y);
    }
    fclose(meta);
    return true;
}

bool LoadTextGlyphAtlasFromMemory(const unsigned char* image_data, size_t image_size,
                                  const unsigned char* metadata_data, size_t metadata_size,
                                  TextGlyphAtlas* atlas)
{
    if (!image_data || image_size == 0 || !metadata_data || metadata_size == 0 || !atlas) return false;
    memset(atlas, 0, sizeof(*atlas));
    ImageTexture texture = {};
    if (!LoadImageTextureFromMemory(image_data, image_size, &texture)) return false;
    char* metadata = new char[metadata_size + 1];
    memcpy(metadata, metadata_data, metadata_size);
    metadata[metadata_size] = '\0';
    char* line = strtok(metadata, "\r\n");
    int width = 0, height = 0;
    float font_size = 0.0f, line_height = 0.0f;
    bool header_ok = line && sscanf_s(line, "REV_GLYPH_ATLAS_V1|%d|%d|%f|%f",
                                      &width, &height, &font_size, &line_height) == 4;
    if (!header_ok) {
        delete[] metadata;
        glDeleteTextures(1, &texture.texture_id);
        return false;
    }
    while ((line = strtok(nullptr, "\r\n")) != nullptr) {
        TextGlyph glyph = {};
        if (sscanf_s(line, "%d|%f|%f|%f|%f|%f|%f|%f|%f",
                     &glyph.codepoint, &glyph.u0, &glyph.v0, &glyph.u1, &glyph.v1,
                     &glyph.width, &glyph.height, &glyph.advance, &glyph.bearing_y) == 9 &&
            glyph.codepoint >= 0 && glyph.codepoint < 128) {
            atlas->glyphs[glyph.codepoint] = glyph;
        }
    }
    delete[] metadata;
    atlas->texture_id = texture.texture_id;
    atlas->width = width > 0 ? width : texture.width;
    atlas->height = height > 0 ? height : texture.height;
    atlas->font_size = font_size;
    atlas->line_height = line_height > 0.0f ? line_height : font_size * 1.25f;
    return true;
}

void DestroyTextGlyphAtlas(TextGlyphAtlas* atlas)
{
    if (!atlas) return;
    if (atlas->texture_id != 0) glDeleteTextures(1, &atlas->texture_id);
    memset(atlas, 0, sizeof(*atlas));
}

const TextGlyph* FindTextGlyph(const TextGlyphAtlas* atlas, unsigned int codepoint)
{
    if (!atlas || codepoint >= 128 || atlas->glyphs[codepoint].codepoint == 0) return nullptr;
    return &atlas->glyphs[codepoint];
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
        //         fade_out_start|fade_out_end|curve_x|curve_y|curve_scale|curve_opacity|blend_mode|rotation|curve_rotation
        char* pipe1 = strchr(s, '|');
        if (!pipe1) continue;
        *pipe1 = '\0';
        strncpy_s(cue->asset_key, s, _TRUNCATE);

        char* pipe2 = strchr(pipe1 + 1, '|');
        if (!pipe2) continue;
        *pipe2 = '\0';
        strncpy_s(cue->asset_path, pipe1 + 1, _TRUNCATE);

        int layer_order = 0;
        int curve_x = -1, curve_y = -1, curve_scale = -1, curve_opacity = -1, curve_rotation = -1;
        int blend_mode = 0;
        int scanned = sscanf_s(pipe2 + 1,
                 "%f|%f|%f|%f|%f|%f|%d|%d|%f|%f|%f|%f|%d|%d|%d|%d|%d|%f|%d",
                     &cue->x, &cue->y, &cue->scale, &cue->opacity,
                     &cue->cue_start, &cue->cue_end,
                     &layer_order, &cue->effect_type,
                     &cue->fade_in_start,  &cue->fade_in_end,
                     &cue->fade_out_start, &cue->fade_out_end,
                 &curve_x, &curve_y, &curve_scale, &curve_opacity,
                 &blend_mode, &cue->rotation, &curve_rotation);
        
        if (scanned >= 6) {
            cue->layer_order = layer_order;
            // Load curve assignments if present (backwards compatible)
            if (scanned >= 16) {
                cue->curve_x = curve_x;
                cue->curve_y = curve_y;
                cue->curve_scale = curve_scale;
                cue->curve_rotation = curve_rotation;
                cue->curve_opacity = curve_opacity;
            } else {
                cue->curve_x = cue->curve_y = cue->curve_scale = cue->curve_rotation = cue->curve_opacity = -1;
            }
            cue->blend_mode = (scanned >= 17) ? blend_mode : 0;
            found = true;
            break;
        }
    }

    fclose(f);
    return found;
}

bool LoadAnimatedSpriteCue(const char* cues_path, AnimatedSpriteCue* cue)
{
    FILE* f = nullptr;
    fopen_s(&f, cues_path, "r");
    if (!f) return false;

    char line[8192];
    bool in_section = false;
    bool found      = false;

    cue->fps = 12.0f;
    cue->playback_mode = 0;
    cue->start_frame = 0;
    cue->curve_x = cue->curve_y = cue->curve_scale = cue->curve_rotation = cue->curve_opacity = cue->curve_frame = -1;

    while (fgets(line, sizeof(line), f)) {
        char* s = line; TrimLeft(s);
        if (strstr(s, "[animated_sprite_cues]")) { in_section = true; continue; }
        if (s[0] == '[' && in_section) { break; }
        if (!in_section || s[0] == '#' || s[0] == '\0' || s[0] == '\n') continue;

        char* pipe1 = strchr(s, '|');
        if (!pipe1) continue;
        *pipe1 = '\0';
        strncpy_s(cue->sprite_name, s, _TRUNCATE);

        char* pipe2 = strchr(pipe1 + 1, '|');
        if (!pipe2) continue;
        *pipe2 = '\0';
        strncpy_s(cue->frame_keys_csv, pipe1 + 1, _TRUNCATE);

        char* pipe3 = strchr(pipe2 + 1, '|');
        if (!pipe3) continue;
        *pipe3 = '\0';
        strncpy_s(cue->frame_paths_csv, pipe2 + 1, _TRUNCATE);

        int parsed = sscanf_s(pipe3 + 1,
            "%f|%f|%f|%f|%f|%f|%d|%d|%f|%f|%f|%f|%d|%f|%d|%d|%d|%d|%d|%d|%d|%f|%d",
            &cue->x, &cue->y, &cue->scale, &cue->opacity,
            &cue->cue_start, &cue->cue_end,
            &cue->layer_order, &cue->effect_type,
            &cue->fade_in_start, &cue->fade_in_end,
            &cue->fade_out_start, &cue->fade_out_end,
            &cue->blend_mode,
            &cue->fps,
            &cue->playback_mode,
            &cue->start_frame,
            &cue->curve_x, &cue->curve_y, &cue->curve_scale, &cue->curve_opacity, &cue->curve_frame,
            &cue->rotation, &cue->curve_rotation);

        if (parsed >= 6) {
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
        //         fade_in_start|fade_in_end|fade_out_start|fade_out_end|layer_order|blend_mode|
        //         curve_x|curve_y|curve_size|curve_color_r|curve_color_g|curve_color_b|
        //         bake_mode|baked_asset_key|baked_asset_path|glyph_atlas_key|glyph_atlas_path|glyph_meta_key|glyph_meta_path (optional)
        char* pipe1 = strchr(s, '|');
        if (!pipe1) continue;
        *pipe1 = '\0';
        
        // Decode literal \\n back to actual newlines
        char decoded_text[256];
        const char* src = s;
        char* dst = decoded_text;
        while (*src && (dst - decoded_text) < 254) {
            if (src[0] == '\\' && src[1] == 'n') {
                *dst++ = '\n';
                src += 2;
            } else {
                *dst++ = *src++;
            }
        }
        *dst = '\0';
        
        strncpy_s(cue->text, decoded_text, _TRUNCATE);

        char* pipe2 = strchr(pipe1 + 1, '|');
        if (!pipe2) continue;
        *pipe2 = '\0';
        strncpy_s(cue->font_name, pipe1 + 1, _TRUNCATE);

        int layer_order = 0;
        int blend_mode = 0;
        int curve_x = -1, curve_y = -1, curve_size = -1;
        int curve_color_r = -1, curve_color_g = -1, curve_color_b = -1;
        int bake_mode = 0;
        char baked_asset_key[64] = {};
        char baked_asset_path[512] = {};
        char glyph_atlas_key[64] = {};
        char glyph_atlas_path[512] = {};
        char glyph_meta_key[64] = {};
        char glyph_meta_path[512] = {};
        
        bool parsed_with_bake_mode = true;
        int parsed = sscanf_s(pipe2 + 1,
                 "%f|%f|%f|%f|%f|%f|%d|%f|%f|%f|%f|%f|%f|%d|%d|%d|%d|%d|%d|%d|%d|%d|%63[^|]|%511[^|]|%63[^|]|%511[^|]|%63[^|]|%511[^|\r\n]",
                     &cue->x, &cue->y, &cue->size,
                     &cue->color.r, &cue->color.g, &cue->color.b,
                     &cue->effect_type,
                     &cue->cue_start, &cue->cue_end,
                     &cue->fade_in_start,  &cue->fade_in_end,
                     &cue->fade_out_start, &cue->fade_out_end,
                     &layer_order,
                     &blend_mode,
                     &curve_x, &curve_y, &curve_size,
                     &curve_color_r, &curve_color_g, &curve_color_b,
                     &bake_mode,
                     baked_asset_key, (unsigned)_countof(baked_asset_key),
                     baked_asset_path, (unsigned)_countof(baked_asset_path),
                     glyph_atlas_key, (unsigned)_countof(glyph_atlas_key),
                     glyph_atlas_path, (unsigned)_countof(glyph_atlas_path),
                     glyph_meta_key, (unsigned)_countof(glyph_meta_key),
                     glyph_meta_path, (unsigned)_countof(glyph_meta_path));
        if (parsed < 23) {
            // Backward compatibility: older exports had no blend_mode column.
            parsed_with_bake_mode = false;
            parsed = sscanf_s(pipe2 + 1,
                 "%f|%f|%f|%f|%f|%f|%d|%f|%f|%f|%f|%f|%f|%d|%d|%d|%d|%d|%d|%d|%63[^|]|%511[^|\r\n]",
                     &cue->x, &cue->y, &cue->size,
                     &cue->color.r, &cue->color.g, &cue->color.b,
                     &cue->effect_type,
                     &cue->cue_start, &cue->cue_end,
                     &cue->fade_in_start,  &cue->fade_in_end,
                     &cue->fade_out_start, &cue->fade_out_end,
                     &layer_order,
                     &curve_x, &curve_y, &curve_size,
                     &curve_color_r, &curve_color_g, &curve_color_b,
                     baked_asset_key, (unsigned)_countof(baked_asset_key),
                     baked_asset_path, (unsigned)_countof(baked_asset_path));
        }
        
        if (parsed >= 14) {  // At least the old format
            cue->layer_order = layer_order;
            cue->blend_mode = (parsed_with_bake_mode && parsed >= 15) ? blend_mode : 0;
            // Curve fields (backwards compatible - default to -1)
            if (parsed_with_bake_mode) {
                cue->curve_x = (parsed >= 16) ? curve_x : -1;
                cue->curve_y = (parsed >= 17) ? curve_y : -1;
                cue->curve_size = (parsed >= 18) ? curve_size : -1;
                cue->curve_color_r = (parsed >= 19) ? curve_color_r : -1;
                cue->curve_color_g = (parsed >= 20) ? curve_color_g : -1;
                cue->curve_color_b = (parsed >= 21) ? curve_color_b : -1;
            } else {
                cue->curve_x = (parsed >= 15) ? curve_x : -1;
                cue->curve_y = (parsed >= 16) ? curve_y : -1;
                cue->curve_size = (parsed >= 17) ? curve_size : -1;
                cue->curve_color_r = (parsed >= 18) ? curve_color_r : -1;
                cue->curve_color_g = (parsed >= 19) ? curve_color_g : -1;
                cue->curve_color_b = (parsed >= 20) ? curve_color_b : -1;
            }
            cue->bake_mode = (parsed_with_bake_mode && parsed >= 22) ? bake_mode : 0;
            if (parsed_with_bake_mode) {
                if (parsed >= 23) {
                    strncpy_s(cue->baked_asset_key, baked_asset_key, _TRUNCATE);
                } else {
                    cue->baked_asset_key[0] = '\0';
                }
                if (parsed >= 24) {
                    strncpy_s(cue->baked_asset_path, baked_asset_path, _TRUNCATE);
                } else {
                    cue->baked_asset_path[0] = '\0';
                }
                if (parsed >= 25) strncpy_s(cue->glyph_atlas_key, glyph_atlas_key, _TRUNCATE);
                if (parsed >= 26) strncpy_s(cue->glyph_atlas_path, glyph_atlas_path, _TRUNCATE);
                if (parsed >= 27) strncpy_s(cue->glyph_meta_key, glyph_meta_key, _TRUNCATE);
                if (parsed >= 28) strncpy_s(cue->glyph_meta_path, glyph_meta_path, _TRUNCATE);
            } else {
                if (parsed >= 21) {
                    strncpy_s(cue->baked_asset_key, baked_asset_key, _TRUNCATE);
                } else {
                    cue->baked_asset_key[0] = '\0';
                }
                if (parsed >= 22) {
                    strncpy_s(cue->baked_asset_path, baked_asset_path, _TRUNCATE);
                } else {
                    cue->baked_asset_path[0] = '\0';
                }
            }
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

// ============================================================
// LoadMeshCue + Mat4 math
// ============================================================
#include <cmath>

namespace rev {
namespace runtime {

// ---- LoadMeshCue ---------------------------------------------------
// Format: asset_key|asset_path|mesh_type|pos_x|pos_y|pos_z|rot_x|rot_y|rot_z|
//         scale_x|scale_y|scale_z|color_r|color_g|color_b|color_a|
//         mesh_size|mesh_param|cue_start|cue_end|layer_order|
//         effect_type|fade_in_start|fade_in_end|fade_out_start|fade_out_end|
//         metallic|roughness|curve_pos_x|curve_pos_y|curve_pos_z|
//         curve_rot_x|curve_rot_y|curve_rot_z|curve_scale_x|curve_scale_y|curve_scale_z|
//         curve_color_r|curve_color_g|curve_color_b|curve_color_a|
//         curve_mesh_size|curve_metallic|curve_roughness|fov_deg|cull_mode
//         |curve_fov|use_imported_light|use_imported_camera
//         |emissive_r|emissive_g|emissive_b|emissive_strength
// mesh_type 4 = external file (asset_path holds path to .gltf/.glb)
bool LoadMeshCue(const char* cues_path, MeshCue* cue)
{
    FILE* f = nullptr;
    fopen_s(&f, cues_path, "r");
    if (!f) return false;

    cue->fov_deg = 45.0f;
    cue->cull_mode = 0;
    cue->emissive_color[0] = 1.0f;
    cue->emissive_color[1] = 1.0f;
    cue->emissive_color[2] = 1.0f;
    cue->emissive_strength = 0.0f;

    char line[2048];
    bool in_section = false;
    bool found      = false;

    while (fgets(line, sizeof(line), f)) {
        char* s = line; TrimLeft(s);
        if (strstr(s, "[mesh_cues]")) { in_section = true;  continue; }
        if (s[0] == '[' && in_section) {                    break;    }
        if (!in_section || s[0] == '#' || s[0] == '\0' || s[0] == '\n') continue;

        // Split off asset_key (before first pipe)
        char* pipe1 = strchr(s, '|');
        if (!pipe1) continue;
        size_t key_len = (size_t)(pipe1 - s);
        if (key_len >= sizeof(cue->asset_key)) key_len = sizeof(cue->asset_key) - 1;
        strncpy_s(cue->asset_key, s, key_len);
        cue->asset_key[key_len] = '\0';

        // Split off asset_path (between first and second pipe)
        char* pipe2 = strchr(pipe1 + 1, '|');
        if (!pipe2) continue;
        size_t path_len = (size_t)(pipe2 - (pipe1 + 1));
        if (path_len >= sizeof(cue->asset_path)) path_len = sizeof(cue->asset_path) - 1;
        strncpy_s(cue->asset_path, pipe1 + 1, path_len);
        cue->asset_path[path_len] = '\0';

        // Parse remaining numeric fields
        // Old 26-field format + 16 curve fields + optional fov/cull/curve_fov/toggles
        // + emissive controls = 51 fields total:
        // mesh_type|pos(3)|rot(3)|scale(3)|color(4)|mesh_size|mesh_param|
        // cue_start|cue_end|layer_order|effect_type|fade_in/out(4)|metallic|roughness|
        // curve_pos(3)|curve_rot(3)|curve_scale(3)|curve_color(4)|curve_mesh_size|curve_metallic|curve_roughness|fov_deg|cull_mode|curve_fov|use_imported_light|use_imported_camera|emissive_color(3)|emissive_strength
        cue->metallic  = 0.0f;  // defaults for old files
        cue->roughness = 0.5f;
        int curve_pos_x = -1, curve_pos_y = -1, curve_pos_z = -1;
        int curve_rot_x = -1, curve_rot_y = -1, curve_rot_z = -1;
        int curve_scale_x = -1, curve_scale_y = -1, curve_scale_z = -1;
        int curve_color_r = -1, curve_color_g = -1, curve_color_b = -1, curve_color_a = -1;
        int curve_mesh_size = -1, curve_metallic = -1, curve_roughness = -1;
        float fov_deg = 45.0f;
        int cull_mode = 0;
        int curve_fov = -1;
        int use_imported_light = 0;
        int use_imported_camera = 0;
        float emissive_r = 1.0f;
        float emissive_g = 1.0f;
        float emissive_b = 1.0f;
        float emissive_strength = cue->emissive_strength;
        
        int n = sscanf_s(pipe2 + 1,
            "%d|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%d|%d|%f|%f|%f|%f|%f|%f|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%f|%d|%d|%d|%d|%f|%f|%f|%f",
            &cue->mesh_type,
            &cue->pos[0],   &cue->pos[1],   &cue->pos[2],
            &cue->rot[0],   &cue->rot[1],   &cue->rot[2],
            &cue->scale[0], &cue->scale[1], &cue->scale[2],
            &cue->color[0], &cue->color[1], &cue->color[2], &cue->color[3],
            &cue->mesh_size, &cue->mesh_param,
            &cue->cue_start, &cue->cue_end,
            &cue->layer_order, &cue->effect_type,
            &cue->fade_in_start, &cue->fade_in_end,
            &cue->fade_out_start, &cue->fade_out_end,
            &cue->metallic, &cue->roughness,
            &curve_pos_x, &curve_pos_y, &curve_pos_z,
            &curve_rot_x, &curve_rot_y, &curve_rot_z,
            &curve_scale_x, &curve_scale_y, &curve_scale_z,
            &curve_color_r, &curve_color_g, &curve_color_b, &curve_color_a,
            &curve_mesh_size, &curve_metallic, &curve_roughness,
            &fov_deg, &cull_mode, &curve_fov,
            &use_imported_light, &use_imported_camera,
            &emissive_r, &emissive_g, &emissive_b, &emissive_strength);
        
        if (n >= 18) {   // minimum viable parse (first 18 fields after key+path)
            // Curve fields (backwards compatible - default to -1)
            cue->curve_pos_x = (n >= 27) ? curve_pos_x : -1;
            cue->curve_pos_y = (n >= 28) ? curve_pos_y : -1;
            cue->curve_pos_z = (n >= 29) ? curve_pos_z : -1;
            cue->curve_rot_x = (n >= 30) ? curve_rot_x : -1;
            cue->curve_rot_y = (n >= 31) ? curve_rot_y : -1;
            cue->curve_rot_z = (n >= 32) ? curve_rot_z : -1;
            cue->curve_scale_x = (n >= 33) ? curve_scale_x : -1;
            cue->curve_scale_y = (n >= 34) ? curve_scale_y : -1;
            cue->curve_scale_z = (n >= 35) ? curve_scale_z : -1;
            cue->curve_color_r = (n >= 36) ? curve_color_r : -1;
            cue->curve_color_g = (n >= 37) ? curve_color_g : -1;
            cue->curve_color_b = (n >= 38) ? curve_color_b : -1;
            cue->curve_color_a = (n >= 39) ? curve_color_a : -1;
            cue->curve_mesh_size = (n >= 40) ? curve_mesh_size : -1;
            cue->curve_metallic = (n >= 41) ? curve_metallic : -1;
            cue->curve_roughness = (n >= 42) ? curve_roughness : -1;
            cue->fov_deg = (n >= 43 && fov_deg > 0.0f) ? fov_deg : 45.0f;
            cue->cull_mode = (n >= 44 && cull_mode >= 0 && cull_mode <= 2) ? cull_mode : 0;
            cue->curve_fov = (n >= 45) ? curve_fov : -1;
            cue->use_imported_light = (n >= 46) ? use_imported_light : 0;
            cue->use_imported_camera = (n >= 47) ? use_imported_camera : 0;
            cue->emissive_color[0] = (n >= 48) ? emissive_r : 1.0f;
            cue->emissive_color[1] = (n >= 49) ? emissive_g : 1.0f;
            cue->emissive_color[2] = (n >= 50) ? emissive_b : 1.0f;
            cue->emissive_strength = (n >= 51) ? emissive_strength : 0.0f;
            found = true;
            break;
        }
    }

    fclose(f);
    return found;
}

int LoadCurves(const char* cues_path, curve::Curve* curves, int max_curves)
{
    FILE* f = nullptr;
    fopen_s(&f, cues_path, "r");
    if (!f) return 0;

    char line[2048];
    bool in_section = false;
    int curve_count = 0;
    int current_curve_idx = -1;

    while (fgets(line, sizeof(line), f) && curve_count < max_curves) {
        char* s = line;
        
        if (strstr(s, "[curves]")) { 
            in_section = true;  
            continue; 
        }
        if (s[0] == '[' && in_section) {
            break;
        }
        
        // Check indentation BEFORE trimming
        bool is_indented = (s[0] == ' ' || s[0] == '\t');
        TrimLeft(s);
        
        if (!in_section || s[0] == '#' || s[0] == '\0' || s[0] == '\n') continue;

        // Check if this is a curve header line (curve_id|wrap_mode|duration|point_count)
        if (!is_indented) {
            int curve_id, point_count;
            char wrap_mode[32];
            float duration;
            
            if (sscanf_s(s, "%d|%31[^|]|%f|%d", 
                &curve_id, wrap_mode, (unsigned)sizeof(wrap_mode), &duration, &point_count) == 4) {
                
                if (curve_id >= 0 && curve_id < max_curves) {
                    current_curve_idx = curve_id;
                    if (current_curve_idx >= curve_count) {
                        curve_count = current_curve_idx + 1;
                    }
                    
                    curves[current_curve_idx] = curve::CreateCurve(point_count);
                    curves[current_curve_idx].duration = duration;
                    
                    // Parse wrap mode
                    if (strcmp(wrap_mode, "loop") == 0) {
                        curves[current_curve_idx].wrap_mode = curve::WrapMode::Loop;
                    } else if (strcmp(wrap_mode, "pingpong") == 0) {
                        curves[current_curve_idx].wrap_mode = curve::WrapMode::PingPong;
                    } else if (strcmp(wrap_mode, "mirror") == 0) {
                        curves[current_curve_idx].wrap_mode = curve::WrapMode::Mirror;
                    } else {
                        curves[current_curve_idx].wrap_mode = curve::WrapMode::Clamp;
                    }
                }
            }
        }
        // Check if this is a point line (starts with spaces: t|v|in_ease|out_ease|mode)
        else if (is_indented && current_curve_idx >= 0 && current_curve_idx < max_curves) {
            float t, v, in_ease, out_ease;
            char mode[32];
            
            if (sscanf_s(s, "%f|%f|%f|%f|%31s", 
                &t, &v, &in_ease, &out_ease, mode, (unsigned)sizeof(mode)) == 5) {
                
                curve::EaseMode ease_mode = curve::EaseMode::Linear;
                if (strcmp(mode, "ease_in") == 0) ease_mode = curve::EaseMode::EaseIn;
                else if (strcmp(mode, "ease_out") == 0) ease_mode = curve::EaseMode::EaseOut;
                else if (strcmp(mode, "ease_in_out") == 0) ease_mode = curve::EaseMode::EaseInOut;
                else if (strcmp(mode, "smoothstep") == 0) ease_mode = curve::EaseMode::Smoothstep;
                else if (strcmp(mode, "hold") == 0) ease_mode = curve::EaseMode::Hold;
                
                curve::AddPoint(curves[current_curve_idx], t, v, ease_mode);
                curves[current_curve_idx].points[curves[current_curve_idx].point_count - 1].in_ease = in_ease;
                curves[current_curve_idx].points[curves[current_curve_idx].point_count - 1].out_ease = out_ease;
            }
        }
    }

    fclose(f);
    return curve_count;
}

// ---- Mat4 math -----------------------------------------------------

static const float kPi = 3.14159265358979323846f;

void Mat4Identity(float* m) {
    for (int i = 0; i < 16; ++i) m[i] = 0.0f;
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

void Mat4Perspective(float* m, float fov_rad, float aspect, float znear, float zfar) {
    float f = 1.0f / tanf(fov_rad * 0.5f);
    Mat4Identity(m);
    m[0]  =  f / aspect;
    m[5]  =  f;
    m[10] =  (zfar + znear) / (znear - zfar);
    m[11] = -1.0f;
    m[14] =  (2.0f * zfar * znear) / (znear - zfar);
    m[15] =  0.0f;
}

void Mat4LookAt(float* m, const float eye[3], const float center[3], const float up[3]) {
    float fx = center[0]-eye[0], fy = center[1]-eye[1], fz = center[2]-eye[2];
    float len = sqrtf(fx*fx + fy*fy + fz*fz);
    if (len < 1e-6f) { Mat4Identity(m); return; }
    fx /= len; fy /= len; fz /= len;

    float sx = fy*up[2] - fz*up[1];
    float sy = fz*up[0] - fx*up[2];
    float sz = fx*up[1] - fy*up[0];
    len = sqrtf(sx*sx + sy*sy + sz*sz);
    if (len < 1e-6f) { Mat4Identity(m); return; }
    sx /= len; sy /= len; sz /= len;

    float ux = sy*fz - sz*fy;
    float uy = sz*fx - sx*fz;
    float uz = sx*fy - sy*fx;

    Mat4Identity(m);
    m[0] = sx; m[4] = ux; m[8]  = -fx;
    m[1] = sy; m[5] = uy; m[9]  = -fy;
    m[2] = sz; m[6] = uz; m[10] = -fz;
    m[12] = -(sx*eye[0] + sy*eye[1] + sz*eye[2]);
    m[13] = -(ux*eye[0] + uy*eye[1] + uz*eye[2]);
    m[14] =   fx*eye[0] + fy*eye[1] + fz*eye[2];
}

void Mat4Translate(float* m, float x, float y, float z) {
    Mat4Identity(m);
    m[12] = x; m[13] = y; m[14] = z;
}

void Mat4RotateEuler(float* m, float rx_deg, float ry_deg, float rz_deg) {
    float rx = rx_deg * kPi / 180.0f;
    float ry = ry_deg * kPi / 180.0f;
    float rz = rz_deg * kPi / 180.0f;

    float cx = cosf(rx), sx = sinf(rx);
    float cy = cosf(ry), sy = sinf(ry);
    float cz = cosf(rz), sz = sinf(rz);

    // Rx
    float Rx[16]; Mat4Identity(Rx);
    Rx[5] = cx; Rx[9] = -sx; Rx[6] = sx; Rx[10] = cx;

    // Ry
    float Ry[16]; Mat4Identity(Ry);
    Ry[0] = cy; Ry[8] = sy; Ry[2] = -sy; Ry[10] = cy;

    // Rz
    float Rz[16]; Mat4Identity(Rz);
    Rz[0] = cz; Rz[4] = -sz; Rz[1] = sz; Rz[5] = cz;

    float tmp[16];
    Mat4Multiply(tmp, Ry, Rx);
    Mat4Multiply(m, Rz, tmp);
}

void Mat4Scale(float* m, float sx, float sy, float sz) {
    Mat4Identity(m);
    m[0] = sx; m[5] = sy; m[10] = sz;
}

void Mat4Multiply(float* out, const float* a, const float* b) {
    float tmp[16];
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            tmp[col*4 + row] =
                a[0*4 + row] * b[col*4 + 0] +
                a[1*4 + row] * b[col*4 + 1] +
                a[2*4 + row] * b[col*4 + 2] +
                a[3*4 + row] * b[col*4 + 3];
        }
    }
    for (int i = 0; i < 16; ++i) out[i] = tmp[i];
}

void Mat4Model(float* out, const float pos[3], const float rot_deg[3], const float scale[3]) {
    float T[16], R[16], S[16], tmp[16];
    Mat4Translate(T, pos[0], pos[1], pos[2]);
    Mat4RotateEuler(R, rot_deg[0], rot_deg[1], rot_deg[2]);
    Mat4Scale(S, scale[0], scale[1], scale[2]);
    Mat4Multiply(tmp, R, S);
    Mat4Multiply(out, T, tmp);
}

} // namespace runtime
} // namespace rev