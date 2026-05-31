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

// ============================================================
// LoadMeshCue + Mat4 math
// ============================================================
#include <cmath>

namespace rev {
namespace runtime {

// ---- LoadMeshCue ---------------------------------------------------
// Format: asset_key|mesh_type|pos_x|pos_y|pos_z|rot_x|rot_y|rot_z|
//         scale_x|scale_y|scale_z|color_r|color_g|color_b|color_a|
//         mesh_size|mesh_param|cue_start|cue_end|layer_order|
//         effect_type|fade_in_start|fade_in_end|fade_out_start|fade_out_end
bool LoadMeshCue(const char* cues_path, MeshCue* cue)
{
    FILE* f = nullptr;
    fopen_s(&f, cues_path, "r");
    if (!f) return false;

    char line[2048];
    bool in_section = false;
    bool found      = false;

    while (fgets(line, sizeof(line), f)) {
        char* s = line; TrimLeft(s);
        if (strstr(s, "[mesh_cues]")) { in_section = true;  continue; }
        if (s[0] == '[' && in_section) {                    break;    }
        if (!in_section || s[0] == '#' || s[0] == '\0' || s[0] == '\n') continue;

        // Split off asset_key (first pipe)
        char* pipe1 = strchr(s, '|');
        if (!pipe1) continue;
        size_t key_len = (size_t)(pipe1 - s);
        if (key_len >= sizeof(cue->asset_key)) key_len = sizeof(cue->asset_key) - 1;
        strncpy_s(cue->asset_key, s, key_len);
        cue->asset_key[key_len] = '\0';

        // Parse remaining 24 numeric fields
        int n = sscanf_s(pipe1 + 1,
            "%d|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%d|%d|%f|%f|%f|%f",
            &cue->mesh_type,
            &cue->pos[0],   &cue->pos[1],   &cue->pos[2],
            &cue->rot[0],   &cue->rot[1],   &cue->rot[2],
            &cue->scale[0], &cue->scale[1], &cue->scale[2],
            &cue->color[0], &cue->color[1], &cue->color[2], &cue->color[3],
            &cue->mesh_size, &cue->mesh_param,
            &cue->cue_start, &cue->cue_end,
            &cue->layer_order, &cue->effect_type,
            &cue->fade_in_start, &cue->fade_in_end,
            &cue->fade_out_start, &cue->fade_out_end);
        if (n >= 18) {   // minimum viable parse (first 18 fields after key)
            found = true;
            break;
        }
    }

    fclose(f);
    return found;
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