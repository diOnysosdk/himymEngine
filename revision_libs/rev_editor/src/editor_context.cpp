#include "rev_editor.h"
#include "rev_shader.h"
#include "rev_pack.h"
#include "rev_mesh.h"
#include "rev_gltf.h"
#include <cstring>
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
#include <windows.h>
#include <gl/gl.h>
#include <gdiplus.h>

#pragma comment(lib, "gdiplus.lib")

// OpenGL constants not in gl.h
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_CONSTANT_ALPHA
#define GL_CONSTANT_ALPHA 0x8003
#endif
#ifndef GL_ONE_MINUS_CONSTANT_ALPHA
#define GL_ONE_MINUS_CONSTANT_ALPHA 0x8004
#endif

// NOTE: This file requires Dear ImGui to be fully functional
// See revision_libs/rev_editor/README.md for setup instructions

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_opengl3.h"

#include "editor_internal.h"

// Forward declare the ImGui Win32 handler
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Message callback for rev_platform
static long long ImGuiMessageCallback(void* hwnd, unsigned int msg, unsigned long long wparam, long long lparam) {
    return ImGui_ImplWin32_WndProcHandler((HWND)hwnd, msg, (WPARAM)wparam, (LPARAM)lparam);
}

namespace rev {
namespace editor {

// Helper: get file modification time as uint64 (Windows FILETIME)
uint64_t GetFileModificationTime(const char* path) {
    WIN32_FILE_ATTRIBUTE_DATA fileInfo;
    if (GetFileAttributesExA(path, GetFileExInfoStandard, &fileInfo)) {
        ULARGE_INTEGER ull;
        ull.LowPart = fileInfo.ftLastWriteTime.dwLowDateTime;
        ull.HighPart = fileInfo.ftLastWriteTime.dwHighDateTime;
        return ull.QuadPart;
    }
    return 0;
}

static void JsonEscapeString(const char* src, char* dst, size_t dst_size) {
    if (!src || !dst || dst_size == 0) return;
    size_t di = 0;
    for (size_t si = 0; src[si] != '\0' && di + 1 < dst_size; ++si) {
        char c = src[si];
        if (c == '"' || c == '\\') {
            if (di + 2 >= dst_size) break;
            dst[di++] = '\\';
            dst[di++] = c;
        } else if (c == '\n') {
            if (di + 2 >= dst_size) break;
            dst[di++] = '\\';
            dst[di++] = 'n';
        } else if (c == '\r') {
            if (di + 2 >= dst_size) break;
            dst[di++] = '\\';
            dst[di++] = 'r';
        } else if (c == '\t') {
            if (di + 2 >= dst_size) break;
            dst[di++] = '\\';
            dst[di++] = 't';
        } else {
            dst[di++] = c;
        }
    }
    dst[di] = '\0';
}

static void ApplySpriteBlendMode(int blend_mode) {
    switch (blend_mode) {
        case 1: // Additive
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            break;
        case 2: // Multiply
            glBlendFunc(GL_DST_COLOR, GL_ZERO);
            break;
        case 3: // Screen
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_COLOR);
            break;
        case 0:
        default: // Alpha
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            break;
    }
}

static void ApplyShaderLayerBlendMode(int blend_mode, float opacity) {
    typedef void (*PFNGLBLENDCOLORPROC)(float red, float green, float blue, float alpha);
    static PFNGLBLENDCOLORPROC glBlendColor_fn = nullptr;
    static bool blend_color_loaded = false;
    if (!blend_color_loaded) {
        glBlendColor_fn = (PFNGLBLENDCOLORPROC)wglGetProcAddress("glBlendColor");
        blend_color_loaded = true;
    }

    float a = opacity;
    if (a < 0.0f) a = 0.0f;
    if (a > 1.0f) a = 1.0f;

    bool has_blend_color = (glBlendColor_fn != nullptr);
    if (has_blend_color) {
        glBlendColor_fn(0.0f, 0.0f, 0.0f, a);
    }

    switch (blend_mode) {
        case 1: // Additive
            glBlendFunc(has_blend_color ? GL_CONSTANT_ALPHA : GL_SRC_ALPHA, GL_ONE);
            break;
        case 2: // Multiply
            glBlendFunc(GL_DST_COLOR, GL_ZERO);
            break;
        case 3: // Screen
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_COLOR);
            break;
        case 0:
        default: // Alpha
            glBlendFunc(has_blend_color ? GL_CONSTANT_ALPHA : GL_SRC_ALPHA,
                        has_blend_color ? GL_ONE_MINUS_CONSTANT_ALPHA : GL_ONE_MINUS_SRC_ALPHA);
            break;
    }
}

static float Clamp01(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

static float ComputeShaderCueEnvelope(float local_time, float cue_duration, float fade_in, float fade_out) {
    float env = 1.0f;

    if (fade_in > 0.0001f) {
        env *= Clamp01(local_time / fade_in);
    }

    if (fade_out > 0.0001f && cue_duration > 0.0001f) {
        float time_to_end = cue_duration - local_time;
        env *= Clamp01(time_to_end / fade_out);
    }

    return env;
}

static void JsonUnescapeString(const char* src, char* dst, size_t dst_size) {
    if (!src || !dst || dst_size == 0) return;
    size_t di = 0;
    for (size_t si = 0; src[si] != '\0' && di + 1 < dst_size; ++si) {
        if (src[si] == '\\' && src[si + 1] != '\0') {
            ++si;
            if (src[si] == 'n') dst[di++] = '\n';
            else if (src[si] == 'r') dst[di++] = '\r';
            else if (src[si] == 't') dst[di++] = '\t';
            else dst[di++] = src[si];
        } else {
            dst[di++] = src[si];
        }
    }
    dst[di] = '\0';
}

static bool ParseJsonStringValue(const char* line, char* out, size_t out_size) {
    if (!line || !out || out_size == 0) return false;

    const char* colon = strchr(line, ':');
    if (!colon) return false;

    const char* p = colon + 1;
    while (*p == ' ' || *p == '\t') ++p;
    if (*p != '"') return false;
    ++p;

    char escaped[1024] = {};
    size_t ei = 0;
    while (*p && ei + 1 < sizeof(escaped)) {
        if (*p == '"' && (p == line || *(p - 1) != '\\')) break;
        escaped[ei++] = *p++;
    }
    escaped[ei] = '\0';

    JsonUnescapeString(escaped, out, out_size);
    return true;
}

static bool GetPngEncoderClsid(CLSID* clsid) {
    if (!clsid) return false;
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return false;

    BYTE* image_codec_memory = new BYTE[size];
    Gdiplus::ImageCodecInfo* image_codecs = (Gdiplus::ImageCodecInfo*)image_codec_memory;
    if (Gdiplus::GetImageEncoders(num, size, image_codecs) != Gdiplus::Ok) {
        delete[] image_codec_memory;
        return false;
    }

    bool found = false;
    for (UINT i = 0; i < num; ++i) {
        if (wcscmp(image_codecs[i].MimeType, L"image/png") == 0) {
            *clsid = image_codecs[i].Clsid;
            found = true;
            break;
        }
    }

    delete[] image_codec_memory;
    return found;
}

struct FontEnumData {
    char** fonts;
    int count;
    int capacity;
};

static int CALLBACK EnumFontFamExProc(const LOGFONTA* lpelfe, const TEXTMETRICA*, DWORD, LPARAM lParam) {
    FontEnumData* data = (FontEnumData*)lParam;
    if (!data || !lpelfe) return 1;

    const char* face = lpelfe->lfFaceName;
    if (!face || face[0] == '\0') return 1;

    for (int i = 0; i < data->count; ++i) {
        if (strcmp(data->fonts[i], face) == 0) {
            return 1;
        }
    }

    if (data->count >= data->capacity) {
        int new_capacity = (data->capacity > 0) ? (data->capacity * 2) : 64;
        char** new_fonts = new char*[new_capacity];
        for (int i = 0; i < data->count; ++i) {
            new_fonts[i] = data->fonts[i];
        }
        delete[] data->fonts;
        data->fonts = new_fonts;
        data->capacity = new_capacity;
    }

    size_t len = strlen(face);
    char* copy = new char[len + 1];
    strcpy_s(copy, len + 1, face);
    data->fonts[data->count++] = copy;
    return 1;
}

static void EnumerateInstalledFonts(char*** out_fonts, int* out_count) {
    if (!out_fonts || !out_count) return;
    *out_fonts = nullptr;
    *out_count = 0;

    HDC hdc = GetDC(nullptr);
    if (!hdc) {
        return;
    }

    FontEnumData data = {};
    LOGFONTA lf = {};
    lf.lfCharSet = DEFAULT_CHARSET;

    EnumFontFamiliesExA(hdc, &lf, (FONTENUMPROCA)EnumFontFamExProc, (LPARAM)&data, 0);
    ReleaseDC(nullptr, hdc);

    if (data.count == 0) {
        data.fonts = new char*[1];
        data.fonts[0] = new char[6];
        strcpy_s(data.fonts[0], 6, "Arial");
        data.count = 1;
        data.capacity = 1;
    }

    *out_fonts = data.fonts;
    *out_count = data.count;
}

static bool BakeTextCueToPng(const TextCue* cue, const char* output_path) {
    if (!cue || !output_path || cue->text[0] == '\0' || cue->font_name[0] == '\0') return false;

    wchar_t wtext[256] = {};
    wchar_t wfont[64] = {};
    wchar_t wpath[512] = {};
    MultiByteToWideChar(CP_UTF8, 0, cue->text, -1, wtext, (int)_countof(wtext));
    MultiByteToWideChar(CP_UTF8, 0, cue->font_name, -1, wfont, (int)_countof(wfont));
    MultiByteToWideChar(CP_UTF8, 0, output_path, -1, wpath, (int)_countof(wpath));

    Gdiplus::Bitmap temp_bitmap(1, 1, PixelFormat32bppARGB);
    Gdiplus::Graphics temp_g(&temp_bitmap);
    Gdiplus::Font font_obj(wfont, (Gdiplus::REAL)cue->size,
                           Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentNear);
    format.SetLineAlignment(Gdiplus::StringAlignmentNear);
    // Do NOT set StringFormatFlagsNoWrap — it suppresses embedded \n line breaks.
    format.SetTrimming(Gdiplus::StringTrimmingNone);

    Gdiplus::RectF layout(0.0f, 0.0f, 8192.0f, 8192.0f);
    Gdiplus::RectF bounds;
    temp_g.MeasureString(wtext, -1, &font_obj, layout, &format, &bounds);

    int width  = (int)(bounds.Width)  + 32;
    int height = (int)(bounds.Height) + 32;
    if (width  < 8) width  = 8;
    if (height < 8) height = 8;

    Gdiplus::Bitmap bitmap(width, height, PixelFormat32bppARGB);
    Gdiplus::Graphics gfx(&bitmap);
    gfx.Clear(Gdiplus::Color(0, 0, 0, 0));
    gfx.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
    gfx.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    gfx.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
    gfx.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    Gdiplus::SolidBrush brush(Gdiplus::Color(
        255,
        (BYTE)(cue->color.r * 255.0f),
        (BYTE)(cue->color.g * 255.0f),
        (BYTE)(cue->color.b * 255.0f)));

    Gdiplus::RectF draw_rect(8.0f, 8.0f, (float)width - 16.0f, (float)height - 16.0f);
    gfx.DrawString(wtext, -1, &font_obj, draw_rect, &format, &brush);

    CLSID png_clsid = {};
    if (!GetPngEncoderClsid(&png_clsid)) return false;
    return bitmap.Save(wpath, &png_clsid, nullptr) == Gdiplus::Ok;
}

static bool BakeScrollTextCueToPng(const ScrollTextCue* cue, const char* output_path) {
    if (!cue || !output_path || cue->text[0] == '\0' || cue->font_name[0] == '\0') return false;

    char bake_text[1024] = {};
    if (cue->direction == 0 || cue->direction == 1) {
        snprintf(bake_text, sizeof(bake_text), "%s   %s", cue->text, cue->text);
    } else {
        strncpy_s(bake_text, sizeof(bake_text), cue->text, _TRUNCATE);
    }

    wchar_t wtext[1024] = {};
    wchar_t wfont[64] = {};
    wchar_t wpath[512] = {};
    MultiByteToWideChar(CP_UTF8, 0, bake_text, -1, wtext, (int)_countof(wtext));
    MultiByteToWideChar(CP_UTF8, 0, cue->font_name, -1, wfont, (int)_countof(wfont));
    MultiByteToWideChar(CP_UTF8, 0, output_path, -1, wpath, (int)_countof(wpath));

    float bake_size = cue->size;
    if (bake_size < 4.0f) bake_size = 4.0f;

    Gdiplus::Bitmap temp_bitmap(1, 1, PixelFormat32bppARGB);
    Gdiplus::Graphics temp_g(&temp_bitmap);
    Gdiplus::Font font_obj(wfont, (Gdiplus::REAL)bake_size,
                           Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentNear);
    format.SetLineAlignment(Gdiplus::StringAlignmentNear);
    format.SetTrimming(Gdiplus::StringTrimmingNone);

    Gdiplus::RectF layout(0.0f, 0.0f, 16384.0f, 4096.0f);
    Gdiplus::RectF bounds;
    temp_g.MeasureString(wtext, -1, &font_obj, layout, &format, &bounds);

    int width  = (int)(bounds.Width * ((cue->spacing > 0.01f) ? cue->spacing : 0.01f)) + 32;
    int height = (int)(bounds.Height) + 32;
    if (width < 8) width = 8;
    if (height < 8) height = 8;

    Gdiplus::Bitmap bitmap(width, height, PixelFormat32bppARGB);
    Gdiplus::Graphics gfx(&bitmap);
    gfx.Clear(Gdiplus::Color(0, 0, 0, 0));
    gfx.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
    gfx.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    gfx.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
    gfx.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    Gdiplus::SolidBrush brush(Gdiplus::Color(
        255,
        (BYTE)(cue->color.r * 255.0f),
        (BYTE)(cue->color.g * 255.0f),
        (BYTE)(cue->color.b * 255.0f)));

    Gdiplus::RectF draw_rect(8.0f, 8.0f, (float)width - 16.0f, (float)height - 16.0f);
    gfx.DrawString(wtext, -1, &font_obj, draw_rect, &format, &brush);

    CLSID png_clsid = {};
    if (!GetPngEncoderClsid(&png_clsid)) return false;
    return bitmap.Save(wpath, &png_clsid, nullptr) == Gdiplus::Ok;
}

static bool IsFileReadableWithRetry(const char* path, int max_attempts, int sleep_ms) {
    if (!path || path[0] == '\0') return false;
    if (max_attempts < 1) max_attempts = 1;
    if (sleep_ms < 0) sleep_ms = 0;

    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        FILE* f = nullptr;
        fopen_s(&f, path, "rb");
        if (f) {
            fclose(f);
            return true;
        }
        if (attempt + 1 < max_attempts && sleep_ms > 0) {
            Sleep((DWORD)sleep_ms);
        }
    }
    return false;
}

EditorContext* CreateEditor(rev::platform::Window* window) {
    EditorContext* editor = new EditorContext();
    editor->window = window;
    editor->imgui_context = nullptr;
    editor->project = nullptr;
    editor->show_timeline = true;
    editor->show_curve_editor = true;
    editor->show_properties = true;
    editor->show_asset_browser = false;
    editor->show_demo = false;
    editor->timeline_zoom = 1.0f;
    editor->timeline_scroll = 0.0f;
    editor->selected_scene_index = -1;
    editor->selected_cue_index = -1;
    editor->selected_cue_type = CueTypeShader;
    editor->current_time = 0.0f;
    editor->playing = false;
    editor->show_preview = true;
    editor->preview_fbo = 0;
    editor->preview_texture = 0;
    editor->preview_depth = 0;
    editor->preview_vao = 0;
    editor->preview_width = 1920;
    editor->preview_height = 1080;
    editor->preview_initialized = false;
    editor->preview_shader = nullptr;
    editor->sprite_shader = nullptr;
    editor->preview_current_shader_id = -1;
    editor->shader_modal_open = false;
    editor->shader_modal_request_open = false;
    editor->music_modal_open = false;
    editor->music_modal_request_open = false;
    editor->image_modal_open = false;
    editor->image_modal_request_open = false;
    editor->text_modal_open = false;
    editor->text_modal_request_open = false;
    editor->scroll_text_modal_open = false;
    editor->scroll_text_modal_request_open = false;
    memset(&editor->editing_scroll_text, 0, sizeof(editor->editing_scroll_text));
    strncpy_s(editor->editing_scroll_text.font_name, sizeof(editor->editing_scroll_text.font_name), "Arial", _TRUNCATE);
    editor->editing_scroll_text.size = 40.0f;
    editor->editing_scroll_text.color = {1.0f, 1.0f, 1.0f};
    editor->editing_scroll_text.opacity = 1.0f;
    editor->editing_scroll_text.speed = 0.25f;
    editor->editing_scroll_text.wrap_gap = 0.2f;
    editor->editing_scroll_text.spacing = 1.0f;
    editor->editing_scroll_text.wave_freq = 1.0f;
    editor->editing_scroll_text.jitter_freq = 1.0f;
    editor->editing_scroll_text.bake_mode = 0;
    editor->editing_scroll_text.curve_x = -1;
    editor->editing_scroll_text.curve_y = -1;
    editor->editing_scroll_text.curve_speed = -1;
    editor->editing_scroll_text.curve_size = -1;
    editor->editing_scroll_text.curve_opacity = -1;
    editor->editing_scroll_text.curve_color_r = -1;
    editor->editing_scroll_text.curve_color_g = -1;
    editor->editing_scroll_text.curve_color_b = -1;
    editor->editing_scroll_text.curve_wave_amp = -1;
    editor->editing_scroll_text.curve_wave_freq = -1;
    editor->editing_scroll_text.curve_jitter_amp = -1;
    editor->editing_scroll_text.curve_jitter_freq = -1;
    editor->installed_fonts = nullptr;
    editor->installed_font_count = 0;
    editor->mesh_modal_open = false;
    editor->mesh_modal_request_open = false;
    memset(&editor->editing_mesh, 0, sizeof(editor->editing_mesh));
    editor->editing_mesh.scale[0] = editor->editing_mesh.scale[1] = editor->editing_mesh.scale[2] = 1.0f;
    editor->mesh_cache_count = 0;
    memset(editor->mesh_cache, 0, sizeof(editor->mesh_cache));
    editor->mesh_shader = nullptr;
    editor->selected_curve_index = -1;
    editor->dragging_point_index = -1;
    editor->selected_point_index = -1;
    editor->show_curve_grid = true;
    editor->curve_editor_modal_open = false;
    editor->curve_editor_modal_request_open = false;
    editor->point_properties_modal_open = false;
    editor->editing_curve_index = -1;
    editor->editing_curve_cue_type = -1;
    editor->editing_curve_field = -1;
    editor->editing_curve_label[0] = '\0';
    editor->build_status_message[0] = '\0';
    editor->build_status_timer = 0.0f;

    // Capture startup working directory before any file dialog can mutate it
    GetCurrentDirectoryA(sizeof(editor->startup_dir), editor->startup_dir);

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    
    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(window->hwnd);
    ImGui_ImplOpenGL3_Init("#version 330");
    
    // Register message callback for ImGui input handling
    rev::platform::SetMessageCallback(window, ImGuiMessageCallback);
    
    // Style
    ImGui::StyleColorsDark();
    
    // Create empty project
    editor->project = new ProjectData();
    editor->project->scenes = nullptr;
    editor->project->scene_count = 0;
    editor->project->scene_capacity = 0;
    
    // Allocate fixed curve array (max 32 curves)
    editor->project->curves = new rev::curve::Curve[32];
    for (int i = 0; i < 32; ++i) {
        editor->project->curves[i].points = nullptr;
        editor->project->curves[i].point_count = 0;
        editor->project->curves[i].capacity = 0;
    }
    editor->project->curve_count = 0;
    
    editor->project->modified = false;
    editor->project->total_duration = 0.0f;
    editor->project->loop_intro = false;
    editor->project->loop_music = false;
    memset(editor->project->project_path, 0, sizeof(editor->project->project_path));
    memset(editor->project->workspace_path, 0, sizeof(editor->project->workspace_path));
    memset(editor->project->assets_path, 0, sizeof(editor->project->assets_path));
    
    // Enumerate installed Windows fonts for font picker
    EnumerateInstalledFonts(&editor->installed_fonts, &editor->installed_font_count);
    printf("Enumerated %d installed fonts\n", editor->installed_font_count);
    
    return editor;
}

void DestroyEditor(EditorContext* editor) {
    if (!editor) return;
    
    // Unregister message callback
    rev::platform::SetMessageCallback(editor->window, nullptr);
    
    // Cleanup project
    if (editor->project) {
        // Clean up scenes
        for (int i = 0; i < editor->project->scene_count; ++i) {
            SceneBlock* scene = &editor->project->scenes[i];
            delete[] scene->shader_cues;
            delete[] scene->image_cues;
            delete[] scene->text_cues;
            delete[] scene->scroll_text_cues;
            delete[] scene->music_cues;
            delete[] scene->mesh_cues;
        }
        delete[] editor->project->scenes;
        
        // Clean up curves
        for (int i = 0; i < editor->project->curve_count; ++i) {
            rev::curve::DestroyCurve(editor->project->curves[i]);
        }
        delete[] editor->project->curves;
        
        delete editor->project;
    }
    
    // Cleanup preview
    CleanupPreview(editor);
    
    // Cleanup installed fonts list
    if (editor->installed_fonts) {
        for (int i = 0; i < editor->installed_font_count; ++i) {
            delete[] editor->installed_fonts[i];
        }
        delete[] editor->installed_fonts;
    }
    
    // Cleanup ImGui
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    
    delete editor;
}

bool LoadProject(EditorContext* editor, const char* path) {
    if (!editor || !path) return false;
    
    FILE* f = nullptr;
    fopen_s(&f, path, "r");
    if (!f) return false;
    
    // Clear existing project
    NewProject(editor);
    
    char line[1024];
    SceneBlock* current_scene = nullptr;
    bool in_scenes = false;
    bool in_shader_cues = false;
    bool in_image_cues = false;
    bool in_text_cues = false;
    bool in_scroll_text_cues = false;
    bool in_music_cues = false;
    bool in_mesh_cues = false;
    bool in_curves = false;
    bool in_curve_points = false;
    
    ShaderCue current_shader_cue = {};
    // Initialize shader curve fields to -1 (no curve)
    current_shader_cue.curve_speed = current_shader_cue.curve_intensity = current_shader_cue.curve_warp = -1;
    current_shader_cue.curve_exposure = current_shader_cue.curve_fade = -1;
    current_shader_cue.curve_palette_low_r = current_shader_cue.curve_palette_low_g = current_shader_cue.curve_palette_low_b = -1;
    current_shader_cue.curve_palette_mid_r = current_shader_cue.curve_palette_mid_g = current_shader_cue.curve_palette_mid_b = -1;
    current_shader_cue.curve_palette_high_r = current_shader_cue.curve_palette_high_g = current_shader_cue.curve_palette_high_b = -1;
    current_shader_cue.curve_opacity = current_shader_cue.curve_exposure_ramp = current_shader_cue.curve_fade_ramp = -1;
    
    ImageCue current_image_cue = {};
    TextCue current_text_cue = {};
    current_text_cue.curve_x = -1;
    current_text_cue.curve_y = -1;
    current_text_cue.curve_size = -1;
    current_text_cue.curve_color_r = -1;
    current_text_cue.curve_color_g = -1;
    current_text_cue.curve_color_b = -1;
    current_text_cue.blend_mode = 0;
    current_text_cue.bake_mode = 0;
    ScrollTextCue current_scroll_text_cue = {};
    strncpy_s(current_scroll_text_cue.font_name, sizeof(current_scroll_text_cue.font_name), "Arial", _TRUNCATE);
    current_scroll_text_cue.size = 40.0f;
    current_scroll_text_cue.color = {1.0f, 1.0f, 1.0f};
    current_scroll_text_cue.opacity = 1.0f;
    current_scroll_text_cue.wrap_gap = 0.2f;
    current_scroll_text_cue.speed = 0.25f;
    current_scroll_text_cue.spacing = 1.0f;
    current_scroll_text_cue.wave_freq = 1.0f;
    current_scroll_text_cue.jitter_freq = 1.0f;
    current_scroll_text_cue.bake_mode = 0;
    current_scroll_text_cue.curve_x = -1;
    current_scroll_text_cue.curve_y = -1;
    current_scroll_text_cue.curve_speed = -1;
    current_scroll_text_cue.curve_size = -1;
    current_scroll_text_cue.curve_opacity = -1;
    current_scroll_text_cue.curve_color_r = -1;
    current_scroll_text_cue.curve_color_g = -1;
    current_scroll_text_cue.curve_color_b = -1;
    current_scroll_text_cue.curve_wave_amp = -1;
    current_scroll_text_cue.curve_wave_freq = -1;
    current_scroll_text_cue.curve_jitter_amp = -1;
    current_scroll_text_cue.curve_jitter_freq = -1;
    MusicCue current_music_cue = {};
    MeshCue current_mesh_cue = {};
    current_mesh_cue.scale[0] = current_mesh_cue.scale[1] = current_mesh_cue.scale[2] = 1.0f;
    current_mesh_cue.roughness = 0.5f;
    current_mesh_cue.curve_pos_x = current_mesh_cue.curve_pos_y = current_mesh_cue.curve_pos_z = -1;
    current_mesh_cue.curve_rot_x = current_mesh_cue.curve_rot_y = current_mesh_cue.curve_rot_z = -1;
    current_mesh_cue.curve_scale_x = current_mesh_cue.curve_scale_y = current_mesh_cue.curve_scale_z = -1;
    current_mesh_cue.curve_color_r = current_mesh_cue.curve_color_g = current_mesh_cue.curve_color_b = current_mesh_cue.curve_color_a = -1;
    current_mesh_cue.curve_mesh_size = current_mesh_cue.curve_metallic = current_mesh_cue.curve_roughness = -1;
    rev::curve::Curve* current_curve = nullptr;
    
    char scene_name[64] = {};
    float scene_duration = 0.0f;
    bool has_name = false;
    bool has_duration = false;
    
    while (fgets(line, sizeof(line), f)) {
        // Trim whitespace
        char* start = line;
        while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') start++;

        int bool_value = 0;
        if (sscanf_s(start, "\"loop_intro\": %d", &bool_value) == 1) {
            editor->project->loop_intro = (bool_value != 0);
            continue;
        }
        if (sscanf_s(start, "\"loop_music\": %d", &bool_value) == 1) {
            editor->project->loop_music = (bool_value != 0);
            continue;
        }
        
        // Detect sections
        if (strstr(start, "\"scenes\":")) {
            in_scenes = true;
            continue;
        }
        if (strstr(start, "\"curves\":")) {
            in_scenes = false;
            in_curves = true;
            continue;
        }
        
        // Scene name and duration detection (inside scenes array)
        if (in_scenes && strstr(start, "\"name\":")) {
            if (ParseJsonStringValue(start, scene_name, sizeof(scene_name))) {
                has_name = true;
            }
        }
        if (in_scenes && strstr(start, "\"duration\":")) {
            if (sscanf_s(start, "\"duration\": %f", &scene_duration) == 1) {
                has_duration = true;
            }
        }
        
        // When we have both name and duration, create the scene
        if (in_scenes && has_name && has_duration) {
            int idx = AddScene(editor, scene_name, scene_duration);
            current_scene = GetScene(editor, idx);
            has_name = false;
            has_duration = false;
            in_shader_cues = false;
            in_image_cues = false;
            in_text_cues = false;
            in_scroll_text_cues = false;
            in_music_cues = false;
            in_mesh_cues = false;
        }
        
        // Section detection
        if (strstr(start, "\"shader_cues\":")) {
            in_shader_cues = true;
            in_image_cues = false;
            in_text_cues = false;
            in_scroll_text_cues = false;
            in_music_cues = false;
        } else if (strstr(start, "\"image_cues\":")) {
            in_shader_cues = false;
            in_image_cues = true;
            in_text_cues = false;
            in_scroll_text_cues = false;
            in_music_cues = false;
        } else if (strstr(start, "\"text_cues\":")) {
            in_shader_cues = false;
            in_image_cues = false;
            in_text_cues = true;
            in_scroll_text_cues = false;
            in_music_cues = false;
        } else if (strstr(start, "\"scroll_text_cues\":")) {
            in_shader_cues = false;
            in_image_cues = false;
            in_text_cues = false;
            in_scroll_text_cues = true;
            in_music_cues = false;
        } else if (strstr(start, "\"music_cues\":")) {
            in_shader_cues = false;
            in_image_cues = false;
            in_text_cues = false;
            in_scroll_text_cues = false;
            in_music_cues = true;
            in_mesh_cues = false;
        } else if (strstr(start, "\"mesh_cues\":")) {
            in_shader_cues = false;
            in_image_cues = false;
            in_text_cues = false;
            in_scroll_text_cues = false;
            in_music_cues = false;
            in_mesh_cues = true;
        } else if (strstr(start, "\"curves\":")) {
            in_curves = true;
        }
        
        // Parse shader cue fields
        if (in_shader_cues && current_scene) {
            if (strstr(start, "\"shader_name\":")) {
                ParseJsonStringValue(start, current_shader_cue.shader_name, sizeof(current_shader_cue.shader_name));
            } else if (strstr(start, "\"shader_scene_id\":")) {
                sscanf_s(start, "\"shader_scene_id\": %d", &current_shader_cue.shader_scene_id);
            } else if (strstr(start, "\"palette_low\":")) {
                sscanf_s(start, "\"palette_low\": [%f, %f, %f]",
                    &current_shader_cue.palette_low.r,
                    &current_shader_cue.palette_low.g,
                    &current_shader_cue.palette_low.b);
            } else if (strstr(start, "\"palette_mid\":")) {
                sscanf_s(start, "\"palette_mid\": [%f, %f, %f]",
                    &current_shader_cue.palette_mid.r,
                    &current_shader_cue.palette_mid.g,
                    &current_shader_cue.palette_mid.b);
            } else if (strstr(start, "\"palette_high\":")) {
                sscanf_s(start, "\"palette_high\": [%f, %f, %f]",
                    &current_shader_cue.palette_high.r,
                    &current_shader_cue.palette_high.g,
                    &current_shader_cue.palette_high.b);
            } else if (strstr(start, "\"speed\":")) {
                sscanf_s(start, "\"speed\": %f", &current_shader_cue.speed);
            } else if (strstr(start, "\"intensity\":")) {
                sscanf_s(start, "\"intensity\": %f", &current_shader_cue.intensity);
            } else if (strstr(start, "\"warp\":")) {
                sscanf_s(start, "\"warp\": %f", &current_shader_cue.warp);
            } else if (strstr(start, "\"exposure_base\":")) {
                sscanf_s(start, "\"exposure_base\": %f", &current_shader_cue.exposure_base);
            } else if (strstr(start, "\"exposure_ramp\":")) {
                sscanf_s(start, "\"exposure_ramp\": %f", &current_shader_cue.exposure_ramp);
            } else if (strstr(start, "\"fade_base\":")) {
                sscanf_s(start, "\"fade_base\": %f", &current_shader_cue.fade_base);
            } else if (strstr(start, "\"fade_ramp\":")) {
                sscanf_s(start, "\"fade_ramp\": %f", &current_shader_cue.fade_ramp);
            } else if (strstr(start, "\"cue_start\":")) {
                sscanf_s(start, "\"cue_start\": %f", &current_shader_cue.cue_start);
            } else if (strstr(start, "\"cue_end\":")) {
                sscanf_s(start, "\"cue_end\": %f", &current_shader_cue.cue_end);
            } else if (strstr(start, "\"fade_in\":")) {
                sscanf_s(start, "\"fade_in\": %f", &current_shader_cue.fade_in);
            } else if (strstr(start, "\"fade_out\":")) {
                sscanf_s(start, "\"fade_out\": %f", &current_shader_cue.fade_out);
            } else if (strstr(start, "\"layer_role\":")) {
                sscanf_s(start, "\"layer_role\": %d", &current_shader_cue.layer_role);
            } else if (strstr(start, "\"opacity\":")) {
                sscanf_s(start, "\"opacity\": %f", &current_shader_cue.opacity);
            } else if (strstr(start, "\"blend_mode\":")) {
                sscanf_s(start, "\"blend_mode\": %d", &current_shader_cue.blend_mode);
            } else if (strstr(start, "\"layer_order\":")) {
                sscanf_s(start, "\"layer_order\": %d", &current_shader_cue.layer_order);
            } else if (strstr(start, "\"curve_speed\":")) {
                sscanf_s(start, "\"curve_speed\": %d", &current_shader_cue.curve_speed);
            } else if (strstr(start, "\"curve_intensity\":")) {
                sscanf_s(start, "\"curve_intensity\": %d", &current_shader_cue.curve_intensity);
            } else if (strstr(start, "\"curve_warp\":")) {
                sscanf_s(start, "\"curve_warp\": %d", &current_shader_cue.curve_warp);
            } else if (strstr(start, "\"curve_exposure\":")) {
                sscanf_s(start, "\"curve_exposure\": %d", &current_shader_cue.curve_exposure);
            } else if (strstr(start, "\"curve_fade\":")) {
                sscanf_s(start, "\"curve_fade\": %d", &current_shader_cue.curve_fade);
            } else if (strstr(start, "\"curve_palette_low_r\":")) {
                sscanf_s(start, "\"curve_palette_low_r\": %d", &current_shader_cue.curve_palette_low_r);
            } else if (strstr(start, "\"curve_palette_low_g\":")) {
                sscanf_s(start, "\"curve_palette_low_g\": %d", &current_shader_cue.curve_palette_low_g);
            } else if (strstr(start, "\"curve_palette_low_b\":")) {
                sscanf_s(start, "\"curve_palette_low_b\": %d", &current_shader_cue.curve_palette_low_b);
            } else if (strstr(start, "\"curve_palette_mid_r\":")) {
                sscanf_s(start, "\"curve_palette_mid_r\": %d", &current_shader_cue.curve_palette_mid_r);
            } else if (strstr(start, "\"curve_palette_mid_g\":")) {
                sscanf_s(start, "\"curve_palette_mid_g\": %d", &current_shader_cue.curve_palette_mid_g);
            } else if (strstr(start, "\"curve_palette_mid_b\":")) {
                sscanf_s(start, "\"curve_palette_mid_b\": %d", &current_shader_cue.curve_palette_mid_b);
            } else if (strstr(start, "\"curve_palette_high_r\":")) {
                sscanf_s(start, "\"curve_palette_high_r\": %d", &current_shader_cue.curve_palette_high_r);
            } else if (strstr(start, "\"curve_palette_high_g\":")) {
                sscanf_s(start, "\"curve_palette_high_g\": %d", &current_shader_cue.curve_palette_high_g);
            } else if (strstr(start, "\"curve_palette_high_b\":")) {
                sscanf_s(start, "\"curve_palette_high_b\": %d", &current_shader_cue.curve_palette_high_b);
            } else if (strstr(start, "\"curve_opacity\":")) {
                sscanf_s(start, "\"curve_opacity\": %d", &current_shader_cue.curve_opacity);
            } else if (strstr(start, "\"curve_exposure_ramp\":")) {
                sscanf_s(start, "\"curve_exposure_ramp\": %d", &current_shader_cue.curve_exposure_ramp);
            } else if (strstr(start, "\"curve_fade_ramp\":")) {
                sscanf_s(start, "\"curve_fade_ramp\": %d", &current_shader_cue.curve_fade_ramp);
            } else if (start[0] == '}' && current_shader_cue.shader_name[0] != '\0') {
                // End of shader cue object - add it
                printf("[LoadProject] Loaded shader cue: name='%s' id=%d start=%.2f end=%.2f\n",
                       current_shader_cue.shader_name, current_shader_cue.shader_scene_id,
                       current_shader_cue.cue_start, current_shader_cue.cue_end);
                AddShaderCue(current_scene, current_shader_cue);
                memset(&current_shader_cue, 0, sizeof(current_shader_cue));
            }
        }
        
        // Parse image cue fields
        if (in_image_cues && current_scene) {
            if (strstr(start, "\"asset_key\":")) {
                ParseJsonStringValue(start, current_image_cue.asset_key, sizeof(current_image_cue.asset_key));
            } else if (strstr(start, "\"x\":")) {
                sscanf_s(start, "\"x\": %f", &current_image_cue.x);
            } else if (strstr(start, "\"y\":")) {
                sscanf_s(start, "\"y\": %f", &current_image_cue.y);
            } else if (strstr(start, "\"scale\":")) {
                sscanf_s(start, "\"scale\": %f", &current_image_cue.scale);
            } else if (strstr(start, "\"opacity\":")) {
                sscanf_s(start, "\"opacity\": %f", &current_image_cue.opacity);
            } else if (strstr(start, "\"effect_type\":")) {
                sscanf_s(start, "\"effect_type\": %d", &current_image_cue.effect_type);
            } else if (strstr(start, "\"cue_start\":")) {
                sscanf_s(start, "\"cue_start\": %f", &current_image_cue.cue_start);
            } else if (strstr(start, "\"cue_end\":")) {
                sscanf_s(start, "\"cue_end\": %f", &current_image_cue.cue_end);
            } else if (strstr(start, "\"effect_start\":")) {
                sscanf_s(start, "\"effect_start\": %f", &current_image_cue.fade_in_start);
            } else if (strstr(start, "\"effect_end\":")) {
                sscanf_s(start, "\"effect_end\": %f", &current_image_cue.fade_in_end);
            } else if (strstr(start, "\"fade_in_start\":")) {
                sscanf_s(start, "\"fade_in_start\": %f", &current_image_cue.fade_in_start);
            } else if (strstr(start, "\"fade_in_end\":")) {
                sscanf_s(start, "\"fade_in_end\": %f", &current_image_cue.fade_in_end);
            } else if (strstr(start, "\"fade_out_start\":")) {
                sscanf_s(start, "\"fade_out_start\": %f", &current_image_cue.fade_out_start);
            } else if (strstr(start, "\"fade_out_end\":")) {
                sscanf_s(start, "\"fade_out_end\": %f", &current_image_cue.fade_out_end);
            } else if (strstr(start, "\"blend_mode\":")) {
                sscanf_s(start, "\"blend_mode\": %d", &current_image_cue.blend_mode);
            } else if (strstr(start, "\"layer_order\":")) {
                sscanf_s(start, "\"layer_order\": %d", &current_image_cue.layer_order);
            } else if (strstr(start, "\"curve_x\":")) {
                sscanf_s(start, "\"curve_x\": %d", &current_image_cue.curve_x);
            } else if (strstr(start, "\"curve_y\":")) {
                sscanf_s(start, "\"curve_y\": %d", &current_image_cue.curve_y);
            } else if (strstr(start, "\"curve_scale\":")) {
                sscanf_s(start, "\"curve_scale\": %d", &current_image_cue.curve_scale);
            } else if (strstr(start, "\"curve_opacity\":")) {
                sscanf_s(start, "\"curve_opacity\": %d", &current_image_cue.curve_opacity);
            } else if (start[0] == '}' && current_image_cue.asset_key[0] != '\0') {
                // End of image cue object - add it
                // Initialize curve fields to -1 if not loaded (backwards compatibility)
                if (current_image_cue.curve_x == 0 && current_image_cue.curve_y == 0 && 
                    current_image_cue.curve_scale == 0 && current_image_cue.curve_opacity == 0) {
                    current_image_cue.curve_x = -1;
                    current_image_cue.curve_y = -1;
                    current_image_cue.curve_scale = -1;
                    current_image_cue.curve_opacity = -1;
                }
                printf("[LoadProject] Loaded image cue: %s pos=(%.2f,%.2f) scale=%.2f\n",
                       current_image_cue.asset_key, current_image_cue.x, current_image_cue.y, current_image_cue.scale);
                AddImageCue(current_scene, current_image_cue);
                memset(&current_image_cue, 0, sizeof(current_image_cue));
            }
        }

        // Parse text cue fields
        if (in_text_cues && current_scene) {
            if (strstr(start, "\"text\":")) {
                // Robust parse: handle both valid JSON escaped text and legacy multiline text
                // that may span multiple physical lines in old project files.
                char escaped_text[512] = {};
                size_t out_len = 0;
                char* colon = strchr(start, ':');
                if (colon) {
                    char* p = colon + 1;
                    while (*p == ' ' || *p == '\t') ++p;
                    if (*p == '"') {
                        ++p; // skip opening quote
                        bool done = false;
                        char continuation[1024];

                        while (!done && out_len + 1 < sizeof(escaped_text)) {
                            for (; *p && out_len + 1 < sizeof(escaped_text); ++p) {
                                // End at unescaped quote
                                if (*p == '"' && (p == start || *(p - 1) != '\\')) {
                                    done = true;
                                    break;
                                }
                                if (*p == '\r' || *p == '\n') continue;
                                escaped_text[out_len++] = *p;
                            }

                            if (done) break;

                            // Legacy recovery: text continued on next line in malformed JSON.
                            if (out_len + 1 < sizeof(escaped_text)) {
                                escaped_text[out_len++] = '\\';
                            }
                            if (out_len + 1 < sizeof(escaped_text)) {
                                escaped_text[out_len++] = 'n';
                            }

                            if (!fgets(continuation, sizeof(continuation), f)) break;
                            p = continuation;
                            while (*p == ' ' || *p == '\t') ++p;
                        }
                        escaped_text[out_len] = '\0';
                    }
                }

                if (escaped_text[0] != '\0') {
                    JsonUnescapeString(escaped_text, current_text_cue.text, sizeof(current_text_cue.text));
                }
            } else if (strstr(start, "\"font_name\":")) {
                ParseJsonStringValue(start, current_text_cue.font_name, sizeof(current_text_cue.font_name));
            } else if (strstr(start, "\"x\":")) {
                sscanf_s(start, "\"x\": %f", &current_text_cue.x);
            } else if (strstr(start, "\"y\":")) {
                sscanf_s(start, "\"y\": %f", &current_text_cue.y);
            } else if (strstr(start, "\"size\":")) {
                sscanf_s(start, "\"size\": %f", &current_text_cue.size);
            } else if (strstr(start, "\"color\":")) {
                sscanf_s(start, "\"color\": [%f, %f, %f]",
                    &current_text_cue.color.r, &current_text_cue.color.g, &current_text_cue.color.b);
            } else if (strstr(start, "\"effect_type\":")) {
                sscanf_s(start, "\"effect_type\": %d", &current_text_cue.effect_type);
            } else if (strstr(start, "\"cue_start\":")) {
                sscanf_s(start, "\"cue_start\": %f", &current_text_cue.cue_start);
            } else if (strstr(start, "\"cue_end\":")) {
                sscanf_s(start, "\"cue_end\": %f", &current_text_cue.cue_end);
            } else if (strstr(start, "\"effect_start\":")) {
                sscanf_s(start, "\"effect_start\": %f", &current_text_cue.fade_in_start);
            } else if (strstr(start, "\"effect_end\":")) {
                sscanf_s(start, "\"effect_end\": %f", &current_text_cue.fade_in_end);
            } else if (strstr(start, "\"fade_in_start\":")) {
                sscanf_s(start, "\"fade_in_start\": %f", &current_text_cue.fade_in_start);
            } else if (strstr(start, "\"fade_in_end\":")) {
                sscanf_s(start, "\"fade_in_end\": %f", &current_text_cue.fade_in_end);
            } else if (strstr(start, "\"fade_out_start\":")) {
                sscanf_s(start, "\"fade_out_start\": %f", &current_text_cue.fade_out_start);
            } else if (strstr(start, "\"fade_out_end\":")) {
                sscanf_s(start, "\"fade_out_end\": %f", &current_text_cue.fade_out_end);
            } else if (strstr(start, "\"blend_mode\":")) {
                sscanf_s(start, "\"blend_mode\": %d", &current_text_cue.blend_mode);
            } else if (strstr(start, "\"bake_mode\":")) {
                sscanf_s(start, "\"bake_mode\": %d", &current_text_cue.bake_mode);
            } else if (strstr(start, "\"curve_x\":")) {
                sscanf_s(start, "\"curve_x\": %d", &current_text_cue.curve_x);
            } else if (strstr(start, "\"curve_y\":")) {
                sscanf_s(start, "\"curve_y\": %d", &current_text_cue.curve_y);
            } else if (strstr(start, "\"curve_size\":")) {
                sscanf_s(start, "\"curve_size\": %d", &current_text_cue.curve_size);
            } else if (strstr(start, "\"curve_color_r\":")) {
                sscanf_s(start, "\"curve_color_r\": %d", &current_text_cue.curve_color_r);
            } else if (strstr(start, "\"curve_color_g\":")) {
                sscanf_s(start, "\"curve_color_g\": %d", &current_text_cue.curve_color_g);
            } else if (strstr(start, "\"curve_color_b\":")) {
                sscanf_s(start, "\"curve_color_b\": %d", &current_text_cue.curve_color_b);
            } else if (strstr(start, "\"baked_asset_key\":")) {
                ParseJsonStringValue(start, current_text_cue.baked_asset_key, sizeof(current_text_cue.baked_asset_key));
            } else if (strstr(start, "\"baked_asset_path\":")) {
                ParseJsonStringValue(start, current_text_cue.baked_asset_path, sizeof(current_text_cue.baked_asset_path));
            } else if (strstr(start, "\"layer_order\":")) {
                sscanf_s(start, "\"layer_order\": %d", &current_text_cue.layer_order);
            } else if (start[0] == '}' && current_text_cue.text[0] != '\0') {
                AddTextCue(current_scene, current_text_cue);
                memset(&current_text_cue, 0, sizeof(current_text_cue));
                current_text_cue.curve_x = -1;
                current_text_cue.curve_y = -1;
                current_text_cue.curve_size = -1;
                current_text_cue.curve_color_r = -1;
                current_text_cue.curve_color_g = -1;
                current_text_cue.curve_color_b = -1;
                current_text_cue.blend_mode = 0;
                current_text_cue.bake_mode = 0;
            }
        }

        // Parse scroll text cue fields
        if (in_scroll_text_cues && current_scene) {
            if (strstr(start, "\"text\":")) {
                ParseJsonStringValue(start, current_scroll_text_cue.text, sizeof(current_scroll_text_cue.text));
            } else if (strstr(start, "\"font_name\":")) {
                ParseJsonStringValue(start, current_scroll_text_cue.font_name, sizeof(current_scroll_text_cue.font_name));
            } else if (strstr(start, "\"x\":")) {
                sscanf_s(start, "\"x\": %f", &current_scroll_text_cue.x);
            } else if (strstr(start, "\"y\":")) {
                sscanf_s(start, "\"y\": %f", &current_scroll_text_cue.y);
            } else if (strstr(start, "\"size\":")) {
                sscanf_s(start, "\"size\": %f", &current_scroll_text_cue.size);
            } else if (strstr(start, "\"color\":")) {
                sscanf_s(start, "\"color\": [%f, %f, %f]",
                    &current_scroll_text_cue.color.r, &current_scroll_text_cue.color.g, &current_scroll_text_cue.color.b);
            } else if (strstr(start, "\"cue_start\":")) {
                sscanf_s(start, "\"cue_start\": %f", &current_scroll_text_cue.cue_start);
            } else if (strstr(start, "\"cue_end\":")) {
                sscanf_s(start, "\"cue_end\": %f", &current_scroll_text_cue.cue_end);
            } else if (strstr(start, "\"fade_in_start\":")) {
                sscanf_s(start, "\"fade_in_start\": %f", &current_scroll_text_cue.fade_in_start);
            } else if (strstr(start, "\"fade_in_end\":")) {
                sscanf_s(start, "\"fade_in_end\": %f", &current_scroll_text_cue.fade_in_end);
            } else if (strstr(start, "\"fade_out_start\":")) {
                sscanf_s(start, "\"fade_out_start\": %f", &current_scroll_text_cue.fade_out_start);
            } else if (strstr(start, "\"fade_out_end\":")) {
                sscanf_s(start, "\"fade_out_end\": %f", &current_scroll_text_cue.fade_out_end);
            } else if (strstr(start, "\"opacity\":")) {
                sscanf_s(start, "\"opacity\": %f", &current_scroll_text_cue.opacity);
            } else if (strstr(start, "\"layer_order\":")) {
                sscanf_s(start, "\"layer_order\": %d", &current_scroll_text_cue.layer_order);
            } else if (strstr(start, "\"blend_mode\":")) {
                sscanf_s(start, "\"blend_mode\": %d", &current_scroll_text_cue.blend_mode);
            } else if (strstr(start, "\"style_id\":")) {
                sscanf_s(start, "\"style_id\": %d", &current_scroll_text_cue.style_id);
            } else if (strstr(start, "\"direction\":")) {
                sscanf_s(start, "\"direction\": %d", &current_scroll_text_cue.direction);
            } else if (strstr(start, "\"loop_mode\":")) {
                sscanf_s(start, "\"loop_mode\": %d", &current_scroll_text_cue.loop_mode);
            } else if (strstr(start, "\"speed\":")) {
                sscanf_s(start, "\"speed\": %f", &current_scroll_text_cue.speed);
            } else if (strstr(start, "\"wrap_gap\":")) {
                sscanf_s(start, "\"wrap_gap\": %f", &current_scroll_text_cue.wrap_gap);
            } else if (strstr(start, "\"spacing\":")) {
                sscanf_s(start, "\"spacing\": %f", &current_scroll_text_cue.spacing);
            } else if (strstr(start, "\"slant_deg\":")) {
                sscanf_s(start, "\"slant_deg\": %f", &current_scroll_text_cue.slant_deg);
            } else if (strstr(start, "\"wave_amp\":")) {
                sscanf_s(start, "\"wave_amp\": %f", &current_scroll_text_cue.wave_amp);
            } else if (strstr(start, "\"wave_freq\":")) {
                sscanf_s(start, "\"wave_freq\": %f", &current_scroll_text_cue.wave_freq);
            } else if (strstr(start, "\"jitter_amp\":")) {
                sscanf_s(start, "\"jitter_amp\": %f", &current_scroll_text_cue.jitter_amp);
            } else if (strstr(start, "\"jitter_freq\":")) {
                sscanf_s(start, "\"jitter_freq\": %f", &current_scroll_text_cue.jitter_freq);
            } else if (strstr(start, "\"glow\":")) {
                sscanf_s(start, "\"glow\": %f", &current_scroll_text_cue.glow);
            } else if (strstr(start, "\"shadow\":")) {
                sscanf_s(start, "\"shadow\": %f", &current_scroll_text_cue.shadow);
            } else if (strstr(start, "\"outline\":")) {
                sscanf_s(start, "\"outline\": %f", &current_scroll_text_cue.outline);
            } else if (strstr(start, "\"chroma_shift\":")) {
                sscanf_s(start, "\"chroma_shift\": %f", &current_scroll_text_cue.chroma_shift);
            } else if (strstr(start, "\"distortion\":")) {
                sscanf_s(start, "\"distortion\": %f", &current_scroll_text_cue.distortion);
            } else if (strstr(start, "\"bake_mode\":")) {
                sscanf_s(start, "\"bake_mode\": %d", &current_scroll_text_cue.bake_mode);
            } else if (strstr(start, "\"baked_asset_key\":")) {
                ParseJsonStringValue(start, current_scroll_text_cue.baked_asset_key, sizeof(current_scroll_text_cue.baked_asset_key));
            } else if (strstr(start, "\"baked_asset_path\":")) {
                ParseJsonStringValue(start, current_scroll_text_cue.baked_asset_path, sizeof(current_scroll_text_cue.baked_asset_path));
            } else if (strstr(start, "\"curve_x\":")) {
                sscanf_s(start, "\"curve_x\": %d", &current_scroll_text_cue.curve_x);
            } else if (strstr(start, "\"curve_y\":")) {
                sscanf_s(start, "\"curve_y\": %d", &current_scroll_text_cue.curve_y);
            } else if (strstr(start, "\"curve_speed\":")) {
                sscanf_s(start, "\"curve_speed\": %d", &current_scroll_text_cue.curve_speed);
            } else if (strstr(start, "\"curve_size\":")) {
                sscanf_s(start, "\"curve_size\": %d", &current_scroll_text_cue.curve_size);
            } else if (strstr(start, "\"curve_opacity\":")) {
                sscanf_s(start, "\"curve_opacity\": %d", &current_scroll_text_cue.curve_opacity);
            } else if (strstr(start, "\"curve_color_r\":")) {
                sscanf_s(start, "\"curve_color_r\": %d", &current_scroll_text_cue.curve_color_r);
            } else if (strstr(start, "\"curve_color_g\":")) {
                sscanf_s(start, "\"curve_color_g\": %d", &current_scroll_text_cue.curve_color_g);
            } else if (strstr(start, "\"curve_color_b\":")) {
                sscanf_s(start, "\"curve_color_b\": %d", &current_scroll_text_cue.curve_color_b);
            } else if (strstr(start, "\"curve_wave_amp\":")) {
                sscanf_s(start, "\"curve_wave_amp\": %d", &current_scroll_text_cue.curve_wave_amp);
            } else if (strstr(start, "\"curve_wave_freq\":")) {
                sscanf_s(start, "\"curve_wave_freq\": %d", &current_scroll_text_cue.curve_wave_freq);
            } else if (strstr(start, "\"curve_jitter_amp\":")) {
                sscanf_s(start, "\"curve_jitter_amp\": %d", &current_scroll_text_cue.curve_jitter_amp);
            } else if (strstr(start, "\"curve_jitter_freq\":")) {
                sscanf_s(start, "\"curve_jitter_freq\": %d", &current_scroll_text_cue.curve_jitter_freq);
            } else if (start[0] == '}' && current_scroll_text_cue.text[0] != '\0') {
                AddScrollTextCue(current_scene, current_scroll_text_cue);
                memset(&current_scroll_text_cue, 0, sizeof(current_scroll_text_cue));
                strncpy_s(current_scroll_text_cue.font_name, sizeof(current_scroll_text_cue.font_name), "Arial", _TRUNCATE);
                current_scroll_text_cue.size = 40.0f;
                current_scroll_text_cue.color = {1.0f, 1.0f, 1.0f};
                current_scroll_text_cue.opacity = 1.0f;
                current_scroll_text_cue.wrap_gap = 0.2f;
                current_scroll_text_cue.speed = 0.25f;
                current_scroll_text_cue.spacing = 1.0f;
                current_scroll_text_cue.wave_freq = 1.0f;
                current_scroll_text_cue.jitter_freq = 1.0f;
                current_scroll_text_cue.bake_mode = 0;
                current_scroll_text_cue.curve_x = -1;
                current_scroll_text_cue.curve_y = -1;
                current_scroll_text_cue.curve_speed = -1;
                current_scroll_text_cue.curve_size = -1;
                current_scroll_text_cue.curve_opacity = -1;
                current_scroll_text_cue.curve_color_r = -1;
                current_scroll_text_cue.curve_color_g = -1;
                current_scroll_text_cue.curve_color_b = -1;
                current_scroll_text_cue.curve_wave_amp = -1;
                current_scroll_text_cue.curve_wave_freq = -1;
                current_scroll_text_cue.curve_jitter_amp = -1;
                current_scroll_text_cue.curve_jitter_freq = -1;
            }
        }

        // Parse music cue fields
        if (in_music_cues && current_scene) {
            if (strstr(start, "\"asset_key\":")) {
                ParseJsonStringValue(start, current_music_cue.asset_key, sizeof(current_music_cue.asset_key));
            } else if (strstr(start, "\"asset_path\":")) {
                ParseJsonStringValue(start, current_music_cue.asset_path, sizeof(current_music_cue.asset_path));
            } else if (strstr(start, "\"cue_start\":")) {
                sscanf_s(start, "\"cue_start\": %f", &current_music_cue.cue_start);
            } else if (strstr(start, "\"cue_end\":")) {
                sscanf_s(start, "\"cue_end\": %f", &current_music_cue.cue_end);
            } else if (start[0] == '}' && current_music_cue.asset_key[0] != '\0') {
                printf("[LoadProject] Loaded music cue: %s path=%s\n",
                       current_music_cue.asset_key, current_music_cue.asset_path);
                AddMusicCue(current_scene, current_music_cue);
                memset(&current_music_cue, 0, sizeof(current_music_cue));
            }
        }

        // Parse mesh cue fields
        if (in_mesh_cues && current_scene) {
            if (strstr(start, "\"asset_key\":")) {
                ParseJsonStringValue(start, current_mesh_cue.asset_key, sizeof(current_mesh_cue.asset_key));
            } else if (strstr(start, "\"asset_path\":")) {
                ParseJsonStringValue(start, current_mesh_cue.asset_path, sizeof(current_mesh_cue.asset_path));
            } else if (strstr(start, "\"mesh_type\":")) {
                sscanf_s(start, "\"mesh_type\": %d", &current_mesh_cue.mesh_type);
            } else if (strstr(start, "\"pos\":")) {
                sscanf_s(start, "\"pos\": [%f, %f, %f]", &current_mesh_cue.pos[0], &current_mesh_cue.pos[1], &current_mesh_cue.pos[2]);
            } else if (strstr(start, "\"rot\":")) {
                sscanf_s(start, "\"rot\": [%f, %f, %f]", &current_mesh_cue.rot[0], &current_mesh_cue.rot[1], &current_mesh_cue.rot[2]);
            } else if (strstr(start, "\"scale\":")) {
                sscanf_s(start, "\"scale\": [%f, %f, %f]", &current_mesh_cue.scale[0], &current_mesh_cue.scale[1], &current_mesh_cue.scale[2]);
            } else if (strstr(start, "\"color\":")) {
                sscanf_s(start, "\"color\": [%f, %f, %f, %f]", &current_mesh_cue.color[0], &current_mesh_cue.color[1], &current_mesh_cue.color[2], &current_mesh_cue.color[3]);
            } else if (strstr(start, "\"mesh_size\":")) {
                sscanf_s(start, "\"mesh_size\": %f", &current_mesh_cue.mesh_size);
            } else if (strstr(start, "\"mesh_param\":")) {
                sscanf_s(start, "\"mesh_param\": %f", &current_mesh_cue.mesh_param);
            } else if (strstr(start, "\"metallic\":")) {
                sscanf_s(start, "\"metallic\": %f", &current_mesh_cue.metallic);
            } else if (strstr(start, "\"roughness\":")) {
                sscanf_s(start, "\"roughness\": %f", &current_mesh_cue.roughness);
            } else if (strstr(start, "\"effect_type\":")) {
                sscanf_s(start, "\"effect_type\": %d", &current_mesh_cue.effect_type);
            } else if (strstr(start, "\"cue_start\":")) {
                sscanf_s(start, "\"cue_start\": %f", &current_mesh_cue.cue_start);
            } else if (strstr(start, "\"cue_end\":")) {
                sscanf_s(start, "\"cue_end\": %f", &current_mesh_cue.cue_end);
            } else if (strstr(start, "\"fade_in_start\":")) {
                sscanf_s(start, "\"fade_in_start\": %f", &current_mesh_cue.fade_in_start);
            } else if (strstr(start, "\"fade_in_end\":")) {
                sscanf_s(start, "\"fade_in_end\": %f", &current_mesh_cue.fade_in_end);
            } else if (strstr(start, "\"fade_out_start\":")) {
                sscanf_s(start, "\"fade_out_start\": %f", &current_mesh_cue.fade_out_start);
            } else if (strstr(start, "\"fade_out_end\":")) {
                sscanf_s(start, "\"fade_out_end\": %f", &current_mesh_cue.fade_out_end);
            } else if (strstr(start, "\"layer_order\":")) {
                sscanf_s(start, "\"layer_order\": %d", &current_mesh_cue.layer_order);
            } else if (strstr(start, "\"curve_pos_x\":")) {
                sscanf_s(start, "\"curve_pos_x\": %d", &current_mesh_cue.curve_pos_x);
            } else if (strstr(start, "\"curve_pos_y\":")) {
                sscanf_s(start, "\"curve_pos_y\": %d", &current_mesh_cue.curve_pos_y);
            } else if (strstr(start, "\"curve_pos_z\":")) {
                sscanf_s(start, "\"curve_pos_z\": %d", &current_mesh_cue.curve_pos_z);
            } else if (strstr(start, "\"curve_rot_x\":")) {
                sscanf_s(start, "\"curve_rot_x\": %d", &current_mesh_cue.curve_rot_x);
            } else if (strstr(start, "\"curve_rot_y\":")) {
                sscanf_s(start, "\"curve_rot_y\": %d", &current_mesh_cue.curve_rot_y);
            } else if (strstr(start, "\"curve_rot_z\":")) {
                sscanf_s(start, "\"curve_rot_z\": %d", &current_mesh_cue.curve_rot_z);
            } else if (strstr(start, "\"curve_scale_x\":")) {
                sscanf_s(start, "\"curve_scale_x\": %d", &current_mesh_cue.curve_scale_x);
            } else if (strstr(start, "\"curve_scale_y\":")) {
                sscanf_s(start, "\"curve_scale_y\": %d", &current_mesh_cue.curve_scale_y);
            } else if (strstr(start, "\"curve_scale_z\":")) {
                sscanf_s(start, "\"curve_scale_z\": %d", &current_mesh_cue.curve_scale_z);
            } else if (strstr(start, "\"curve_color_r\":")) {
                sscanf_s(start, "\"curve_color_r\": %d", &current_mesh_cue.curve_color_r);
            } else if (strstr(start, "\"curve_color_g\":")) {
                sscanf_s(start, "\"curve_color_g\": %d", &current_mesh_cue.curve_color_g);
            } else if (strstr(start, "\"curve_color_b\":")) {
                sscanf_s(start, "\"curve_color_b\": %d", &current_mesh_cue.curve_color_b);
            } else if (strstr(start, "\"curve_color_a\":")) {
                sscanf_s(start, "\"curve_color_a\": %d", &current_mesh_cue.curve_color_a);
            } else if (strstr(start, "\"curve_mesh_size\":")) {
                sscanf_s(start, "\"curve_mesh_size\": %d", &current_mesh_cue.curve_mesh_size);
            } else if (strstr(start, "\"curve_metallic\":")) {
                sscanf_s(start, "\"curve_metallic\": %d", &current_mesh_cue.curve_metallic);
            } else if (strstr(start, "\"curve_roughness\":")) {
                sscanf_s(start, "\"curve_roughness\": %d", &current_mesh_cue.curve_roughness);
            } else if (start[0] == '}' && current_mesh_cue.asset_key[0] != '\0') {
                AddMeshCue(current_scene, current_mesh_cue);
                MeshCue blank = {};
                blank.scale[0] = blank.scale[1] = blank.scale[2] = 1.0f;
                blank.roughness = 0.5f;
                blank.curve_pos_x = blank.curve_pos_y = blank.curve_pos_z = -1;
                blank.curve_rot_x = blank.curve_rot_y = blank.curve_rot_z = -1;
                blank.curve_scale_x = blank.curve_scale_y = blank.curve_scale_z = -1;
                blank.curve_color_r = blank.curve_color_g = blank.curve_color_b = blank.curve_color_a = -1;
                blank.curve_mesh_size = blank.curve_metallic = blank.curve_roughness = -1;
                current_mesh_cue = blank;
            }
        }

        // Parse curve data
        if (in_curves && !in_curve_points) {
            // Create new curve if we haven't yet for this curve object
            if (!current_curve && editor->project->curve_count < 32) {
                if (strstr(start, "\"wrap_mode\":") || strstr(start, "\"points\":")) {
                    current_curve = &editor->project->curves[editor->project->curve_count++];
                    *current_curve = rev::curve::CreateCurve();
                }
            }
            
            // Parse wrap mode
            if (current_curve && strstr(start, "\"wrap_mode\":")) {
                char wrap_mode[32];
                if (sscanf_s(start, "\"wrap_mode\": \"%31[^\"]\"", wrap_mode, (unsigned)sizeof(wrap_mode)) == 1) {
                    if (strcmp(wrap_mode, "loop") == 0) current_curve->wrap_mode = rev::curve::WrapMode::Loop;
                    else if (strcmp(wrap_mode, "pingpong") == 0) current_curve->wrap_mode = rev::curve::WrapMode::PingPong;
                    else if (strcmp(wrap_mode, "mirror") == 0) current_curve->wrap_mode = rev::curve::WrapMode::Mirror;
                    else current_curve->wrap_mode = rev::curve::WrapMode::Clamp;
                }
            }
            
            // Parse duration
            if (current_curve && strstr(start, "\"duration\":")) {
                float duration;
                if (sscanf_s(start, "\"duration\": %f", &duration) == 1) {
                    current_curve->duration = duration;
                }
            }
            
            // Start parsing points
            if (strstr(start, "\"points\":")) {
                in_curve_points = true;
            }
        }
        
        // Parse individual curve points
        if (in_curve_points && current_curve && strstr(start, "{\"t\":")) {
            float t, v, in_ease, out_ease;
            char mode[32];
            if (sscanf_s(start, "{\"t\": %f, \"v\": %f, \"in_ease\": %f, \"out_ease\": %f, \"mode\": \"%31[^\"]\"",
                &t, &v, &in_ease, &out_ease, mode, (unsigned)sizeof(mode)) == 5) {
                
                rev::curve::EaseMode ease_mode = rev::curve::EaseMode::Linear;
                if (strcmp(mode, "ease_in") == 0) ease_mode = rev::curve::EaseMode::EaseIn;
                else if (strcmp(mode, "ease_out") == 0) ease_mode = rev::curve::EaseMode::EaseOut;
                else if (strcmp(mode, "ease_in_out") == 0) ease_mode = rev::curve::EaseMode::EaseInOut;
                else if (strcmp(mode, "smoothstep") == 0) ease_mode = rev::curve::EaseMode::Smoothstep;
                else if (strcmp(mode, "hold") == 0) ease_mode = rev::curve::EaseMode::Hold;
                
                rev::curve::AddPoint(*current_curve, t, v, ease_mode);
                current_curve->points[current_curve->point_count - 1].in_ease = in_ease;
                current_curve->points[current_curve->point_count - 1].out_ease = out_ease;
            }
        } else if (in_curve_points && strstr(start, "]")) {
            in_curve_points = false;
            current_curve = nullptr;
        }
    }
    
    fclose(f);
    
    strncpy_s(editor->project->project_path, path, sizeof(editor->project->project_path) - 1);
    
    // Set workspace_path to the directory containing the project file
    strncpy_s(editor->project->workspace_path, path, sizeof(editor->project->workspace_path) - 1);
    // Find last slash/backslash and truncate to get directory
    char* last_slash = strrchr(editor->project->workspace_path, '\\');
    if (!last_slash) last_slash = strrchr(editor->project->workspace_path, '/');
    if (last_slash) *last_slash = '\0';
    
    printf("[LoadProject] Workspace path set to: %s\n", editor->project->workspace_path);
    
    // Create project-specific assets folder path
    // Extract project name from path (filename without extension)
    char project_name[256] = {0};
    const char* filename_start = strrchr(path, '\\');
    if (!filename_start) filename_start = strrchr(path, '/');
    filename_start = filename_start ? filename_start + 1 : path;
    
    // Copy filename and remove extension
    strncpy_s(project_name, filename_start, sizeof(project_name) - 1);
    char* dot = strrchr(project_name, '.');
    if (dot) *dot = '\0';
    
    // Create assets folder path: workspace_path\{project_name}_assets
    snprintf(editor->project->assets_path, sizeof(editor->project->assets_path),
             "%s\\%s_assets", editor->project->workspace_path, project_name);
    
    // Create the directory if it doesn't exist
    CreateDirectoryA(editor->project->assets_path, NULL);
    
    printf("[LoadProject] Assets path set to: %s\n", editor->project->assets_path);
    
    editor->project->modified = false;
    
    return true;
}

bool SaveProject(EditorContext* editor, const char* path) {
    if (!editor || !path) return false;
    
    FILE* f = nullptr;
    fopen_s(&f, path, "w");
    if (!f) return false;
    
    char escaped_workspace[1024] = {};
    JsonEscapeString(editor->project->workspace_path, escaped_workspace, sizeof(escaped_workspace));

    fprintf(f, "{\n");
    fprintf(f, "  \"version\": \"1.0\",\n");
    fprintf(f, "  \"workspace_path\": \"%s\",\n", escaped_workspace);
    fprintf(f, "  \"total_duration\": %.3f,\n", editor->project->total_duration);
    fprintf(f, "  \"loop_intro\": %d,\n", editor->project->loop_intro ? 1 : 0);
    fprintf(f, "  \"loop_music\": %d,\n", editor->project->loop_music ? 1 : 0);
    fprintf(f, "  \"scenes\": [\n");
    
    // Save scenes
    for (int s = 0; s < editor->project->scene_count; ++s) {
        SceneBlock* scene = &editor->project->scenes[s];
        char escaped_scene_name[128] = {};
        JsonEscapeString(scene->name, escaped_scene_name, sizeof(escaped_scene_name));
        fprintf(f, "    {\n");
        fprintf(f, "      \"name\": \"%s\",\n", escaped_scene_name);
        fprintf(f, "      \"duration\": %.3f,\n", scene->duration);
        
        // Shader cues
        fprintf(f, "      \"shader_cues\": [\n");
        for (int i = 0; i < scene->shader_cue_count; ++i) {
            ShaderCue* cue = &scene->shader_cues[i];
            char escaped_shader_name[128] = {};
            JsonEscapeString(cue->shader_name, escaped_shader_name, sizeof(escaped_shader_name));
            fprintf(f, "        {\n");
            fprintf(f, "          \"shader_name\": \"%s\",\n", escaped_shader_name);
            fprintf(f, "          \"shader_scene_id\": %d,\n", cue->shader_scene_id);
            fprintf(f, "          \"palette_low\": [%.3f, %.3f, %.3f],\n", 
                cue->palette_low.r, cue->palette_low.g, cue->palette_low.b);
            fprintf(f, "          \"palette_mid\": [%.3f, %.3f, %.3f],\n",
                cue->palette_mid.r, cue->palette_mid.g, cue->palette_mid.b);
            fprintf(f, "          \"palette_high\": [%.3f, %.3f, %.3f],\n",
                cue->palette_high.r, cue->palette_high.g, cue->palette_high.b);
            fprintf(f, "          \"speed\": %.3f,\n", cue->speed);
            fprintf(f, "          \"intensity\": %.3f,\n", cue->intensity);
            fprintf(f, "          \"warp\": %.3f,\n", cue->warp);
            fprintf(f, "          \"exposure_base\": %.3f,\n", cue->exposure_base);
            fprintf(f, "          \"exposure_ramp\": %.3f,\n", cue->exposure_ramp);
            fprintf(f, "          \"fade_base\": %.3f,\n", cue->fade_base);
            fprintf(f, "          \"fade_ramp\": %.3f,\n", cue->fade_ramp);
            fprintf(f, "          \"cue_start\": %.3f,\n", cue->cue_start);
            fprintf(f, "          \"cue_end\": %.3f,\n", cue->cue_end);
            fprintf(f, "          \"fade_in\": %.3f,\n", cue->fade_in);
            fprintf(f, "          \"fade_out\": %.3f,\n", cue->fade_out);
            fprintf(f, "          \"layer_role\": %d,\n", cue->layer_role);
            fprintf(f, "          \"opacity\": %.3f,\n", cue->opacity);
            fprintf(f, "          \"blend_mode\": %d,\n", cue->blend_mode);
            fprintf(f, "          \"layer_order\": %d,\n", cue->layer_order);
            fprintf(f, "          \"curve_speed\": %d,\n", cue->curve_speed);
            fprintf(f, "          \"curve_intensity\": %d,\n", cue->curve_intensity);
            fprintf(f, "          \"curve_warp\": %d,\n", cue->curve_warp);
            fprintf(f, "          \"curve_exposure\": %d,\n", cue->curve_exposure);
            fprintf(f, "          \"curve_fade\": %d,\n", cue->curve_fade);
            fprintf(f, "          \"curve_palette_low_r\": %d,\n", cue->curve_palette_low_r);
            fprintf(f, "          \"curve_palette_low_g\": %d,\n", cue->curve_palette_low_g);
            fprintf(f, "          \"curve_palette_low_b\": %d,\n", cue->curve_palette_low_b);
            fprintf(f, "          \"curve_palette_mid_r\": %d,\n", cue->curve_palette_mid_r);
            fprintf(f, "          \"curve_palette_mid_g\": %d,\n", cue->curve_palette_mid_g);
            fprintf(f, "          \"curve_palette_mid_b\": %d,\n", cue->curve_palette_mid_b);
            fprintf(f, "          \"curve_palette_high_r\": %d,\n", cue->curve_palette_high_r);
            fprintf(f, "          \"curve_palette_high_g\": %d,\n", cue->curve_palette_high_g);
            fprintf(f, "          \"curve_palette_high_b\": %d,\n", cue->curve_palette_high_b);
            fprintf(f, "          \"curve_opacity\": %d,\n", cue->curve_opacity);
            fprintf(f, "          \"curve_exposure_ramp\": %d,\n", cue->curve_exposure_ramp);
            fprintf(f, "          \"curve_fade_ramp\": %d\n", cue->curve_fade_ramp);
            fprintf(f, "        }%s\n", (i < scene->shader_cue_count - 1) ? "," : "");
        }
        fprintf(f, "      ],\n");
        
        // Image cues
        fprintf(f, "      \"image_cues\": [\n");
        for (int i = 0; i < scene->image_cue_count; ++i) {
            ImageCue* cue = &scene->image_cues[i];
            char escaped_asset_key[256] = {};
            JsonEscapeString(cue->asset_key, escaped_asset_key, sizeof(escaped_asset_key));
            fprintf(f, "        {\n");
            fprintf(f, "          \"asset_key\": \"%s\",\n", escaped_asset_key);
            fprintf(f, "          \"x\": %.3f,\n", cue->x);
            fprintf(f, "          \"y\": %.3f,\n", cue->y);
            fprintf(f, "          \"scale\": %.3f,\n", cue->scale);
            fprintf(f, "          \"opacity\": %.3f,\n", cue->opacity);
            fprintf(f, "          \"effect_type\": %d,\n", cue->effect_type);
            fprintf(f, "          \"cue_start\": %.3f,\n", cue->cue_start);
            fprintf(f, "          \"cue_end\": %.3f,\n", cue->cue_end);
            fprintf(f, "          \"fade_in_start\": %.3f,\n", cue->fade_in_start);
            fprintf(f, "          \"fade_in_end\": %.3f,\n", cue->fade_in_end);
            fprintf(f, "          \"fade_out_start\": %.3f,\n", cue->fade_out_start);
            fprintf(f, "          \"fade_out_end\": %.3f,\n", cue->fade_out_end);
            fprintf(f, "          \"blend_mode\": %d,\n", cue->blend_mode);
            fprintf(f, "          \"layer_order\": %d,\n", cue->layer_order);
            fprintf(f, "          \"curve_x\": %d,\n", cue->curve_x);
            fprintf(f, "          \"curve_y\": %d,\n", cue->curve_y);
            fprintf(f, "          \"curve_scale\": %d,\n", cue->curve_scale);
            fprintf(f, "          \"curve_opacity\": %d\n", cue->curve_opacity);
            fprintf(f, "        }%s\n", (i < scene->image_cue_count - 1) ? "," : "");
        }
        fprintf(f, "      ],\n");
        
        // Text cues
        fprintf(f, "      \"text_cues\": [\n");
        for (int i = 0; i < scene->text_cue_count; ++i) {
            TextCue* cue = &scene->text_cues[i];
            char escaped_text[512] = {};
            char escaped_font[128] = {};
            char escaped_baked_key[128] = {};
            char escaped_baked_path[1024] = {};
            JsonEscapeString(cue->text, escaped_text, sizeof(escaped_text));
            JsonEscapeString(cue->font_name, escaped_font, sizeof(escaped_font));
            JsonEscapeString(cue->baked_asset_key, escaped_baked_key, sizeof(escaped_baked_key));
            JsonEscapeString(cue->baked_asset_path, escaped_baked_path, sizeof(escaped_baked_path));
            fprintf(f, "        {\n");
            fprintf(f, "          \"text\": \"%s\",\n", escaped_text);
            fprintf(f, "          \"font_name\": \"%s\",\n", escaped_font);
            fprintf(f, "          \"x\": %.3f,\n", cue->x);
            fprintf(f, "          \"y\": %.3f,\n", cue->y);
            fprintf(f, "          \"size\": %.3f,\n", cue->size);
            fprintf(f, "          \"color\": [%.3f, %.3f, %.3f],\n",
                cue->color.r, cue->color.g, cue->color.b);
            fprintf(f, "          \"effect_type\": %d,\n", cue->effect_type);
            fprintf(f, "          \"cue_start\": %.3f,\n", cue->cue_start);
            fprintf(f, "          \"cue_end\": %.3f,\n", cue->cue_end);
            fprintf(f, "          \"fade_in_start\": %.3f,\n", cue->fade_in_start);
            fprintf(f, "          \"fade_in_end\": %.3f,\n", cue->fade_in_end);
            fprintf(f, "          \"fade_out_start\": %.3f,\n", cue->fade_out_start);
            fprintf(f, "          \"fade_out_end\": %.3f,\n", cue->fade_out_end);
            fprintf(f, "          \"blend_mode\": %d,\n", cue->blend_mode);
            fprintf(f, "          \"bake_mode\": %d,\n", cue->bake_mode);
            fprintf(f, "          \"curve_x\": %d,\n", cue->curve_x);
            fprintf(f, "          \"curve_y\": %d,\n", cue->curve_y);
            fprintf(f, "          \"curve_size\": %d,\n", cue->curve_size);
            fprintf(f, "          \"curve_color_r\": %d,\n", cue->curve_color_r);
            fprintf(f, "          \"curve_color_g\": %d,\n", cue->curve_color_g);
            fprintf(f, "          \"curve_color_b\": %d,\n", cue->curve_color_b);
            fprintf(f, "          \"baked_asset_key\": \"%s\",\n", escaped_baked_key);
            fprintf(f, "          \"baked_asset_path\": \"%s\",\n", escaped_baked_path);
            fprintf(f, "          \"layer_order\": %d\n", cue->layer_order);
            fprintf(f, "        }%s\n", (i < scene->text_cue_count - 1) ? "," : "");
        }
        fprintf(f, "      ],\n");

        // Scroll text cues
        fprintf(f, "      \"scroll_text_cues\": [\n");
        for (int i = 0; i < scene->scroll_text_cue_count; ++i) {
            ScrollTextCue* cue = &scene->scroll_text_cues[i];
            char escaped_text[1024] = {};
            char escaped_font[128] = {};
            char escaped_baked_key[128] = {};
            char escaped_baked_path[1024] = {};
            JsonEscapeString(cue->text, escaped_text, sizeof(escaped_text));
            JsonEscapeString(cue->font_name, escaped_font, sizeof(escaped_font));
            JsonEscapeString(cue->baked_asset_key, escaped_baked_key, sizeof(escaped_baked_key));
            JsonEscapeString(cue->baked_asset_path, escaped_baked_path, sizeof(escaped_baked_path));
            fprintf(f, "        {\n");
            fprintf(f, "          \"text\": \"%s\",\n", escaped_text);
            fprintf(f, "          \"font_name\": \"%s\",\n", escaped_font);
            fprintf(f, "          \"x\": %.3f,\n", cue->x);
            fprintf(f, "          \"y\": %.3f,\n", cue->y);
            fprintf(f, "          \"size\": %.3f,\n", cue->size);
            fprintf(f, "          \"color\": [%.3f, %.3f, %.3f],\n", cue->color.r, cue->color.g, cue->color.b);
            fprintf(f, "          \"cue_start\": %.3f,\n", cue->cue_start);
            fprintf(f, "          \"cue_end\": %.3f,\n", cue->cue_end);
            fprintf(f, "          \"fade_in_start\": %.3f,\n", cue->fade_in_start);
            fprintf(f, "          \"fade_in_end\": %.3f,\n", cue->fade_in_end);
            fprintf(f, "          \"fade_out_start\": %.3f,\n", cue->fade_out_start);
            fprintf(f, "          \"fade_out_end\": %.3f,\n", cue->fade_out_end);
            fprintf(f, "          \"opacity\": %.3f,\n", cue->opacity);
            fprintf(f, "          \"layer_order\": %d,\n", cue->layer_order);
            fprintf(f, "          \"blend_mode\": %d,\n", cue->blend_mode);
            fprintf(f, "          \"style_id\": %d,\n", cue->style_id);
            fprintf(f, "          \"direction\": %d,\n", cue->direction);
            fprintf(f, "          \"loop_mode\": %d,\n", cue->loop_mode);
            fprintf(f, "          \"speed\": %.3f,\n", cue->speed);
            fprintf(f, "          \"wrap_gap\": %.3f,\n", cue->wrap_gap);
            fprintf(f, "          \"spacing\": %.3f,\n", cue->spacing);
            fprintf(f, "          \"slant_deg\": %.3f,\n", cue->slant_deg);
            fprintf(f, "          \"wave_amp\": %.3f,\n", cue->wave_amp);
            fprintf(f, "          \"wave_freq\": %.3f,\n", cue->wave_freq);
            fprintf(f, "          \"jitter_amp\": %.3f,\n", cue->jitter_amp);
            fprintf(f, "          \"jitter_freq\": %.3f,\n", cue->jitter_freq);
            fprintf(f, "          \"glow\": %.3f,\n", cue->glow);
            fprintf(f, "          \"shadow\": %.3f,\n", cue->shadow);
            fprintf(f, "          \"outline\": %.3f,\n", cue->outline);
            fprintf(f, "          \"chroma_shift\": %.3f,\n", cue->chroma_shift);
            fprintf(f, "          \"distortion\": %.3f,\n", cue->distortion);
            fprintf(f, "          \"bake_mode\": %d,\n", cue->bake_mode);
            fprintf(f, "          \"curve_x\": %d,\n", cue->curve_x);
            fprintf(f, "          \"curve_y\": %d,\n", cue->curve_y);
            fprintf(f, "          \"curve_speed\": %d,\n", cue->curve_speed);
            fprintf(f, "          \"curve_size\": %d,\n", cue->curve_size);
            fprintf(f, "          \"curve_opacity\": %d,\n", cue->curve_opacity);
            fprintf(f, "          \"curve_color_r\": %d,\n", cue->curve_color_r);
            fprintf(f, "          \"curve_color_g\": %d,\n", cue->curve_color_g);
            fprintf(f, "          \"curve_color_b\": %d,\n", cue->curve_color_b);
            fprintf(f, "          \"curve_wave_amp\": %d,\n", cue->curve_wave_amp);
            fprintf(f, "          \"curve_wave_freq\": %d,\n", cue->curve_wave_freq);
            fprintf(f, "          \"curve_jitter_amp\": %d,\n", cue->curve_jitter_amp);
            fprintf(f, "          \"curve_jitter_freq\": %d,\n", cue->curve_jitter_freq);
            fprintf(f, "          \"baked_asset_key\": \"%s\",\n", escaped_baked_key);
            fprintf(f, "          \"baked_asset_path\": \"%s\"\n", escaped_baked_path);
            fprintf(f, "        }%s\n", (i < scene->scroll_text_cue_count - 1) ? "," : "");
        }
        fprintf(f, "      ],\n");
        
        // Music cues
        fprintf(f, "      \"music_cues\": [\n");
        for (int i = 0; i < scene->music_cue_count; ++i) {
            MusicCue* cue = &scene->music_cues[i];
            char escaped_asset_key[128] = {};
            char escaped_asset_path[1024] = {};
            JsonEscapeString(cue->asset_key, escaped_asset_key, sizeof(escaped_asset_key));
            JsonEscapeString(cue->asset_path, escaped_asset_path, sizeof(escaped_asset_path));
            fprintf(f, "        {\n");
            fprintf(f, "          \"asset_key\": \"%s\",\n", escaped_asset_key);
            fprintf(f, "          \"asset_path\": \"%s\",\n", escaped_asset_path);
            fprintf(f, "          \"cue_start\": %.3f,\n", cue->cue_start);
            fprintf(f, "          \"cue_end\": %.3f\n", cue->cue_end);
            fprintf(f, "        }%s\n", (i < scene->music_cue_count - 1) ? "," : "");
        }
        fprintf(f, "      ],\n");

        // Mesh cues
        fprintf(f, "      \"mesh_cues\": [\n");
        for (int i = 0; i < scene->mesh_cue_count; ++i) {
            MeshCue* cue = &scene->mesh_cues[i];
            char escaped_asset_key[128] = {};
            char escaped_asset_path[1024] = {};
            JsonEscapeString(cue->asset_key, escaped_asset_key, sizeof(escaped_asset_key));
            JsonEscapeString(cue->asset_path, escaped_asset_path, sizeof(escaped_asset_path));
            fprintf(f, "        {\n");
            fprintf(f, "          \"asset_key\": \"%s\",\n",   escaped_asset_key);
            fprintf(f, "          \"asset_path\": \"%s\",\n",  escaped_asset_path);
            fprintf(f, "          \"mesh_type\": %d,\n",        cue->mesh_type);
            fprintf(f, "          \"pos\": [%.3f, %.3f, %.3f],\n",   cue->pos[0],   cue->pos[1],   cue->pos[2]);
            fprintf(f, "          \"rot\": [%.3f, %.3f, %.3f],\n",   cue->rot[0],   cue->rot[1],   cue->rot[2]);
            fprintf(f, "          \"scale\": [%.3f, %.3f, %.3f],\n", cue->scale[0], cue->scale[1], cue->scale[2]);
            fprintf(f, "          \"color\": [%.3f, %.3f, %.3f, %.3f],\n", cue->color[0], cue->color[1], cue->color[2], cue->color[3]);
            fprintf(f, "          \"mesh_size\": %.3f,\n",  cue->mesh_size);
            fprintf(f, "          \"mesh_param\": %.3f,\n", cue->mesh_param);
            fprintf(f, "          \"metallic\": %.3f,\n",   cue->metallic);
            fprintf(f, "          \"roughness\": %.3f,\n",  cue->roughness);
            fprintf(f, "          \"effect_type\": %d,\n",  cue->effect_type);
            fprintf(f, "          \"cue_start\": %.3f,\n",  cue->cue_start);
            fprintf(f, "          \"cue_end\": %.3f,\n",    cue->cue_end);
            fprintf(f, "          \"fade_in_start\": %.3f,\n",  cue->fade_in_start);
            fprintf(f, "          \"fade_in_end\": %.3f,\n",    cue->fade_in_end);
            fprintf(f, "          \"fade_out_start\": %.3f,\n", cue->fade_out_start);
            fprintf(f, "          \"fade_out_end\": %.3f,\n",   cue->fade_out_end);
            fprintf(f, "          \"layer_order\": %d,\n",   cue->layer_order);
            fprintf(f, "          \"curve_pos_x\": %d,\n", cue->curve_pos_x);
            fprintf(f, "          \"curve_pos_y\": %d,\n", cue->curve_pos_y);
            fprintf(f, "          \"curve_pos_z\": %d,\n", cue->curve_pos_z);
            fprintf(f, "          \"curve_rot_x\": %d,\n", cue->curve_rot_x);
            fprintf(f, "          \"curve_rot_y\": %d,\n", cue->curve_rot_y);
            fprintf(f, "          \"curve_rot_z\": %d,\n", cue->curve_rot_z);
            fprintf(f, "          \"curve_scale_x\": %d,\n", cue->curve_scale_x);
            fprintf(f, "          \"curve_scale_y\": %d,\n", cue->curve_scale_y);
            fprintf(f, "          \"curve_scale_z\": %d,\n", cue->curve_scale_z);
            fprintf(f, "          \"curve_color_r\": %d,\n", cue->curve_color_r);
            fprintf(f, "          \"curve_color_g\": %d,\n", cue->curve_color_g);
            fprintf(f, "          \"curve_color_b\": %d,\n", cue->curve_color_b);
            fprintf(f, "          \"curve_color_a\": %d,\n", cue->curve_color_a);
            fprintf(f, "          \"curve_mesh_size\": %d,\n", cue->curve_mesh_size);
            fprintf(f, "          \"curve_metallic\": %d,\n", cue->curve_metallic);
            fprintf(f, "          \"curve_roughness\": %d\n", cue->curve_roughness);
            fprintf(f, "        }%s\n", (i < scene->mesh_cue_count - 1) ? "," : "");
        }
        fprintf(f, "      ]\n");
        
        fprintf(f, "    }%s\n", (s < editor->project->scene_count - 1) ? "," : "");
    }
    
    fprintf(f, "  ],\n");
    
    // Save curves
    fprintf(f, "  \"curves\": [\n");
    for (int c = 0; c < editor->project->curve_count; ++c) {
        rev::curve::Curve* curve = &editor->project->curves[c];
        fprintf(f, "    {\n");
        
        // Save wrap mode
        const char* wrap_mode_str = "clamp";
        switch (curve->wrap_mode) {
            case rev::curve::WrapMode::Clamp: wrap_mode_str = "clamp"; break;
            case rev::curve::WrapMode::Loop: wrap_mode_str = "loop"; break;
            case rev::curve::WrapMode::PingPong: wrap_mode_str = "pingpong"; break;
            case rev::curve::WrapMode::Mirror: wrap_mode_str = "mirror"; break;
        }
        fprintf(f, "      \"wrap_mode\": \"%s\",\n", wrap_mode_str);
        fprintf(f, "      \"duration\": %.3f,\n", curve->duration);
        
        fprintf(f, "      \"points\": [\n");
        
        for (int p = 0; p < curve->point_count; ++p) {
            rev::curve::Point* pt = &curve->points[p];
            
            const char* mode_str = "linear";
            switch (pt->mode) {
                case rev::curve::EaseMode::Linear: mode_str = "linear"; break;
                case rev::curve::EaseMode::EaseIn: mode_str = "ease_in"; break;
                case rev::curve::EaseMode::EaseOut: mode_str = "ease_out"; break;
                case rev::curve::EaseMode::EaseInOut: mode_str = "ease_in_out"; break;
                case rev::curve::EaseMode::Smoothstep: mode_str = "smoothstep"; break;
                case rev::curve::EaseMode::Hold: mode_str = "hold"; break;
            }
            
            fprintf(f, "        {\"t\": %.3f, \"v\": %.3f, \"in_ease\": %.3f, \"out_ease\": %.3f, \"mode\": \"%s\"}%s\n",
                pt->t, pt->v, pt->in_ease, pt->out_ease, mode_str,
                (p < curve->point_count - 1) ? "," : ""
            );
        }
        
        fprintf(f, "      ]\n");
        fprintf(f, "    }%s\n", (c < editor->project->curve_count - 1) ? "," : "");
    }
    fprintf(f, "  ]\n");
    
    fprintf(f, "}\n");
    
    fclose(f);
    
    strncpy_s(editor->project->project_path, path, sizeof(editor->project->project_path) - 1);

    // Derive workspace_path (directory of the project file)
    strncpy_s(editor->project->workspace_path, path, sizeof(editor->project->workspace_path) - 1);
    char* last_slash = strrchr(editor->project->workspace_path, '\\');
    if (!last_slash) last_slash = strrchr(editor->project->workspace_path, '/');
    if (last_slash) *last_slash = '\0';

    // Derive assets_path: {workspace}\{project_name}_assets
    char project_name[256] = {};
    const char* fn = strrchr(path, '\\');
    if (!fn) fn = strrchr(path, '/');
    fn = fn ? fn + 1 : path;
    strncpy_s(project_name, fn, sizeof(project_name) - 1);
    char* dot = strrchr(project_name, '.');
    if (dot) *dot = '\0';
    snprintf(editor->project->assets_path, sizeof(editor->project->assets_path),
             "%s\\%s_assets", editor->project->workspace_path, project_name);
    CreateDirectoryA(editor->project->assets_path, NULL);

    editor->project->modified = false;
    
    return true;
}

bool NewProject(EditorContext* editor) {
    if (!editor) return false;
    
    // Clean up existing scenes
    for (int i = 0; i < editor->project->scene_count; ++i) {
        SceneBlock* scene = &editor->project->scenes[i];
        delete[] scene->shader_cues;
        delete[] scene->image_cues;
        delete[] scene->text_cues;
        delete[] scene->scroll_text_cues;
        delete[] scene->music_cues;
        delete[] scene->mesh_cues;
    }
    delete[] editor->project->scenes;
    editor->project->scenes = nullptr;
    editor->project->scene_count = 0;
    editor->project->scene_capacity = 0;
    
    // Clean up curves
    if (editor->project->curves) {
        for (int i = 0; i < editor->project->curve_count; ++i) {
            rev::curve::DestroyCurve(editor->project->curves[i]);
        }
        editor->project->curve_count = 0;
    }
    
    editor->project->total_duration = 0.0f;  // Will be updated as scenes are added
    editor->project->loop_intro = false;
    editor->project->loop_music = false;
    memset(editor->project->project_path, 0, sizeof(editor->project->project_path));
    memset(editor->project->workspace_path, 0, sizeof(editor->project->workspace_path));
    memset(editor->project->assets_path, 0, sizeof(editor->project->assets_path));
    editor->project->modified = false;

    // Note: GetScene(0) returns nullptr when scene_count==0
    // First scene will be created via AddScene when user adds content
    
    // Clear mesh cache — meshes belong to the old project's GPU context
    for (int i = 0; i < editor->mesh_cache_count; ++i) {
        if (editor->mesh_cache[i].mesh) {
            rev::mesh::DestroyMesh((rev::mesh::Mesh*)editor->mesh_cache[i].mesh);
            editor->mesh_cache[i].mesh = nullptr;
        }
    }
    editor->mesh_cache_count = 0;

    return true;
}

void BeginFrame(EditorContext* editor) {
    if (!editor) return;
    
    // ImGui frame start
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    
    // Update playback
    ImGuiIO& io = ImGui::GetIO();
    UpdatePlayback(editor, io.DeltaTime);
    
    // Render preview frame
    if (editor->show_preview && editor->preview_initialized) {
        RenderPreviewFrame(editor);
    }
}

void RenderUI(EditorContext* editor) {
    if (!editor) return;
    
    // Create dockspace over main viewport
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                     ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    
    ImGui::Begin("DockSpace", nullptr, window_flags);
    ImGui::PopStyleVar(3);
    
    // Create the actual dockspace
    ImGuiID dockspace_id = ImGui::GetID("MainDockSpace");
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
    
    ImGui::End();
    
    RenderMenuBar(editor);
    
    // Handle keyboard shortcuts
    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        // Ctrl+S - Save
        const char* path = (editor->project->project_path[0] != '\0') 
            ? editor->project->project_path 
            : "project.json";
        if (SaveProject(editor, path)) {
            strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), 
                     "Project saved successfully!", _TRUNCATE);
            editor->build_status_timer = 3.0f;
        }
    }
    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) {
        // Ctrl+O - Open with dialog
        OPENFILENAMEA ofn = {};
        char filepath[260] = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = (HWND)editor->window->hwnd;
        ofn.lpstrFile = filepath;
        ofn.nMaxFile = sizeof(filepath);
        ofn.lpstrFilter = "JSON Files\0*.json\0All Files\0*.*\0";
        ofn.nFilterIndex = 1;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
        
        if (GetOpenFileNameA(&ofn)) {
            if (LoadProject(editor, filepath)) {
                strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), 
                         "Project loaded!", _TRUNCATE);
                editor->build_status_timer = 3.0f;
            }
        }
    }
    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N, false)) {
        // Ctrl+N - New
        NewProject(editor);
        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), 
                 "New project created", _TRUNCATE);
        editor->build_status_timer = 3.0f;
    }
    
    if (editor->show_timeline) {
        RenderTimeline(editor);
    }
    
    if (editor->show_properties) {
        RenderProperties(editor);
    }
    
    if (editor->show_curve_editor) {
        RenderCurveEditor(editor);
    }
    
    if (editor->show_asset_browser) {
        RenderAssetBrowser(editor);
    }
    
    if (editor->shader_modal_open || editor->shader_modal_request_open) {
        RenderShaderModal(editor);
    }
    
    if (editor->music_modal_open || editor->music_modal_request_open) {
        RenderMusicModal(editor);
    }
    
    if (editor->image_modal_open || editor->image_modal_request_open) {
        RenderImageModal(editor);
    }
    
    if (editor->text_modal_open || editor->text_modal_request_open) {
        RenderTextModal(editor);
    }

    if (editor->scroll_text_modal_open || editor->scroll_text_modal_request_open) {
        RenderScrollTextModal(editor);
    }

    if (editor->mesh_modal_open || editor->mesh_modal_request_open) {
        RenderMeshModal(editor);
    }
    
    if (editor->curve_editor_modal_open || editor->curve_editor_modal_request_open) {
        RenderCurveEditorModal(editor);
    }
    
    if (editor->show_preview) {
        RenderPreviewPanel(editor);
    }
    
    if (editor->show_demo) {
        // ImGui::ShowDemoWindow(&editor->show_demo);  // Requires imgui_demo.cpp
    }
    
    // Show build status notification
    if (editor->build_status_timer > 0.0f) {
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 window_pos = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y - 50.0f);
        ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowBgAlpha(0.9f);
        
        if (ImGui::Begin("BuildStatus", nullptr, 
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | 
            ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("%s", editor->build_status_message);
            ImGui::End();
        }
        
        editor->build_status_timer -= io.DeltaTime;
    }
}

void EndFrame(EditorContext* editor) {
    if (!editor) return;
    
    // ImGui frame end and render
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// Returns the workspace-root-relative path for the cues file next to the project:
//   intros/testintro/cues.txt
// Returns false if the project hasn't been saved yet (no workspace_path).
static bool GetProjectCuesPath(EditorContext* editor, char* out, size_t out_size) {
    if (!editor->project->workspace_path[0] || !editor->project->project_path[0])
        return false;
    // Use startup_dir (not GetCurrentDirectoryA) so Windows file-dialog CWD
    // mutations cannot corrupt the result.
    const char* cwd = editor->startup_dir;
    size_t cwd_len = strlen(cwd);
    const char* wp = editor->project->workspace_path;
    char rel_dir[512] = {};
    if (cwd_len > 0 && _strnicmp(wp, cwd, cwd_len) == 0 &&
        (wp[cwd_len] == '\\' || wp[cwd_len] == '/' || wp[cwd_len] == '\0')) {
        if (wp[cwd_len] == '\0')
            strncpy_s(rel_dir, ".", sizeof(rel_dir) - 1);
        else
            strncpy_s(rel_dir, wp + cwd_len + 1, sizeof(rel_dir) - 1);
    } else {
        strncpy_s(rel_dir, wp, sizeof(rel_dir) - 1);
    }
    for (char* p = rel_dir; *p; ++p) if (*p == '\\') *p = '/';
    snprintf(out, out_size, "%s/cues.txt", rel_dir);
    return true;
}

void RenderMenuBar(EditorContext* editor) {
    if (!editor) return;
    
    // ImGui menu bar
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New", "Ctrl+N")) { 
                NewProject(editor); 
            }
            if (ImGui::MenuItem("Open", "Ctrl+O")) { 
                // Win32 file dialog
                OPENFILENAMEA ofn = {};
                char filepath[260] = {};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = (HWND)editor->window->hwnd;
                ofn.lpstrFile = filepath;
                ofn.nMaxFile = sizeof(filepath);
                ofn.lpstrFilter = "JSON Files\0*.json\0All Files\0*.*\0";
                ofn.nFilterIndex = 1;
                ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
                
                if (GetOpenFileNameA(&ofn)) {
                    if (LoadProject(editor, filepath)) {
                        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), 
                                 "Project loaded!", _TRUNCATE);
                        editor->build_status_timer = 3.0f;
                    } else {
                        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), 
                                 "Load failed!", _TRUNCATE);
                        editor->build_status_timer = 5.0f;
                    }
                }
            }
            if (ImGui::MenuItem("Import from cues.txt")) {
                // Win32 file dialog for cues.txt
                OPENFILENAMEA ofn = {};
                char filepath[260] = {};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = (HWND)editor->window->hwnd;
                ofn.lpstrFile = filepath;
                ofn.nMaxFile = sizeof(filepath);
                ofn.lpstrFilter = "Cues Files\0*.txt\0All Files\0*.*\0";
                ofn.nFilterIndex = 1;
                ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
                
                if (GetOpenFileNameA(&ofn)) {
                    if (ImportFromCues(editor, filepath)) {
                        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), 
                                 "Imported from cues.txt!", _TRUNCATE);
                        editor->build_status_timer = 3.0f;
                    } else {
                        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), 
                                 "Import failed!", _TRUNCATE);
                        editor->build_status_timer = 5.0f;
                    }
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save", "Ctrl+S")) { 
                const char* path = (editor->project->project_path[0] != '\0') 
                    ? editor->project->project_path 
                    : "project.json";
                if (SaveProject(editor, path)) {
                    strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), 
                             "Project saved successfully!", _TRUNCATE);
                    editor->build_status_timer = 3.0f;
                } else {
                    strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), 
                             "Failed to save project!", _TRUNCATE);
                    editor->build_status_timer = 5.0f;
                }
            }
            if (ImGui::MenuItem("Save As")) { 
                // Win32 save dialog
                OPENFILENAMEA ofn = {};
                char filepath[260] = "project.json";
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = (HWND)editor->window->hwnd;
                ofn.lpstrFile = filepath;
                ofn.nMaxFile = sizeof(filepath);
                ofn.lpstrFilter = "JSON Files\0*.json\0All Files\0*.*\0";
                ofn.nFilterIndex = 1;
                ofn.lpstrDefExt = "json";
                ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
                
                if (GetSaveFileNameA(&ofn)) {
                    if (SaveProject(editor, filepath)) {
                        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), 
                                 "Saved!", _TRUNCATE);
                        editor->build_status_timer = 3.0f;
                    } else {
                        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), 
                                 "Save failed!", _TRUNCATE);
                        editor->build_status_timer = 5.0f;
                    }
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) { editor->window->should_close = true; }
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Timeline", nullptr, &editor->show_timeline);
            ImGui::MenuItem("Properties", nullptr, &editor->show_properties);
            ImGui::MenuItem("Curve Editor", nullptr, &editor->show_curve_editor);
            ImGui::MenuItem("Asset Browser", nullptr, &editor->show_asset_browser);
            ImGui::MenuItem("Preview", nullptr, &editor->show_preview);
            ImGui::Separator();
            ImGui::MenuItem("ImGui Demo", nullptr, &editor->show_demo);
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Build")) {
            if (ImGui::MenuItem("Export Project")) {
                char cues_path[512] = {};
                if (GetProjectCuesPath(editor, cues_path, sizeof(cues_path)))
                    ExportProject(editor, cues_path);
                else {
                    strncpy_s(editor->build_status_message, sizeof(editor->build_status_message),
                             "Save the project first!", _TRUNCATE);
                    editor->build_status_timer = 4.0f;
                }
            }
            if (ImGui::MenuItem("Build and Run", "F5")) { 
                BuildAndRun(editor); 
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Pack, Build and Run")) {
                PackBuildAndRun(editor);
            }
            ImGui::EndMenu();
        }
        
        ImGui::EndMainMenuBar();
    }
}

// Panel and modal implementations moved to editor_panels.cpp and editor_modals.cpp

bool ImportFromCues(EditorContext* editor, const char* cues_path) {
    if (!editor || !cues_path) return false;
    
    FILE* f = nullptr;
    fopen_s(&f, cues_path, "r");
    if (!f) return false;
    
    printf("[ImportFromCues] Reading %s\n", cues_path);
    
    // Clear existing project and create a default scene
    NewProject(editor);
    
    char line[1024];
    enum Section { NONE, SHADER_CUES, IMAGE_CUES, TEXT_CUES, SCROLL_TEXT_CUES, MUSIC_CUES, CURVES, METADATA };
    Section current_section = NONE;
    
    float total_duration = 10.0f; // Default
    int intro_loop_setting = 0;
    int music_loop_setting = 0;
    
    while (fgets(line, sizeof(line), f)) {
        // Trim whitespace
        char* start = line;
        while (*start == ' ' || *start == '\t') start++;
        if (*start == '\n' || *start == '\r' || *start == '\0' || *start == '#') continue;
        
        // Section detection
        if (strstr(start, "[shader_cues]")) { current_section = SHADER_CUES; continue; }
        if (strstr(start, "[image_cues]")) { current_section = IMAGE_CUES; continue; }
        if (strstr(start, "[text_cues]")) { current_section = TEXT_CUES; continue; }
        if (strstr(start, "[scroll_text_cues]")) { current_section = SCROLL_TEXT_CUES; continue; }
        if (strstr(start, "[music_cues]")) { current_section = MUSIC_CUES; continue; }
        if (strstr(start, "[curves]")) { current_section = CURVES; continue; }
        if (strstr(start, "[metadata]")) { current_section = METADATA; continue; }
        
        // Parse metadata
        if (current_section == METADATA) {
            if (sscanf_s(start, "total_duration=%f", &total_duration) == 1) {
                printf("[ImportFromCues] Total duration: %.2f\n", total_duration);
            } else if (sscanf_s(start, "intro_loop=%d", &intro_loop_setting) == 1) {
                // parsed below
            } else if (sscanf_s(start, "music_loop=%d", &music_loop_setting) == 1) {
                // parsed below
            }
            continue;
        }
        
        // Parse shader cues
        if (current_section == SHADER_CUES) {
            ShaderCue cue = {};
            int shader_id;
            float abs_start, abs_end;
            
            int parsed = sscanf_s(start, "%d|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%d|%f|%d|%d",
                &shader_id,
                &cue.palette_low.r, &cue.palette_low.g, &cue.palette_low.b,
                &cue.palette_mid.r, &cue.palette_mid.g, &cue.palette_mid.b,
                &cue.palette_high.r, &cue.palette_high.g, &cue.palette_high.b,
                &cue.speed, &cue.intensity, &cue.warp,
                &cue.exposure_base, &cue.exposure_ramp,
                &cue.fade_base, &cue.fade_ramp,
                &abs_start, &abs_end, &cue.fade_in, &cue.fade_out,
                &cue.layer_role, &cue.opacity, &cue.blend_mode, &cue.layer_order
            );
            
            if (parsed >= 18) { // At least basic params
                cue.shader_scene_id = shader_id;
                cue.cue_start = abs_start;
                cue.cue_end = abs_end;
                
                // Set shader name based on ID
                const char* preset_name = "Unknown";
                for (int i = 0; i < 10; i++) {
                    if (g_shader_presets[i].id == shader_id) {
                        preset_name = g_shader_presets[i].name;
                        break;
                    }
                }
                strncpy_s(cue.shader_name, sizeof(cue.shader_name), preset_name, _TRUNCATE);
                
                // Create scene if needed (use total duration)
                if (editor->project->scene_count == 0) {
                    AddScene(editor, "Imported Scene", total_duration);
                }
                
                SceneBlock* scene = &editor->project->scenes[0];
                AddShaderCue(scene, cue);
                
                printf("[ImportFromCues] Imported shader cue: id=%d name='%s' %.2f-%.2f\n",
                       shader_id, cue.shader_name, abs_start, abs_end);
            }
            continue;
        }
        
        // Parse image cues: ...|layer_order|effect_type|fade_in_start|fade_in_end|fade_out_start|fade_out_end|blend_mode(optional)
        if (current_section == IMAGE_CUES) {
            char* p1 = strchr(start, '|');
            if (!p1) continue;
            ImageCue cue = {};
            size_t key_len = (size_t)(p1 - start);
            if (key_len >= sizeof(cue.asset_key)) key_len = sizeof(cue.asset_key) - 1;
            strncpy_s(cue.asset_key, start, key_len);
            char* p2 = strchr(p1 + 1, '|'); // skip asset_path field
            if (!p2) continue;
            float abs_start = 0.0f, abs_end = 0.0f;
            int parsed = sscanf_s(p2 + 1, "%f|%f|%f|%f|%f|%f|%d|%d|%f|%f|%f|%f|%d",
                &cue.x, &cue.y, &cue.scale, &cue.opacity,
                &abs_start, &abs_end, &cue.layer_order,
                &cue.effect_type, &cue.fade_in_start, &cue.fade_in_end, &cue.fade_out_start, &cue.fade_out_end,
                &cue.blend_mode
            );
            if (parsed >= 7) {
                if (parsed < 13) cue.blend_mode = 0;
                cue.cue_start = abs_start;
                cue.cue_end = abs_end;
                if (editor->project->scene_count == 0)
                    AddScene(editor, "Imported Scene", total_duration);
                SceneBlock* scene = &editor->project->scenes[0];
                AddImageCue(scene, cue);
                printf("[ImportFromCues] Imported image cue: %s\n", cue.asset_key);
            }
            continue;
        }

        // Parse text cues: ...|layer_order|blend_mode|curve_x|curve_y|curve_size|curve_color_r|curve_color_g|curve_color_b|bake_mode|baked_asset_key|baked_asset_path(optional)
        if (current_section == TEXT_CUES) {
            char* p1 = strchr(start, '|');
            if (!p1) continue;
            TextCue cue = {};
            size_t text_len = (size_t)(p1 - start);
            if (text_len >= sizeof(cue.text)) text_len = sizeof(cue.text) - 1;
            char encoded_text[256] = {};
            strncpy_s(encoded_text, sizeof(encoded_text), start, text_len);
            JsonUnescapeString(encoded_text, cue.text, sizeof(cue.text));
            char* p2 = strchr(p1 + 1, '|');
            if (!p2) continue;
            size_t font_len = (size_t)(p2 - (p1 + 1));
            if (font_len >= sizeof(cue.font_name)) font_len = sizeof(cue.font_name) - 1;
            strncpy_s(cue.font_name, p1 + 1, font_len);
            float size_f = 0.0f;
            char baked_key[64] = {};
            char baked_path[512] = {};
            int blend_mode = 0;
            int bake_mode = 0;
            bool parsed_with_bake_mode = true;
            bool parsed_with_blend_mode = true;
            int parsed = sscanf_s(p2 + 1, "%f|%f|%f|%f|%f|%f|%d|%f|%f|%f|%f|%f|%f|%d|%d|%*d|%*d|%*d|%*d|%*d|%*d|%d|%63[^|]|%511[^|\r\n]",
                &cue.x, &cue.y, &size_f,
                &cue.color.r, &cue.color.g, &cue.color.b,
                &cue.effect_type, &cue.cue_start, &cue.cue_end,
                &cue.fade_in_start, &cue.fade_in_end, &cue.fade_out_start, &cue.fade_out_end,
                &cue.layer_order,
                &blend_mode,
                &bake_mode,
                baked_key, (unsigned)_countof(baked_key),
                baked_path, (unsigned)_countof(baked_path));
            if (parsed < 16) {
                // Backward compatibility: older exports had no blend_mode column.
                parsed_with_blend_mode = false;
                parsed = sscanf_s(p2 + 1, "%f|%f|%f|%f|%f|%f|%d|%f|%f|%f|%f|%f|%f|%d|%*d|%*d|%*d|%*d|%*d|%*d|%d|%63[^|]|%511[^|\r\n]",
                    &cue.x, &cue.y, &size_f,
                    &cue.color.r, &cue.color.g, &cue.color.b,
                    &cue.effect_type, &cue.cue_start, &cue.cue_end,
                    &cue.fade_in_start, &cue.fade_in_end, &cue.fade_out_start, &cue.fade_out_end,
                    &cue.layer_order,
                    &bake_mode,
                    baked_key, (unsigned)_countof(baked_key),
                    baked_path, (unsigned)_countof(baked_path));
            }
            if (parsed < 16) {
                // Backward compatibility: older exports had no bake_mode column.
                parsed_with_bake_mode = false;
                parsed = sscanf_s(p2 + 1, "%f|%f|%f|%f|%f|%f|%d|%f|%f|%f|%f|%f|%f|%d|%*d|%*d|%*d|%*d|%*d|%*d|%63[^|]|%511[^|\r\n]",
                    &cue.x, &cue.y, &size_f,
                    &cue.color.r, &cue.color.g, &cue.color.b,
                    &cue.effect_type, &cue.cue_start, &cue.cue_end,
                    &cue.fade_in_start, &cue.fade_in_end, &cue.fade_out_start, &cue.fade_out_end,
                    &cue.layer_order,
                    baked_key, (unsigned)_countof(baked_key),
                    baked_path, (unsigned)_countof(baked_path));
                bake_mode = 0;
            }
            if (parsed >= 9) {
                cue.size = size_f;
                cue.blend_mode = (parsed_with_blend_mode && parsed >= 15) ? blend_mode : 0;
                cue.bake_mode = (parsed_with_bake_mode && parsed >= (parsed_with_blend_mode ? 16 : 15)) ? bake_mode : 0;
                if (parsed_with_bake_mode) {
                    if (parsed >= (parsed_with_blend_mode ? 17 : 16)) strncpy_s(cue.baked_asset_key, baked_key, _TRUNCATE);
                    if (parsed >= (parsed_with_blend_mode ? 18 : 17)) strncpy_s(cue.baked_asset_path, baked_path, _TRUNCATE);
                } else {
                    if (parsed >= (parsed_with_blend_mode ? 16 : 15)) strncpy_s(cue.baked_asset_key, baked_key, _TRUNCATE);
                    if (parsed >= (parsed_with_blend_mode ? 17 : 16)) strncpy_s(cue.baked_asset_path, baked_path, _TRUNCATE);
                }
                if (editor->project->scene_count == 0)
                    AddScene(editor, "Imported Scene", total_duration);
                AddTextCue(&editor->project->scenes[0], cue);
                printf("[ImportFromCues] Imported text cue: %s\n", cue.text);
            }
            continue;
        }

        // Parse scroll text cues:
        // text|font_name|x|y|size|color_r|color_g|color_b|cue_start|cue_end|fade_in_start|fade_in_end|fade_out_start|fade_out_end|layer_order|blend_mode|style_id|direction|speed|spacing|wave_amp|wave_freq|glow|opacity|wrap_gap|slant_deg|jitter_amp|jitter_freq|shadow|outline|curve_x|curve_y|curve_speed|curve_size|curve_opacity|curve_color_r|curve_color_g|curve_color_b|curve_wave_amp|curve_wave_freq|curve_jitter_amp|curve_jitter_freq|loop_mode|chroma_shift|distortion|bake_mode|baked_asset_key|baked_asset_path
        if (current_section == SCROLL_TEXT_CUES) {
            char* p1 = strchr(start, '|');
            if (!p1) continue;
            ScrollTextCue cue = {};
            cue.opacity = 1.0f;
            cue.size = 40.0f;
            cue.speed = 0.25f;
            cue.wrap_gap = 0.2f;
            cue.spacing = 1.0f;
            cue.wave_freq = 1.0f;
            cue.jitter_freq = 1.0f;
            cue.curve_x = cue.curve_y = cue.curve_speed = cue.curve_size = cue.curve_opacity = -1;
            cue.curve_color_r = cue.curve_color_g = cue.curve_color_b = -1;
            cue.curve_wave_amp = cue.curve_wave_freq = cue.curve_jitter_amp = cue.curve_jitter_freq = -1;

            size_t text_len = (size_t)(p1 - start);
            if (text_len >= sizeof(cue.text)) text_len = sizeof(cue.text) - 1;
            char encoded_text[512] = {};
            strncpy_s(encoded_text, sizeof(encoded_text), start, text_len);
            JsonUnescapeString(encoded_text, cue.text, sizeof(cue.text));

            char* p2 = strchr(p1 + 1, '|');
            if (!p2) continue;
            size_t font_len = (size_t)(p2 - (p1 + 1));
            if (font_len >= sizeof(cue.font_name)) font_len = sizeof(cue.font_name) - 1;
            strncpy_s(cue.font_name, p1 + 1, font_len);

            char baked_key[64] = {};
            char baked_path[512] = {};

            int parsed = sscanf_s(p2 + 1,
                "%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%d|%d|%d|%d|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%f|%f|%d|%63[^|]|%511[^|\r\n]",
                &cue.x, &cue.y, &cue.size,
                &cue.color.r, &cue.color.g, &cue.color.b,
                &cue.cue_start, &cue.cue_end,
                &cue.fade_in_start, &cue.fade_in_end, &cue.fade_out_start, &cue.fade_out_end,
                &cue.layer_order, &cue.blend_mode, &cue.style_id, &cue.direction,
                &cue.speed, &cue.spacing, &cue.wave_amp, &cue.wave_freq,
                &cue.glow, &cue.opacity, &cue.wrap_gap, &cue.slant_deg,
                &cue.jitter_amp, &cue.jitter_freq, &cue.shadow, &cue.outline,
                &cue.curve_x, &cue.curve_y, &cue.curve_speed, &cue.curve_size, &cue.curve_opacity,
                &cue.curve_color_r, &cue.curve_color_g, &cue.curve_color_b,
                &cue.curve_wave_amp, &cue.curve_wave_freq, &cue.curve_jitter_amp, &cue.curve_jitter_freq,
                &cue.loop_mode, &cue.chroma_shift, &cue.distortion,
                &cue.bake_mode,
                baked_key, (unsigned)_countof(baked_key),
                baked_path, (unsigned)_countof(baked_path));

            if (parsed >= 45) strncpy_s(cue.baked_asset_key, baked_key, _TRUNCATE);
            if (parsed >= 46) strncpy_s(cue.baked_asset_path, baked_path, _TRUNCATE);

            if (parsed < 44) {
                parsed = sscanf_s(p2 + 1,
                    "%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%d|%d|%d|%d|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%d|%d|%d|%d|%d|%d|%d|%d|%d|%f|%f|%d|%63[^|]|%511[^|\r\n]",
                    &cue.x, &cue.y, &cue.size,
                    &cue.color.r, &cue.color.g, &cue.color.b,
                    &cue.cue_start, &cue.cue_end,
                    &cue.fade_in_start, &cue.fade_in_end, &cue.fade_out_start, &cue.fade_out_end,
                    &cue.layer_order, &cue.blend_mode, &cue.style_id, &cue.direction,
                    &cue.speed, &cue.spacing, &cue.wave_amp, &cue.wave_freq,
                    &cue.glow, &cue.opacity, &cue.wrap_gap, &cue.slant_deg,
                    &cue.jitter_amp, &cue.jitter_freq, &cue.shadow, &cue.outline,
                    &cue.curve_x, &cue.curve_y, &cue.curve_speed, &cue.curve_size, &cue.curve_opacity,
                    &cue.curve_color_r, &cue.curve_color_g, &cue.curve_color_b,
                    &cue.loop_mode, &cue.chroma_shift, &cue.distortion,
                    &cue.bake_mode,
                    baked_key, (unsigned)_countof(baked_key),
                    baked_path, (unsigned)_countof(baked_path));
                if (parsed >= 40) strncpy_s(cue.baked_asset_key, baked_key, _TRUNCATE);
                if (parsed >= 41) strncpy_s(cue.baked_asset_path, baked_path, _TRUNCATE);
            }

            if (parsed < 39) {
                parsed = sscanf_s(p2 + 1,
                    "%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%d|%d|%d|%d|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%d|%d|%d|%d|%d|%d|%d|%d|%d|%f|%f",
                    &cue.x, &cue.y, &cue.size,
                    &cue.color.r, &cue.color.g, &cue.color.b,
                    &cue.cue_start, &cue.cue_end,
                    &cue.fade_in_start, &cue.fade_in_end, &cue.fade_out_start, &cue.fade_out_end,
                    &cue.layer_order, &cue.blend_mode, &cue.style_id, &cue.direction,
                    &cue.speed, &cue.spacing, &cue.wave_amp, &cue.wave_freq,
                    &cue.glow, &cue.opacity, &cue.wrap_gap, &cue.slant_deg,
                    &cue.jitter_amp, &cue.jitter_freq, &cue.shadow, &cue.outline,
                    &cue.curve_x, &cue.curve_y, &cue.curve_speed, &cue.curve_size, &cue.curve_opacity,
                    &cue.curve_color_r, &cue.curve_color_g, &cue.curve_color_b,
                    &cue.loop_mode, &cue.chroma_shift, &cue.distortion);
            }

            if (parsed >= 8) {
                if (editor->project->scene_count == 0) {
                    AddScene(editor, "Imported Scene", total_duration);
                }
                AddScrollTextCue(&editor->project->scenes[0], cue);
                printf("[ImportFromCues] Imported scroll text cue: %s\n", cue.text);
            }
            continue;
        }

        // Parse music cues: asset_key|asset_path|cue_start|cue_end
        if (current_section == MUSIC_CUES) {
            MusicCue cue = {};
            char* p1 = strchr(start, '|');
            if (p1) {
                size_t key_len = (size_t)(p1 - start);
                if (key_len >= sizeof(cue.asset_key)) key_len = sizeof(cue.asset_key) - 1;
                strncpy_s(cue.asset_key, start, key_len);
                char* p2 = strchr(p1 + 1, '|');
                if (p2) {
                    size_t path_len = (size_t)(p2 - (p1 + 1));
                    if (path_len >= sizeof(cue.asset_path)) path_len = sizeof(cue.asset_path) - 1;
                    strncpy_s(cue.asset_path, p1 + 1, path_len);
                    if (sscanf_s(p2 + 1, "%f|%f", &cue.cue_start, &cue.cue_end) >= 1) {
                        if (editor->project->scene_count == 0)
                            AddScene(editor, "Imported Scene", total_duration);
                        AddMusicCue(&editor->project->scenes[0], cue);
                        printf("[ImportFromCues] Imported music cue: %s path=%s\n",
                               cue.asset_key, cue.asset_path);
                    }
                }
            }
            continue;
        }
    }

    fclose(f);
    
    // Update project metadata
    if (editor->project) {
        editor->project->total_duration = total_duration;
        editor->project->loop_intro = (intro_loop_setting != 0);
        editor->project->loop_music = (music_loop_setting != 0);
    }
    
    printf("[ImportFromCues] Import complete!\n");
    return true;
}

bool ExportProject(EditorContext* editor, const char* output_path) {
    if (!editor || !output_path) return false;

    FILE* f = nullptr;
    fopen_s(&f, output_path, "w");
    if (!f) return false;

    // [shader_cues] section
    fprintf(f, "[shader_cues]\n");
    fprintf(f, "# shader_scene_id|palette_low_r|palette_low_g|palette_low_b|palette_mid_r|palette_mid_g|palette_mid_b|palette_high_r|palette_high_g|palette_high_b|speed|intensity|warp|exposure_base|exposure_ramp|fade_base|fade_ramp|cue_start|cue_end|fade_in|fade_out|layer_role|opacity|blend_mode|layer_order|curve_speed|curve_intensity|curve_warp|curve_exposure|curve_fade|curve_palette_low_r|curve_palette_low_g|curve_palette_low_b|curve_palette_mid_r|curve_palette_mid_g|curve_palette_mid_b|curve_palette_high_r|curve_palette_high_g|curve_palette_high_b|curve_opacity|curve_exposure_ramp|curve_fade_ramp\n");
    
    // Collect all shader cues from all scenes
    int shader_cue_id = 0;
    for (int scene_idx = 0; scene_idx < editor->project->scene_count; ++scene_idx) {
        SceneBlock* scene = &editor->project->scenes[scene_idx];
        float scene_start = 0.0f;
        
        // Calculate scene start time
        for (int i = 0; i < scene_idx; ++i) {
            scene_start += editor->project->scenes[i].duration;
        }
        
        for (int cue_idx = 0; cue_idx < scene->shader_cue_count; ++cue_idx) {
            ShaderCue* cue = &scene->shader_cues[cue_idx];
            
            // Convert scene-relative times to absolute times
            float abs_start = scene_start + cue->cue_start;
            float abs_end = (cue->cue_end < 0.0f) ? (scene_start + scene->duration) : (scene_start + cue->cue_end);
            
            fprintf(f, "%d|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%d|%.3f|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d\n",
                cue->shader_scene_id,
                cue->palette_low.r, cue->palette_low.g, cue->palette_low.b,
                cue->palette_mid.r, cue->palette_mid.g, cue->palette_mid.b,
                cue->palette_high.r, cue->palette_high.g, cue->palette_high.b,
                cue->speed, cue->intensity, cue->warp,
                cue->exposure_base, cue->exposure_ramp,
                cue->fade_base, cue->fade_ramp,
                abs_start, abs_end, cue->fade_in, cue->fade_out,
                cue->layer_role, cue->opacity, cue->blend_mode, cue->layer_order,
                cue->curve_speed, cue->curve_intensity, cue->curve_warp,
                cue->curve_exposure, cue->curve_fade,
                cue->curve_palette_low_r, cue->curve_palette_low_g, cue->curve_palette_low_b,
                cue->curve_palette_mid_r, cue->curve_palette_mid_g, cue->curve_palette_mid_b,
                cue->curve_palette_high_r, cue->curve_palette_high_g, cue->curve_palette_high_b,
                cue->curve_opacity, cue->curve_exposure_ramp, cue->curve_fade_ramp
            );
            
            shader_cue_id++;
        }
    }
    
    fprintf(f, "\n");
    
    // [image_cues] section
    fprintf(f, "[image_cues]\n");
    fprintf(f, "# asset_key|asset_path|x|y|scale|opacity|cue_start|cue_end|layer_order|effect_type|fade_in_start|fade_in_end|fade_out_start|fade_out_end|curve_x|curve_y|curve_scale|curve_opacity|blend_mode\n");
    
    // Compute workspace-root-relative prefix for asset paths once.
    // assets_path is absolute (e.g. E:\himym\intros\test\test_assets).
    // cwd is always the workspace root (e.g. E:\himym).
    // We need the forward-slash relative form: intros/test/test_assets
    char rel_assets_prefix[512] = {};
    {
        size_t cwd_len = strlen(editor->startup_dir);
        const char* ap = editor->project->assets_path;
        if (cwd_len > 0 && _strnicmp(ap, editor->startup_dir, cwd_len) == 0 &&
            (ap[cwd_len] == '\\' || ap[cwd_len] == '/')) {
            strncpy_s(rel_assets_prefix, ap + cwd_len + 1, sizeof(rel_assets_prefix) - 1);
        } else {
            // assets_path not under cwd — use just project_name_assets as fallback
            const char* proj_path = editor->project->project_path;
            const char* fn = strrchr(proj_path, '\\');
            if (!fn) fn = strrchr(proj_path, '/');
            fn = fn ? fn + 1 : proj_path;
            char pname[256] = {};
            strncpy_s(pname, fn, sizeof(pname) - 1);
            char* dot = strrchr(pname, '.');
            if (dot) *dot = '\0';
            snprintf(rel_assets_prefix, sizeof(rel_assets_prefix), "%s_assets", pname);
        }
        // normalise to forward slashes
        for (char* p = rel_assets_prefix; *p; ++p) if (*p == '\\') *p = '/';
    }

    for (int scene_idx = 0; scene_idx < editor->project->scene_count; ++scene_idx) {
        SceneBlock* scene = &editor->project->scenes[scene_idx];
        float scene_start = 0.0f;
        
        for (int i = 0; i < scene_idx; ++i) {
            scene_start += editor->project->scenes[i].duration;
        }
        
        for (int cue_idx = 0; cue_idx < scene->image_cue_count; ++cue_idx) {
            ImageCue* cue = &scene->image_cues[cue_idx];
            float abs_start = scene_start + cue->cue_start;
            float abs_end = (cue->cue_end < 0.0f) ? (scene_start + scene->duration) : (scene_start + cue->cue_end);
            float abs_fade_in_start  = scene_start + cue->fade_in_start;
            float abs_fade_in_end    = scene_start + cue->fade_in_end;
            float abs_fade_out_start = scene_start + cue->fade_out_start;
            float abs_fade_out_end   = scene_start + cue->fade_out_end;
            
            // Construct workspace-root-relative path: rel_assets_prefix/asset_key
            char full_path[640];
            snprintf(full_path, sizeof(full_path), "%s/%s", rel_assets_prefix, cue->asset_key);
            
            fprintf(f, "%s|%s|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%d|%d|%.3f|%.3f|%.3f|%.3f|%d|%d|%d|%d|%d\n",
                cue->asset_key, full_path, cue->x, cue->y, cue->scale, cue->opacity,
                abs_start, abs_end, cue->layer_order,
                cue->effect_type, abs_fade_in_start, abs_fade_in_end, abs_fade_out_start, abs_fade_out_end,
                cue->curve_x, cue->curve_y, cue->curve_scale, cue->curve_opacity,
                cue->blend_mode
            );
        }
    }
    
    fprintf(f, "\n");
    
    // [text_cues] section
    fprintf(f, "[text_cues]\n");
    fprintf(f, "# text|font_name|x|y|size|color_r|color_g|color_b|effect_type|cue_start|cue_end|fade_in_start|fade_in_end|fade_out_start|fade_out_end|layer_order|blend_mode|curve_x|curve_y|curve_size|curve_color_r|curve_color_g|curve_color_b|bake_mode|baked_asset_key|baked_asset_path\n");
    
    for (int scene_idx = 0; scene_idx < editor->project->scene_count; ++scene_idx) {
        SceneBlock* scene = &editor->project->scenes[scene_idx];
        float scene_start = 0.0f;
        
        for (int i = 0; i < scene_idx; ++i) {
            scene_start += editor->project->scenes[i].duration;
        }
        
        for (int cue_idx = 0; cue_idx < scene->text_cue_count; ++cue_idx) {
            TextCue* cue = &scene->text_cues[cue_idx];
            float abs_start = scene_start + cue->cue_start;
            float abs_end = scene_start + cue->cue_end;
            float abs_fade_in_start  = scene_start + cue->fade_in_start;
            float abs_fade_in_end    = scene_start + cue->fade_in_end;
            float abs_fade_out_start = scene_start + cue->fade_out_start;
            float abs_fade_out_end   = scene_start + cue->fade_out_end;
            
            // Encode newlines as literal \\n for CSV compatibility
            char encoded_text[256];
            const char* src = cue->text;
            char* dst = encoded_text;
            while (*src && (dst - encoded_text) < 254) {
                if (*src == '\n') {
                    *dst++ = '\\';
                    *dst++ = 'n';
                } else {
                    *dst++ = *src;
                }
                src++;
            }
            *dst = '\0';

            // Bake static text cues to PNG so they can be packed into runtime assets.
            char baked_asset_key[64] = {};
            char baked_asset_path[512] = {};
            if ((cue->effect_type <= 2 || cue->bake_mode == 1) && cue->text[0] != '\0') {
                snprintf(baked_asset_key, sizeof(baked_asset_key), "text_s%02d_c%02d.png", scene_idx, cue_idx);

                char baked_abs_path[640] = {};
                snprintf(baked_abs_path, sizeof(baked_abs_path), "%s\\%s", editor->project->assets_path, baked_asset_key);

                if (BakeTextCueToPng(cue, baked_abs_path)) {
                    if (IsFileReadableWithRetry(baked_abs_path, 60, 25)) {
                        snprintf(baked_asset_path, sizeof(baked_asset_path), "%s/%s", rel_assets_prefix, baked_asset_key);
                        strncpy_s(cue->baked_asset_key, sizeof(cue->baked_asset_key), baked_asset_key, _TRUNCATE);
                        strncpy_s(cue->baked_asset_path, sizeof(cue->baked_asset_path), baked_asset_path, _TRUNCATE);
                    } else {
                        printf("[ExportProject] WARNING: baked text exists but is not readable yet: %s\n", baked_abs_path);
                        baked_asset_key[0] = '\0';
                        baked_asset_path[0] = '\0';
                        cue->baked_asset_key[0] = '\0';
                        cue->baked_asset_path[0] = '\0';
                    }
                } else {
                    cue->baked_asset_key[0] = '\0';
                    cue->baked_asset_path[0] = '\0';
                }
            }
            
            fprintf(f, "%s|%s|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%d|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%d|%d|%d|%d|%d|%d|%d|%d|%d|%s|%s\n",
                encoded_text, cue->font_name, cue->x, cue->y, cue->size,
                cue->color.r, cue->color.g, cue->color.b,
                cue->effect_type, abs_start, abs_end,
                abs_fade_in_start, abs_fade_in_end, abs_fade_out_start, abs_fade_out_end,
                cue->layer_order,
                cue->blend_mode,
                cue->curve_x, cue->curve_y, cue->curve_size,
                cue->curve_color_r, cue->curve_color_g, cue->curve_color_b,
                cue->bake_mode,
                baked_asset_key, baked_asset_path
            );
        }
    }
    
    fprintf(f, "\n");

    // [scroll_text_cues] section
    fprintf(f, "[scroll_text_cues]\n");
    fprintf(f, "# text|font_name|x|y|size|color_r|color_g|color_b|cue_start|cue_end|fade_in_start|fade_in_end|fade_out_start|fade_out_end|layer_order|blend_mode|style_id|direction|speed|spacing|wave_amp|wave_freq|glow|opacity|wrap_gap|slant_deg|jitter_amp|jitter_freq|shadow|outline|curve_x|curve_y|curve_speed|curve_size|curve_opacity|curve_color_r|curve_color_g|curve_color_b|curve_wave_amp|curve_wave_freq|curve_jitter_amp|curve_jitter_freq|loop_mode|chroma_shift|distortion|bake_mode|baked_asset_key|baked_asset_path\n");

    for (int scene_idx = 0; scene_idx < editor->project->scene_count; ++scene_idx) {
        SceneBlock* scene = &editor->project->scenes[scene_idx];
        float scene_start = 0.0f;
        for (int i = 0; i < scene_idx; ++i) {
            scene_start += editor->project->scenes[i].duration;
        }

        for (int cue_idx = 0; cue_idx < scene->scroll_text_cue_count; ++cue_idx) {
            ScrollTextCue* cue = &scene->scroll_text_cues[cue_idx];
            float abs_start = scene_start + cue->cue_start;
            float abs_end = scene_start + cue->cue_end;
            float abs_fade_in_start = scene_start + cue->fade_in_start;
            float abs_fade_in_end = scene_start + cue->fade_in_end;
            float abs_fade_out_start = scene_start + cue->fade_out_start;
            float abs_fade_out_end = scene_start + cue->fade_out_end;

            char encoded_text[512] = {};
            const char* src = cue->text;
            char* dst = encoded_text;
            while (*src && (dst - encoded_text) < 510) {
                if (*src == '\n') {
                    *dst++ = '\\';
                    *dst++ = 'n';
                } else {
                    *dst++ = *src;
                }
                src++;
            }
            *dst = '\0';

            char baked_asset_key[64] = {};
            char baked_asset_path[512] = {};
            if (cue->bake_mode == 1 && cue->text[0] != '\0') {
                snprintf(baked_asset_key, sizeof(baked_asset_key), "scroll_s%02d_c%02d.png", scene_idx, cue_idx);

                char baked_abs_path[640] = {};
                snprintf(baked_abs_path, sizeof(baked_abs_path), "%s\\%s", editor->project->assets_path, baked_asset_key);

                if (BakeScrollTextCueToPng(cue, baked_abs_path)) {
                    if (IsFileReadableWithRetry(baked_abs_path, 60, 25)) {
                        snprintf(baked_asset_path, sizeof(baked_asset_path), "%s/%s", rel_assets_prefix, baked_asset_key);
                        strncpy_s(cue->baked_asset_key, sizeof(cue->baked_asset_key), baked_asset_key, _TRUNCATE);
                        strncpy_s(cue->baked_asset_path, sizeof(cue->baked_asset_path), baked_asset_path, _TRUNCATE);
                    } else {
                        printf("[ExportProject] WARNING: baked scroll text exists but is not readable yet: %s\n", baked_abs_path);
                        cue->baked_asset_key[0] = '\0';
                        cue->baked_asset_path[0] = '\0';
                    }
                } else {
                    cue->baked_asset_key[0] = '\0';
                    cue->baked_asset_path[0] = '\0';
                }
            }

            if (baked_asset_key[0] == '\0' && cue->baked_asset_key[0] != '\0') {
                strncpy_s(baked_asset_key, sizeof(baked_asset_key), cue->baked_asset_key, _TRUNCATE);
            }
            if (baked_asset_path[0] == '\0' && cue->baked_asset_path[0] != '\0') {
                strncpy_s(baked_asset_path, sizeof(baked_asset_path), cue->baked_asset_path, _TRUNCATE);
            }

            fprintf(f, "%s|%s|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%d|%d|%d|%d|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%.3f|%.3f|%d|%s|%s\n",
                encoded_text, cue->font_name,
                cue->x, cue->y, cue->size,
                cue->color.r, cue->color.g, cue->color.b,
                abs_start, abs_end,
                abs_fade_in_start, abs_fade_in_end, abs_fade_out_start, abs_fade_out_end,
                cue->layer_order, cue->blend_mode, cue->style_id, cue->direction,
                cue->speed, cue->spacing, cue->wave_amp, cue->wave_freq,
                cue->glow, cue->opacity, cue->wrap_gap, cue->slant_deg,
                cue->jitter_amp, cue->jitter_freq, cue->shadow, cue->outline,
                cue->curve_x, cue->curve_y, cue->curve_speed, cue->curve_size, cue->curve_opacity,
                cue->curve_color_r, cue->curve_color_g, cue->curve_color_b,
                cue->curve_wave_amp, cue->curve_wave_freq, cue->curve_jitter_amp, cue->curve_jitter_freq,
                cue->loop_mode, cue->chroma_shift, cue->distortion,
                cue->bake_mode,
                baked_asset_key, baked_asset_path);
        }
    }

    fprintf(f, "\n");
    
    // [music_cues] section
    fprintf(f, "[music_cues]\n");
    fprintf(f, "# asset_key|asset_path|cue_start|cue_end\n");
    
    for (int scene_idx = 0; scene_idx < editor->project->scene_count; ++scene_idx) {
        SceneBlock* scene = &editor->project->scenes[scene_idx];
        float scene_start = 0.0f;
        
        for (int i = 0; i < scene_idx; ++i) {
            scene_start += editor->project->scenes[i].duration;
        }
        
        for (int cue_idx = 0; cue_idx < scene->music_cue_count; ++cue_idx) {
            MusicCue* cue = &scene->music_cues[cue_idx];
            float abs_start = scene_start + cue->cue_start;
            float abs_end = scene_start + cue->cue_end;
            
            fprintf(f, "%s|%s|%.3f|%.3f\n",
                cue->asset_key, cue->asset_path, abs_start, abs_end
            );
        }
    }
    
    fprintf(f, "\n");

    // [mesh_cues] section
    fprintf(f, "[mesh_cues]\n");
    fprintf(f, "# asset_key|asset_path|mesh_type|pos_x|pos_y|pos_z|rot_x|rot_y|rot_z|scale_x|scale_y|scale_z|color_r|color_g|color_b|color_a|mesh_size|mesh_param|cue_start|cue_end|layer_order|effect_type|fade_in_start|fade_in_end|fade_out_start|fade_out_end|metallic|roughness|curve_pos_x|curve_pos_y|curve_pos_z|curve_rot_x|curve_rot_y|curve_rot_z|curve_scale_x|curve_scale_y|curve_scale_z|curve_color_r|curve_color_g|curve_color_b|curve_color_a|curve_mesh_size|curve_metallic|curve_roughness\n");

    // Packed runtime resolves mesh assets by key. Ensure keys are unique in export,
    // even when authored cues reused defaults like "mesh_0" in multiple scenes.
    char used_mesh_keys[512][64] = {};
    int used_mesh_key_count = 0;

    for (int scene_idx = 0; scene_idx < editor->project->scene_count; ++scene_idx) {
        SceneBlock* scene = &editor->project->scenes[scene_idx];
        float scene_start = 0.0f;

        for (int i = 0; i < scene_idx; ++i) {
            scene_start += editor->project->scenes[i].duration;
        }

        for (int cue_idx = 0; cue_idx < scene->mesh_cue_count; ++cue_idx) {
            MeshCue* cue = &scene->mesh_cues[cue_idx];
            float abs_start           = scene_start + cue->cue_start;
            float abs_end             = scene_start + cue->cue_end;
            float abs_fade_in_start   = scene_start + cue->fade_in_start;
            float abs_fade_in_end     = scene_start + cue->fade_in_end;
            float abs_fade_out_start  = scene_start + cue->fade_out_start;
            float abs_fade_out_end    = scene_start + cue->fade_out_end;

            char export_asset_key[64] = {};
            if (cue->asset_key[0]) {
                strncpy_s(export_asset_key, sizeof(export_asset_key), cue->asset_key, _TRUNCATE);
            } else {
                snprintf(export_asset_key, sizeof(export_asset_key), "mesh_s%02d_c%02d", scene_idx, cue_idx);
            }

            bool duplicate_key = false;
            for (int k = 0; k < used_mesh_key_count; ++k) {
                if (strcmp(used_mesh_keys[k], export_asset_key) == 0) {
                    duplicate_key = true;
                    break;
                }
            }
            if (duplicate_key) {
                snprintf(export_asset_key, sizeof(export_asset_key), "%s_s%02d_c%02d", cue->asset_key[0] ? cue->asset_key : "mesh", scene_idx, cue_idx);
            }

            if (used_mesh_key_count < (int)(sizeof(used_mesh_keys) / sizeof(used_mesh_keys[0]))) {
                strncpy_s(used_mesh_keys[used_mesh_key_count], sizeof(used_mesh_keys[used_mesh_key_count]), export_asset_key, _TRUNCATE);
                used_mesh_key_count++;
            }

            fprintf(f, "%s|%s|%d|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%d|%d|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d\n",
                export_asset_key, cue->asset_path, cue->mesh_type,
                cue->pos[0],   cue->pos[1],   cue->pos[2],
                cue->rot[0],   cue->rot[1],   cue->rot[2],
                cue->scale[0], cue->scale[1], cue->scale[2],
                cue->color[0], cue->color[1], cue->color[2], cue->color[3],
                cue->mesh_size, cue->mesh_param,
                abs_start, abs_end, cue->layer_order, cue->effect_type,
                abs_fade_in_start, abs_fade_in_end, abs_fade_out_start, abs_fade_out_end,
                cue->metallic, cue->roughness,
                cue->curve_pos_x, cue->curve_pos_y, cue->curve_pos_z,
                cue->curve_rot_x, cue->curve_rot_y, cue->curve_rot_z,
                cue->curve_scale_x, cue->curve_scale_y, cue->curve_scale_z,
                cue->curve_color_r, cue->curve_color_g, cue->curve_color_b, cue->curve_color_a,
                cue->curve_mesh_size, cue->curve_metallic, cue->curve_roughness
            );
        }
    }

    fprintf(f, "\n");
    
    // [curves] section
    fprintf(f, "[curves]\n");
    fprintf(f, "# curve_id|wrap_mode|duration|point_count\n");
    
    for (int i = 0; i < editor->project->curve_count; ++i) {
        rev::curve::Curve* curve = &editor->project->curves[i];
        
        const char* wrap_mode_str = "clamp";
        switch (curve->wrap_mode) {
            case rev::curve::WrapMode::Clamp: wrap_mode_str = "clamp"; break;
            case rev::curve::WrapMode::Loop: wrap_mode_str = "loop"; break;
            case rev::curve::WrapMode::PingPong: wrap_mode_str = "pingpong"; break;
            case rev::curve::WrapMode::Mirror: wrap_mode_str = "mirror"; break;
        }
        
        fprintf(f, "%d|%s|%.3f|%d\n", i, wrap_mode_str, curve->duration, curve->point_count);
        
        for (int pt_idx = 0; pt_idx < curve->point_count; ++pt_idx) {
            rev::curve::Point* pt = &curve->points[pt_idx];
            
            const char* mode_str = "linear";
            switch (pt->mode) {
                case rev::curve::EaseMode::Linear: mode_str = "linear"; break;
                case rev::curve::EaseMode::EaseIn: mode_str = "ease_in"; break;
                case rev::curve::EaseMode::EaseOut: mode_str = "ease_out"; break;
                case rev::curve::EaseMode::EaseInOut: mode_str = "ease_in_out"; break;
                case rev::curve::EaseMode::Smoothstep: mode_str = "smoothstep"; break;
                case rev::curve::EaseMode::Hold: mode_str = "hold"; break;
            }
            
            fprintf(f, "  %.3f|%.3f|%.3f|%.3f|%s\n",
                pt->t, pt->v, pt->in_ease, pt->out_ease, mode_str
            );
        }
    }
    
    fprintf(f, "\n");
    
    // [metadata] section
    fprintf(f, "[metadata]\n");
    fprintf(f, "total_duration=%.3f\n", editor->project->total_duration);
    fprintf(f, "scene_count=%d\n", editor->project->scene_count);
    fprintf(f, "intro_loop=%d\n", editor->project->loop_intro ? 1 : 0);
    fprintf(f, "music_loop=%d\n", editor->project->loop_music ? 1 : 0);
    
    fclose(f);
    return true;
}

bool BuildAndRun(EditorContext* editor) {
    if (!editor) return false;

    printf("\n=== Build and Run ===\n");

    // Step 1: compute project-relative cues path
    char cues_path[512] = {};
    if (!GetProjectCuesPath(editor, cues_path, sizeof(cues_path))) {
        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Save the project first!", _TRUNCATE);
        editor->build_status_timer = 5.0f;
        return false;
    }

    // Step 2: Export to {project_dir}/cues.txt
    printf("Step 1: Exporting to %s...\n", cues_path);
    if (!ExportProject(editor, cues_path)) {
        printf("ERROR: Export failed!\n");
        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Export failed!", _TRUNCATE);
        editor->build_status_timer = 5.0f;
        return false;
    }
    printf("Export complete.\n");

    // Step 3: Build — use absolute build dir so CWD mutations don't matter
    strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Building intro...", _TRUNCATE);
    editor->build_status_timer = 5.0f;
    printf("Step 2: Building minimal_intro...\n");
    char build_cmd[768];
    snprintf(build_cmd, sizeof(build_cmd),
             "cmake --build \"%s\\build\" --config Release --target minimal_intro",
             editor->startup_dir);
    int build_result = system(build_cmd);
    if (build_result != 0) {
        printf("ERROR: Build failed with exit code %d\n", build_result);
        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Build failed! Check console for errors.", _TRUNCATE);
        editor->build_status_timer = 10.0f;
        return false;
    }
    printf("Build complete.\n");

    // Step 4: Launch — absolute exe path + cues_path as argv[1]
    strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Launching intro...", _TRUNCATE);
    editor->build_status_timer = 3.0f;
    printf("Step 3: Launching intro (%s)...\n", cues_path);
    char run_command[768];
    snprintf(run_command, sizeof(run_command),
             "start \"\" \"%s\\build\\bin\\Release\\minimal_intro.exe\" %s",
             editor->startup_dir, cues_path);
    int run_result = system(run_command);
    
    if (run_result == 0) {
        printf("Intro launched successfully!\n");
        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Intro launched successfully!", _TRUNCATE);
        editor->build_status_timer = 3.0f;
    } else {
        printf("ERROR: Failed to launch intro (exit code %d)\n", run_result);
        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Failed to launch intro.", _TRUNCATE);
        editor->build_status_timer = 5.0f;
    }
    
    return (run_result == 0);
}

bool PackBuildAndRun(EditorContext* editor) {
    if (!editor) return false;

    printf("\n=== Pack, Build and Run ===\n");

    // Step 1: compute project-relative cues path
    char cues_path[512] = {};
    if (!GetProjectCuesPath(editor, cues_path, sizeof(cues_path))) {
        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Save the project first!", _TRUNCATE);
        editor->build_status_timer = 5.0f;
        return false;
    }

    // Step 2: Export cues.txt to project directory
    strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Exporting project...", _TRUNCATE);
    editor->build_status_timer = 5.0f;
    printf("Step 1: Exporting to %s...\n", cues_path);
    if (!ExportProject(editor, cues_path)) {
        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Export failed!", _TRUNCATE);
        editor->build_status_timer = 5.0f;
        return false;
    }
    printf("Export complete.\n");

    // Step 3: Pack assets into {startup_dir}\build\packed_assets.h
    // Pack cache lives next to the project so each project tracks its own checksums.
    char pack_cache_path[512] = {};
    snprintf(pack_cache_path, sizeof(pack_cache_path), "%s/pack_cache.txt",
             editor->project->workspace_path[0] ? editor->project->workspace_path
                                                 : editor->startup_dir);
    for (char* p = pack_cache_path; *p; ++p) if (*p == '\\') *p = '/';

    char packed_header_path[512] = {};
    char build_dir[512] = {};
    snprintf(build_dir, sizeof(build_dir), "%s\\build", editor->startup_dir);
    CreateDirectoryA(build_dir, NULL);
    snprintf(packed_header_path, sizeof(packed_header_path), "%s\\packed_assets.h", build_dir);

    strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Packing assets...", _TRUNCATE);
    editor->build_status_timer = 5.0f;
    printf("Step 2: Packing assets (cache: %s)...\n", pack_cache_path);

    rev::pack::PackResult pack_result = rev::pack::PackAssets(
        cues_path,              // cues source (project-relative)
        packed_header_path,     // output header (absolute path to build dir)
        pack_cache_path,        // checksum cache next to project
        editor->startup_dir     // exported cue asset paths are workspace-root-relative
    );

    if (!pack_result.ok) {
        printf("ERROR: Packing failed: %s\n", pack_result.error);
        char msg[256];
        snprintf(msg, sizeof(msg), "Pack failed: %s", pack_result.error);
        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), msg, _TRUNCATE);
        editor->build_status_timer = 10.0f;
        return false;
    }
    printf("Pack complete: %d total, %d packed, %d skipped.\n",
           pack_result.total, pack_result.packed, pack_result.skipped);
    if (pack_result.optional_skipped > 0) {
        printf("[Pack] Warning: %d optional baked text asset(s) were skipped.\n", pack_result.optional_skipped);
    }

    // Step 3: Build the packed target — absolute build dir
    strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Building packed intro...", _TRUNCATE);
    editor->build_status_timer = 5.0f;
    printf("Step 3: Building minimal_intro_packed...\n");
    char build_cmd[768];
    snprintf(build_cmd, sizeof(build_cmd),
             "cmake --build \"%s\\build\" --config Release --target minimal_intro_packed",
             editor->startup_dir);
    int build_result = system(build_cmd);
    if (build_result != 0) {
        printf("ERROR: Build failed with exit code %d\n", build_result);
        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Build failed! Check console for errors.", _TRUNCATE);
        editor->build_status_timer = 10.0f;
        return false;
    }
    printf("Build complete.\n");

    // Step 4: Launch — absolute exe path + cues_path as argv[1]
    strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Launching packed intro...", _TRUNCATE);
    editor->build_status_timer = 3.0f;
    printf("Step 4: Launching packed intro (%s)...\n", cues_path);
    char run_command[768];
    snprintf(run_command, sizeof(run_command),
             "start \"\" \"%s\\build\\bin\\Release\\minimal_intro_packed.exe\" %s",
             editor->startup_dir, cues_path);
    int run_result = system(run_command);
    if (run_result == 0) {
        char msg[128];
        if (pack_result.optional_skipped > 0) {
            snprintf(msg, sizeof(msg), "Launched with %d baked-text warning(s)", pack_result.optional_skipped);
        } else {
            snprintf(msg, sizeof(msg), "Packed intro launched! (%d assets, %d skipped)",
                     pack_result.total, pack_result.skipped);
        }
        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), msg, _TRUNCATE);
        editor->build_status_timer = 5.0f;
        printf("Packed intro launched successfully!\n");
    } else {
        printf("ERROR: Failed to launch (exit code %d)\n", run_result);
        strncpy_s(editor->build_status_message, sizeof(editor->build_status_message), "Failed to launch packed intro.", _TRUNCATE);
        editor->build_status_timer = 5.0f;
    }

    return (run_result == 0);
}

// ===== Scene Management =====

int AddScene(EditorContext* editor, const char* name, float duration) {
    if (!editor || !name) return -1;
    
    // Resize array if needed
    if (editor->project->scene_count >= editor->project->scene_capacity) {
        int new_capacity = editor->project->scene_capacity == 0 ? 4 : editor->project->scene_capacity * 2;
        SceneBlock* new_scenes = new SceneBlock[new_capacity];
        
        // Copy existing scenes
        for (int i = 0; i < editor->project->scene_count; ++i) {
            new_scenes[i] = editor->project->scenes[i];
        }
        
        delete[] editor->project->scenes;
        editor->project->scenes = new_scenes;
        editor->project->scene_capacity = new_capacity;
    }
    
    // Initialize new scene
    int index = editor->project->scene_count++;
    SceneBlock* scene = &editor->project->scenes[index];
    
    strncpy_s(scene->name, sizeof(scene->name), name, _TRUNCATE);
    scene->duration = duration;
    
    scene->shader_cues = nullptr;
    scene->shader_cue_count = 0;
    scene->shader_cue_capacity = 0;
    
    scene->image_cues = nullptr;
    scene->image_cue_count = 0;
    scene->image_cue_capacity = 0;
    
    scene->text_cues = nullptr;
    scene->text_cue_count = 0;
    scene->text_cue_capacity = 0;

    scene->scroll_text_cues = nullptr;
    scene->scroll_text_cue_count = 0;
    scene->scroll_text_cue_capacity = 0;
    
    scene->music_cues = nullptr;
    scene->music_cue_count = 0;
    scene->music_cue_capacity = 0;

    scene->mesh_cues = nullptr;
    scene->mesh_cue_count = 0;
    scene->mesh_cue_capacity = 0;

    // Update total duration
    editor->project->total_duration += duration;
    editor->project->modified = true;
    
    return index;
}

void DeleteScene(EditorContext* editor, int scene_index) {
    if (!editor || scene_index < 0 || scene_index >= editor->project->scene_count) return;
    
    SceneBlock* scene = &editor->project->scenes[scene_index];
    
    // Update total duration
    editor->project->total_duration -= scene->duration;
    
    // Free scene resources
    delete[] scene->shader_cues;
    delete[] scene->image_cues;
    delete[] scene->text_cues;
    delete[] scene->scroll_text_cues;
    delete[] scene->music_cues;
    delete[] scene->mesh_cues;
    
    // Shift remaining scenes
    for (int i = scene_index; i < editor->project->scene_count - 1; ++i) {
        editor->project->scenes[i] = editor->project->scenes[i + 1];
    }
    
    editor->project->scene_count--;
    editor->project->modified = true;
}

void MoveScene(EditorContext* editor, int from_index, int to_index) {
    if (!editor || from_index < 0 || from_index >= editor->project->scene_count) return;
    if (to_index < 0 || to_index >= editor->project->scene_count) return;
    if (from_index == to_index) return;
    
    SceneBlock temp = editor->project->scenes[from_index];
    
    if (from_index < to_index) {
        // Move forward
        for (int i = from_index; i < to_index; ++i) {
            editor->project->scenes[i] = editor->project->scenes[i + 1];
        }
    } else {
        // Move backward
        for (int i = from_index; i > to_index; --i) {
            editor->project->scenes[i] = editor->project->scenes[i - 1];
        }
    }
    
    editor->project->scenes[to_index] = temp;
    editor->project->modified = true;
}

SceneBlock* GetScene(EditorContext* editor, int scene_index) {
    if (!editor || scene_index < 0 || scene_index >= editor->project->scene_count) return nullptr;
    return &editor->project->scenes[scene_index];
}

// ===== Cue Management =====

int AddShaderCue(SceneBlock* scene, const ShaderCue& cue) {
    if (!scene) return -1;
    
    // Resize if needed
    if (scene->shader_cue_count >= scene->shader_cue_capacity) {
        int new_capacity = scene->shader_cue_capacity == 0 ? 4 : scene->shader_cue_capacity * 2;
        ShaderCue* new_cues = new ShaderCue[new_capacity];
        
        for (int i = 0; i < scene->shader_cue_count; ++i) {
            new_cues[i] = scene->shader_cues[i];
        }
        
        delete[] scene->shader_cues;
        scene->shader_cues = new_cues;
        scene->shader_cue_capacity = new_capacity;
    }
    
    int index = scene->shader_cue_count++;
    scene->shader_cues[index] = cue;
    return index;
}

int AddImageCue(SceneBlock* scene, const ImageCue& cue) {
    if (!scene) return -1;
    
    if (scene->image_cue_count >= scene->image_cue_capacity) {
        int new_capacity = scene->image_cue_capacity == 0 ? 4 : scene->image_cue_capacity * 2;
        ImageCue* new_cues = new ImageCue[new_capacity];
        
        for (int i = 0; i < scene->image_cue_count; ++i) {
            new_cues[i] = scene->image_cues[i];
        }
        
        delete[] scene->image_cues;
        scene->image_cues = new_cues;
        scene->image_cue_capacity = new_capacity;
    }
    
    int index = scene->image_cue_count++;
    scene->image_cues[index] = cue;
    return index;
}

int AddTextCue(SceneBlock* scene, const TextCue& cue) {
    if (!scene) return -1;
    
    if (scene->text_cue_count >= scene->text_cue_capacity) {
        int new_capacity = scene->text_cue_capacity == 0 ? 4 : scene->text_cue_capacity * 2;
        TextCue* new_cues = new TextCue[new_capacity];
        
        for (int i = 0; i < scene->text_cue_count; ++i) {
            new_cues[i] = scene->text_cues[i];
        }
        
        delete[] scene->text_cues;
        scene->text_cues = new_cues;
        scene->text_cue_capacity = new_capacity;
    }
    
    int index = scene->text_cue_count++;
    scene->text_cues[index] = cue;
    return index;
}

int AddScrollTextCue(SceneBlock* scene, const ScrollTextCue& cue) {
    if (!scene) return -1;

    if (scene->scroll_text_cue_count >= scene->scroll_text_cue_capacity) {
        int new_capacity = scene->scroll_text_cue_capacity == 0 ? 4 : scene->scroll_text_cue_capacity * 2;
        ScrollTextCue* new_cues = new ScrollTextCue[new_capacity];

        for (int i = 0; i < scene->scroll_text_cue_count; ++i) {
            new_cues[i] = scene->scroll_text_cues[i];
        }

        delete[] scene->scroll_text_cues;
        scene->scroll_text_cues = new_cues;
        scene->scroll_text_cue_capacity = new_capacity;
    }

    int index = scene->scroll_text_cue_count++;
    scene->scroll_text_cues[index] = cue;
    return index;
}

int AddMusicCue(SceneBlock* scene, const MusicCue& cue) {
    if (!scene) return -1;
    
    if (scene->music_cue_count >= scene->music_cue_capacity) {
        int new_capacity = scene->music_cue_capacity == 0 ? 4 : scene->music_cue_capacity * 2;
        MusicCue* new_cues = new MusicCue[new_capacity];
        
        for (int i = 0; i < scene->music_cue_count; ++i) {
            new_cues[i] = scene->music_cues[i];
        }
        
        delete[] scene->music_cues;
        scene->music_cues = new_cues;
        scene->music_cue_capacity = new_capacity;
    }
    
    int index = scene->music_cue_count++;
    scene->music_cues[index] = cue;
    return index;
}

void DeleteShaderCue(SceneBlock* scene, int cue_index) {
    if (!scene || cue_index < 0 || cue_index >= scene->shader_cue_count) return;
    
    for (int i = cue_index; i < scene->shader_cue_count - 1; ++i) {
        scene->shader_cues[i] = scene->shader_cues[i + 1];
    }
    scene->shader_cue_count--;
}

void DeleteImageCue(SceneBlock* scene, int cue_index) {
    if (!scene || cue_index < 0 || cue_index >= scene->image_cue_count) return;
    
    for (int i = cue_index; i < scene->image_cue_count - 1; ++i) {
        scene->image_cues[i] = scene->image_cues[i + 1];
    }
    scene->image_cue_count--;
}

void DeleteTextCue(SceneBlock* scene, int cue_index) {
    if (!scene || cue_index < 0 || cue_index >= scene->text_cue_count) return;
    
    for (int i = cue_index; i < scene->text_cue_count - 1; ++i) {
        scene->text_cues[i] = scene->text_cues[i + 1];
    }
    scene->text_cue_count--;
}

void DeleteScrollTextCue(SceneBlock* scene, int cue_index) {
    if (!scene || cue_index < 0 || cue_index >= scene->scroll_text_cue_count) return;

    for (int i = cue_index; i < scene->scroll_text_cue_count - 1; ++i) {
        scene->scroll_text_cues[i] = scene->scroll_text_cues[i + 1];
    }
    scene->scroll_text_cue_count--;
}

void DeleteMusicCue(SceneBlock* scene, int cue_index) {
    if (!scene || cue_index < 0 || cue_index >= scene->music_cue_count) return;
    
    for (int i = cue_index; i < scene->music_cue_count - 1; ++i) {
        scene->music_cues[i] = scene->music_cues[i + 1];
    }
    scene->music_cue_count--;
}

int AddMeshCue(SceneBlock* scene, const MeshCue& cue) {
    if (!scene) return -1;

    if (scene->mesh_cue_count >= scene->mesh_cue_capacity) {
        int new_capacity = scene->mesh_cue_capacity == 0 ? 4 : scene->mesh_cue_capacity * 2;
        MeshCue* new_cues = new MeshCue[new_capacity];

        for (int i = 0; i < scene->mesh_cue_count; ++i) {
            new_cues[i] = scene->mesh_cues[i];
        }

        delete[] scene->mesh_cues;
        scene->mesh_cues = new_cues;
        scene->mesh_cue_capacity = new_capacity;
    }

    int index = scene->mesh_cue_count++;
    scene->mesh_cues[index] = cue;
    return index;
}

void DeleteMeshCue(SceneBlock* scene, int cue_index) {
    if (!scene || cue_index < 0 || cue_index >= scene->mesh_cue_count) return;

    for (int i = cue_index; i < scene->mesh_cue_count - 1; ++i) {
        scene->mesh_cues[i] = scene->mesh_cues[i + 1];
    }
    scene->mesh_cue_count--;
}

// ===== Shader Presets =====

void LoadShaderPreset(ShaderCue* cue, int preset_id) {
    if (!cue) return;
    
    cue->shader_scene_id = preset_id;
    
    // Find and set the preset name
    for (int i = 0; i < g_shader_preset_count; ++i) {
        if (g_shader_presets[i].id == preset_id) {
            strncpy_s(cue->shader_name, sizeof(cue->shader_name), g_shader_presets[i].name, _TRUNCATE);
            break;
        }
    }
    
    // Default values
    ResetShaderValues(cue);
}

void RandomizeShaderColors(ShaderCue* cue) {
    if (!cue) return;
    
    // Simple randomization (can be improved with color harmony later)
    cue->palette_low.r = (float)rand() / RAND_MAX;
    cue->palette_low.g = (float)rand() / RAND_MAX;
    cue->palette_low.b = (float)rand() / RAND_MAX;
    
    cue->palette_mid.r = (float)rand() / RAND_MAX;
    cue->palette_mid.g = (float)rand() / RAND_MAX;
    cue->palette_mid.b = (float)rand() / RAND_MAX;
    
    cue->palette_high.r = (float)rand() / RAND_MAX;
    cue->palette_high.g = (float)rand() / RAND_MAX;
    cue->palette_high.b = (float)rand() / RAND_MAX;
}

// ===== PREVIEW VIEWPORT =====

// Simple vertex shader for fullscreen quad
static const char* preview_vertex_shader = R"(
#version 330 core
out vec2 uv;
uniform float u_warp;
void main() {
    float x = -1.0 + float((gl_VertexID & 1) << 2);
    float y = -1.0 + float((gl_VertexID & 2) << 1);

    vec2 base_uv = vec2((x + 1.0) * 0.5, (y + 1.0) * 0.5);
    vec2 p = base_uv * 2.0 - 1.0;
    float r2 = dot(p, p);
    vec2 swirl = vec2(-p.y, p.x);
    float warp = clamp(u_warp, 0.0, 1.0);

    // Small common UV warp so all shader presets react to warp, even without explicit u_warp usage.
    base_uv += swirl * (0.03 * warp) * (0.2 + 0.8 * max(0.0, 1.0 - r2));
    uv = base_uv;

    gl_Position = vec4(x, y, 0.0, 1.0);
}
)";

// Test fragment shader - Plasma effect
static const char* preview_fragment_shader = R"(
#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform float u_time;
uniform vec2 u_resolution;
uniform vec3 u_palette_low;
uniform vec3 u_palette_mid;
uniform vec3 u_palette_high;
uniform float u_speed;
uniform float u_intensity;

void main() {
    vec2 p = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0);
    float t = u_time * u_speed;
    
    float v = sin(p.x * 10.0 + t) + sin(p.y * 10.0 + t * 0.5) + sin((p.x + p.y) * 5.0 + t * 0.8);
    v = v / 3.0 * u_intensity;
    
    vec3 col = mix(mix(u_palette_low, u_palette_mid, smoothstep(-1.0, 0.0, v)), 
                   u_palette_high, smoothstep(0.0, 1.0, v));
    
    fragColor = vec4(col, 1.0);
}
)";

// Sprite vertex shader for image/text rendering
static const char* sprite_vertex_shader = R"(
#version 330 core
out vec2 uv;
uniform vec2 u_position;  // -1 to 1
uniform vec2 u_size;      // width, height in normalized coords
void main() {
    float x = -1.0 + float((gl_VertexID & 1) << 2);
    float y = -1.0 + float((gl_VertexID & 2) << 1);
    uv = vec2((x + 1.0) * 0.5, 1.0 - (y + 1.0) * 0.5);  // Flip V coordinate
    // Use Z = 0.999 to ensure sprites render in front of 3D meshes even if depth state isn't fully reset
    gl_Position = vec4(u_position.x + x * u_size.x, u_position.y + y * u_size.y, 0.999, 1.0);
}
)";

// Sprite fragment shader - textured with opacity
static const char* sprite_fragment_shader = R"(
#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform sampler2D u_texture;
uniform float u_opacity;
void main() {
    vec4 texColor = texture(u_texture, uv);
    fragColor = vec4(texColor.rgb, texColor.a * u_opacity);
}
)";

// Mesh (3D Phong) shaders
static const char* mesh_vertex_shader = R"(
#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
out vec3 v_frag_pos;
out vec3 v_normal;
out vec2 v_uv;
uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_projection;
void main() {
    vec4 world_pos = u_model * vec4(a_pos, 1.0);
    v_frag_pos = world_pos.xyz;
    v_normal   = mat3(transpose(inverse(u_model))) * a_normal;
    v_uv       = a_uv;
    gl_Position = u_projection * u_view * world_pos;
}
)";

static const char* mesh_fragment_shader = R"(
#version 330 core
in vec3 v_frag_pos;
in vec3 v_normal;
in vec2 v_uv;
out vec4 fragColor;
uniform vec3  u_light_pos;
uniform vec3  u_view_pos;
uniform vec4  u_color;
uniform float u_metallic;
uniform float u_roughness;
uniform sampler2D u_base_color_texture;
uniform int u_has_texture;
void main() {
    vec3  base = u_color.rgb;
    float alpha = u_color.a;
    if (u_has_texture != 0) {
        vec4 tex_color = texture(u_base_color_texture, v_uv);
        base *= tex_color.rgb;
        alpha *= tex_color.a;
    }
    vec3  norm     = normalize(v_normal);
    vec3  ldir     = normalize(u_light_pos - v_frag_pos);
    vec3  vdir     = normalize(u_view_pos  - v_frag_pos);
    vec3  hdir     = normalize(ldir + vdir);
    // Ambient
    float ambient  = 0.15;
    // Diffuse — metals have little diffuse
    float diff     = max(dot(norm, ldir), 0.0) * (1.0 - u_metallic * 0.9);
    // Specular: shininess driven by roughness; colour tinted for metals
    float shininess   = mix(2.0, 256.0, 1.0 - u_roughness);
    float spec_fac    = pow(max(dot(norm, hdir), 0.0), shininess);
    vec3  spec_col    = mix(vec3(0.04), base, u_metallic);
    vec3  spec        = spec_col * spec_fac * (1.0 - u_roughness * 0.85);
    vec3  result      = base * (ambient + diff) + spec;
    fragColor = vec4(result, alpha);
}
)";

// Shader source is now centralized in shader_presets.cpp
// Use GetShaderSourceById() to fetch shader GLSL code

static rev::shader::Program* g_preview_shader_cache[128] = {};

static rev::shader::Program* GetOrCompilePreviewShaderProgram(int shader_id) {
    if (shader_id < 0 || shader_id >= g_shader_preset_count) return nullptr;
    if (shader_id >= (int)(sizeof(g_preview_shader_cache) / sizeof(g_preview_shader_cache[0]))) return nullptr;

    if (g_preview_shader_cache[shader_id]) {
        return g_preview_shader_cache[shader_id];
    }

    const char* shader_source = GetShaderSourceById(shader_id);
    if (!shader_source) {
        return nullptr;
    }

    rev::shader::Program* prog = rev::shader::CompileFromSource(preview_vertex_shader, shader_source);
    if (!prog) {
        return nullptr;
    }

    g_preview_shader_cache[shader_id] = prog;
    return prog;
}

static void CompilePreviewShader(EditorContext* editor, int shader_id) {
    if (!editor) return;
    if (shader_id < 0 || shader_id >= g_shader_preset_count) shader_id = 0;
    
    printf("[CompilePreviewShader] Compiling shader ID: %d\n", shader_id);
    
    // Destroy old shader if exists
    if (editor->preview_shader) {
        rev::shader::DestroyProgram((rev::shader::Program*)editor->preview_shader);
        editor->preview_shader = nullptr;
        printf("[CompilePreviewShader] Destroyed old shader\n");
    }
    
    // Get shader source from centralized registry
    const char* shader_source = GetShaderSourceById(shader_id);
    if (!shader_source) {
        printf("[CompilePreviewShader] ERROR: No shader found for ID %d\n", shader_id);
        return;
    }
    
    // Compile new shader with correct source
    editor->preview_shader = rev::shader::CompileFromSource(preview_vertex_shader, shader_source);
    editor->preview_current_shader_id = shader_id;
    
    if (editor->preview_shader) {
        printf("[CompilePreviewShader] SUCCESS: Shader %d compiled\n", shader_id);
    } else {
        printf("[CompilePreviewShader] FAILED: Shader %d compilation returned null\n", shader_id);
    }
}

void InitializePreview(EditorContext* editor, int width, int height) {
    if (!editor || editor->preview_initialized) return;
    
    // Load OpenGL functions
    typedef void (*PFNGLGENFRAMEBUFFERSPROC)(int n, unsigned int* framebuffers);
    typedef void (*PFNGLBINDFRAMEBUFFERPROC)(unsigned int target, unsigned int framebuffer);
    typedef void (*PFNGLFRAMEBUFFERTEXTURE2DPROC)(unsigned int target, unsigned int attachment, unsigned int textarget, unsigned int texture, int level);
    typedef void (*PFNGLGENRENDERBUFFERSPROC)(int n, unsigned int* renderbuffers);
    typedef void (*PFNGLBINDRENDERBUFFERPROC)(unsigned int target, unsigned int renderbuffer);
    typedef void (*PFNGLRENDERBUFFERSTORAGEPROC)(unsigned int target, unsigned int internalformat, int width, int height);
    typedef void (*PFNGLFRAMEBUFFERRENDERBUFFERPROC)(unsigned int target, unsigned int attachment, unsigned int renderbuffertarget, unsigned int renderbuffer);
    typedef unsigned int (*PFNGLCHECKFRAMEBUFFERSTATUSPROC)(unsigned int target);
    
    auto glGenFramebuffers = (PFNGLGENFRAMEBUFFERSPROC)wglGetProcAddress("glGenFramebuffers");
    auto glBindFramebuffer = (PFNGLBINDFRAMEBUFFERPROC)wglGetProcAddress("glBindFramebuffer");
    auto glFramebufferTexture2D = (PFNGLFRAMEBUFFERTEXTURE2DPROC)wglGetProcAddress("glFramebufferTexture2D");
    auto glGenRenderbuffers = (PFNGLGENRENDERBUFFERSPROC)wglGetProcAddress("glGenRenderbuffers");
    auto glBindRenderbuffer = (PFNGLBINDRENDERBUFFERPROC)wglGetProcAddress("glBindRenderbuffer");
    auto glRenderbufferStorage = (PFNGLRENDERBUFFERSTORAGEPROC)wglGetProcAddress("glRenderbufferStorage");
    auto glFramebufferRenderbuffer = (PFNGLFRAMEBUFFERRENDERBUFFERPROC)wglGetProcAddress("glFramebufferRenderbuffer");
    auto glCheckFramebufferStatus = (PFNGLCHECKFRAMEBUFFERSTATUSPROC)wglGetProcAddress("glCheckFramebufferStatus");
    
    if (!glGenFramebuffers || !glBindFramebuffer || !glFramebufferTexture2D || 
        !glGenRenderbuffers || !glBindRenderbuffer || !glRenderbufferStorage ||
        !glFramebufferRenderbuffer || !glCheckFramebufferStatus) {
        return; // OpenGL functions not available
    }
    
    editor->preview_width = width;
    editor->preview_height = height;
    
    // Create framebuffer
    glGenFramebuffers(1, &editor->preview_fbo);
    glBindFramebuffer(0x8D40, editor->preview_fbo); // GL_FRAMEBUFFER
    
    // Create color texture
    glGenTextures(1, &editor->preview_texture);
    glBindTexture(GL_TEXTURE_2D, editor->preview_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(0x8D40, 0x8CE0, GL_TEXTURE_2D, editor->preview_texture, 0); // GL_COLOR_ATTACHMENT0
    
    // Create depth renderbuffer
    glGenRenderbuffers(1, &editor->preview_depth);
    glBindRenderbuffer(0x8D41, editor->preview_depth); // GL_RENDERBUFFER
    glRenderbufferStorage(0x8D41, 0x81A5, width, height); // GL_DEPTH_COMPONENT24
    glFramebufferRenderbuffer(0x8D40, 0x8D00, 0x8D41, editor->preview_depth); // GL_DEPTH_ATTACHMENT
    
    // Check framebuffer completeness
    if (glCheckFramebufferStatus(0x8D40) != 0x8CD5) { // GL_FRAMEBUFFER_COMPLETE
        CleanupPreview(editor);
        return;
    }
    
    // Unbind framebuffer
    glBindFramebuffer(0x8D40, 0);
    
    // Don't compile any shader yet - wait for first render with actual cue
    // (preview_current_shader_id is -1, so first cue will trigger compile)
    
    // Compile sprite shader for image/text
    editor->sprite_shader = rev::shader::CompileFromSource(sprite_vertex_shader, sprite_fragment_shader);
    if (!editor->sprite_shader) {
        CleanupPreview(editor);
        return;
    }

    editor->mesh_shader = rev::shader::CompileFromSource(mesh_vertex_shader, mesh_fragment_shader);
    // mesh_shader failure is non-fatal — mesh cues just won't render

    // Create a dummy VAO so gl_VertexID-based fullscreen-quad draws are valid in
    // OpenGL 3.3 core profile (draw calls with VAO 0 are undefined behaviour).
    typedef void (*PFNGLGENVERTEXARRAYSPROC)(int n, unsigned int* arrays);
    auto glGenVertexArrays = (PFNGLGENVERTEXARRAYSPROC)wglGetProcAddress("glGenVertexArrays");
    if (glGenVertexArrays) {
        glGenVertexArrays(1, &editor->preview_vao);
    }

    editor->preview_initialized = true;
}

void CleanupPreview(EditorContext* editor) {
    if (!editor || !editor->preview_initialized) return;
    
    typedef void (*PFNGLDELETEFRAMEBUFFERSPROC)(int n, const unsigned int* framebuffers);
    typedef void (*PFNGLDELETERENDERBUFFERSPROC)(int n, const unsigned int* renderbuffers);
    
    auto glDeleteFramebuffers = (PFNGLDELETEFRAMEBUFFERSPROC)wglGetProcAddress("glDeleteFramebuffers");
    auto glDeleteRenderbuffers = (PFNGLDELETERENDERBUFFERSPROC)wglGetProcAddress("glDeleteRenderbuffers");
    
    // Destroy shader programs
    if (editor->preview_shader) {
        rev::shader::DestroyProgram((rev::shader::Program*)editor->preview_shader);
        editor->preview_shader = nullptr;
    }
    if (editor->sprite_shader) {
        rev::shader::DestroyProgram((rev::shader::Program*)editor->sprite_shader);
        editor->sprite_shader = nullptr;
    }
    if (editor->mesh_shader) {
        rev::shader::DestroyProgram((rev::shader::Program*)editor->mesh_shader);
        editor->mesh_shader = nullptr;
    }
    editor->preview_current_shader_id = -1;
    
    if (editor->preview_texture) {
        glDeleteTextures(1, &editor->preview_texture);
        editor->preview_texture = 0;
    }
    
    if (editor->preview_depth && glDeleteRenderbuffers) {
        glDeleteRenderbuffers(1, &editor->preview_depth);
        editor->preview_depth = 0;
    }
    
    if (editor->preview_fbo && glDeleteFramebuffers) {
        glDeleteFramebuffers(1, &editor->preview_fbo);
        editor->preview_fbo = 0;
    }

    if (editor->preview_vao) {
        typedef void (*PFNGLDELETEVERTEXARRAYSPROC)(int n, const unsigned int* arrays);
        auto glDeleteVertexArrays = (PFNGLDELETEVERTEXARRAYSPROC)wglGetProcAddress("glDeleteVertexArrays");
        if (glDeleteVertexArrays) glDeleteVertexArrays(1, &editor->preview_vao);
        editor->preview_vao = 0;
    }

    for (int i = 0; i < (int)(sizeof(g_preview_shader_cache) / sizeof(g_preview_shader_cache[0])); ++i) {
        if (g_preview_shader_cache[i]) {
            rev::shader::DestroyProgram(g_preview_shader_cache[i]);
            g_preview_shader_cache[i] = nullptr;
        }
    }

    editor->preview_initialized = false;
}

void ResizePreview(EditorContext* editor, int width, int height) {
    if (!editor) return;
    
    if (editor->preview_width != width || editor->preview_height != height) {
        CleanupPreview(editor);
        InitializePreview(editor, width, height);
    }
}

void RenderPreviewFrame(EditorContext* editor) {
    if (!editor || !editor->preview_initialized) return;
    
    typedef void (*PFNGLBINDFRAMEBUFFERPROC)(unsigned int target, unsigned int framebuffer);
    auto glBindFramebuffer = (PFNGLBINDFRAMEBUFFERPROC)wglGetProcAddress("glBindFramebuffer");
    
    if (!glBindFramebuffer) return;
    
    // Bind preview framebuffer
    glBindFramebuffer(0x8D40, editor->preview_fbo); // GL_FRAMEBUFFER

    // Bind the dummy VAO — required in OpenGL 3.3 core profile for any glDrawArrays
    // call, including gl_VertexID-based fullscreen quads.  Without this, draw calls
    // silently fail (e.g. background shader shows as black).
    if (editor->preview_vao) {
        typedef void (*PFNGLBINDVERTEXARRAYPROC)(unsigned int array);
        auto glBindVertexArray = (PFNGLBINDVERTEXARRAYPROC)wglGetProcAddress("glBindVertexArray");
        if (glBindVertexArray) glBindVertexArray(editor->preview_vao);
    }

    // Set viewport
    glViewport(0, 0, editor->preview_width, editor->preview_height);
    
    // Clear
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    // Disable depth test for background shader (fullscreen quad at infinite depth)
    glDisable(0x0B71);  // GL_DEPTH_TEST
    typedef void (*PFNGLDEPTHMASKPROC)(unsigned char flag);
    auto glDepthMask_fn = (PFNGLDEPTHMASKPROC)wglGetProcAddress("glDepthMask");
    if (glDepthMask_fn) glDepthMask_fn(0);  // GL_FALSE
    
    // Render shader layers first (supports stacked shader compositing with blend and order).
    if (editor->project) {
        struct ActiveShaderLayer {
            ShaderCue* cue;
            int layer_order;
            int layer_role;
            float absolute_start;
            float absolute_end;
        };

        ActiveShaderLayer layers[256];
        int layer_count = 0;

        float scene_start_time = 0.0f;
        for (int s = 0; s < editor->project->scene_count && layer_count < 256; s++) {
            SceneBlock* scene = &editor->project->scenes[s];
            bool is_last_scene = (s == editor->project->scene_count - 1);

            for (int i = 0; i < scene->shader_cue_count && layer_count < 256; i++) {
                ShaderCue* cue = &scene->shader_cues[i];
                float end = (cue->cue_end < 0.0f) ? scene->duration : cue->cue_end;
                float absolute_start = scene_start_time + cue->cue_start;
                float absolute_end = scene_start_time + end;
                bool time_in_range = is_last_scene
                    ? (editor->current_time >= absolute_start && editor->current_time <= absolute_end)
                    : (editor->current_time >= absolute_start && editor->current_time < absolute_end);
                if (time_in_range) {
                    layers[layer_count++] = { cue, cue->layer_order, cue->layer_role, absolute_start, absolute_end };
                }
            }

            scene_start_time += scene->duration;
        }

        for (int i = 0; i < layer_count - 1; i++) {
            for (int j = 0; j < layer_count - i - 1; j++) {
                const ActiveShaderLayer& a = layers[j];
                const ActiveShaderLayer& b = layers[j + 1];
                if ((a.layer_order > b.layer_order) ||
                    (a.layer_order == b.layer_order && a.layer_role > b.layer_role)) {
                    ActiveShaderLayer tmp = layers[j];
                    layers[j] = layers[j + 1];
                    layers[j + 1] = tmp;
                }
            }
        }

        for (int li = 0; li < layer_count; ++li) {
            ShaderCue* cue = layers[li].cue;
            if (!cue) continue;

            rev::shader::Program* prog = GetOrCompilePreviewShaderProgram(cue->shader_scene_id);
            if (!prog) continue;

            float speed = cue->speed;
            float intensity = cue->intensity;
            float warp = cue->warp;
            float exposure_base = cue->exposure_base;
            float exposure_ramp = cue->exposure_ramp;
            float fade_base = cue->fade_base;
            float fade_ramp = cue->fade_ramp;
            float opacity = cue->opacity;
            float palette_low[3] = { cue->palette_low.r, cue->palette_low.g, cue->palette_low.b };
            float palette_mid[3] = { cue->palette_mid.r, cue->palette_mid.g, cue->palette_mid.b };
            float palette_high[3] = { cue->palette_high.r, cue->palette_high.g, cue->palette_high.b };

            float elapsed_time = editor->current_time - layers[li].absolute_start;
            float cue_duration = layers[li].absolute_end - layers[li].absolute_start;
            float local_time = elapsed_time;
            if (local_time < 0.0f) local_time = 0.0f;
            if (cue_duration > 0.0f && local_time > cue_duration) local_time = cue_duration;
            elapsed_time = local_time;

            if (elapsed_time >= 0.0f) {
                if (cue->curve_palette_low_r >= 0 && cue->curve_palette_low_r < editor->project->curve_count) {
                    float t = elapsed_time / editor->project->curves[cue->curve_palette_low_r].duration;
                    palette_low[0] = rev::curve::Evaluate(editor->project->curves[cue->curve_palette_low_r], t);
                }
                if (cue->curve_palette_low_g >= 0 && cue->curve_palette_low_g < editor->project->curve_count) {
                    float t = elapsed_time / editor->project->curves[cue->curve_palette_low_g].duration;
                    palette_low[1] = rev::curve::Evaluate(editor->project->curves[cue->curve_palette_low_g], t);
                }
                if (cue->curve_palette_low_b >= 0 && cue->curve_palette_low_b < editor->project->curve_count) {
                    float t = elapsed_time / editor->project->curves[cue->curve_palette_low_b].duration;
                    palette_low[2] = rev::curve::Evaluate(editor->project->curves[cue->curve_palette_low_b], t);
                }
                if (cue->curve_palette_mid_r >= 0 && cue->curve_palette_mid_r < editor->project->curve_count) {
                    float t = elapsed_time / editor->project->curves[cue->curve_palette_mid_r].duration;
                    palette_mid[0] = rev::curve::Evaluate(editor->project->curves[cue->curve_palette_mid_r], t);
                }
                if (cue->curve_palette_mid_g >= 0 && cue->curve_palette_mid_g < editor->project->curve_count) {
                    float t = elapsed_time / editor->project->curves[cue->curve_palette_mid_g].duration;
                    palette_mid[1] = rev::curve::Evaluate(editor->project->curves[cue->curve_palette_mid_g], t);
                }
                if (cue->curve_palette_mid_b >= 0 && cue->curve_palette_mid_b < editor->project->curve_count) {
                    float t = elapsed_time / editor->project->curves[cue->curve_palette_mid_b].duration;
                    palette_mid[2] = rev::curve::Evaluate(editor->project->curves[cue->curve_palette_mid_b], t);
                }
                if (cue->curve_palette_high_r >= 0 && cue->curve_palette_high_r < editor->project->curve_count) {
                    float t = elapsed_time / editor->project->curves[cue->curve_palette_high_r].duration;
                    palette_high[0] = rev::curve::Evaluate(editor->project->curves[cue->curve_palette_high_r], t);
                }
                if (cue->curve_palette_high_g >= 0 && cue->curve_palette_high_g < editor->project->curve_count) {
                    float t = elapsed_time / editor->project->curves[cue->curve_palette_high_g].duration;
                    palette_high[1] = rev::curve::Evaluate(editor->project->curves[cue->curve_palette_high_g], t);
                }
                if (cue->curve_palette_high_b >= 0 && cue->curve_palette_high_b < editor->project->curve_count) {
                    float t = elapsed_time / editor->project->curves[cue->curve_palette_high_b].duration;
                    palette_high[2] = rev::curve::Evaluate(editor->project->curves[cue->curve_palette_high_b], t);
                }
                if (cue->curve_speed >= 0 && cue->curve_speed < editor->project->curve_count) {
                    float t = elapsed_time / editor->project->curves[cue->curve_speed].duration;
                    speed = rev::curve::Evaluate(editor->project->curves[cue->curve_speed], t);
                }
                if (cue->curve_intensity >= 0 && cue->curve_intensity < editor->project->curve_count) {
                    float t = elapsed_time / editor->project->curves[cue->curve_intensity].duration;
                    intensity = rev::curve::Evaluate(editor->project->curves[cue->curve_intensity], t);
                }
                if (cue->curve_warp >= 0 && cue->curve_warp < editor->project->curve_count) {
                    float t = elapsed_time / editor->project->curves[cue->curve_warp].duration;
                    warp = rev::curve::Evaluate(editor->project->curves[cue->curve_warp], t);
                }
                if (cue->curve_exposure >= 0 && cue->curve_exposure < editor->project->curve_count) {
                    float t = elapsed_time / editor->project->curves[cue->curve_exposure].duration;
                    exposure_base = rev::curve::Evaluate(editor->project->curves[cue->curve_exposure], t);
                }
                if (cue->curve_fade >= 0 && cue->curve_fade < editor->project->curve_count) {
                    float t = elapsed_time / editor->project->curves[cue->curve_fade].duration;
                    fade_base = rev::curve::Evaluate(editor->project->curves[cue->curve_fade], t);
                }
                if (cue->curve_opacity >= 0 && cue->curve_opacity < editor->project->curve_count) {
                    float t = elapsed_time / editor->project->curves[cue->curve_opacity].duration;
                    opacity = rev::curve::Evaluate(editor->project->curves[cue->curve_opacity], t);
                }
                if (cue->curve_exposure_ramp >= 0 && cue->curve_exposure_ramp < editor->project->curve_count) {
                    float t = elapsed_time / editor->project->curves[cue->curve_exposure_ramp].duration;
                    exposure_ramp = rev::curve::Evaluate(editor->project->curves[cue->curve_exposure_ramp], t);
                }
                if (cue->curve_fade_ramp >= 0 && cue->curve_fade_ramp < editor->project->curve_count) {
                    float t = elapsed_time / editor->project->curves[cue->curve_fade_ramp].duration;
                    fade_ramp = rev::curve::Evaluate(editor->project->curves[cue->curve_fade_ramp], t);
                }
            }

            float envelope = ComputeShaderCueEnvelope(local_time, cue_duration, cue->fade_in, cue->fade_out);
            float exposure = exposure_base + exposure_ramp * local_time;
            float fade = (fade_base + fade_ramp * local_time) * envelope;
            if (exposure < 0.0f) exposure = 0.0f;
            if (fade < 0.0f) fade = 0.0f;

            if (li == 0) {
                glDisable(GL_BLEND);
            } else {
                glEnable(GL_BLEND);
                ApplyShaderLayerBlendMode(cue->blend_mode, opacity);
            }

            // Fold layer opacity into fade for shaders that use fade as alpha/intensity.
            fade *= opacity;

            rev::shader::Use(prog);
            rev::shader::SetFloat(prog, rev::shader::GetUniformLocation(prog, "u_time"), editor->current_time);
            rev::shader::SetVec2(prog, rev::shader::GetUniformLocation(prog, "u_resolution"),
                                 (float)editor->preview_width, (float)editor->preview_height);
            rev::shader::SetVec3(prog, rev::shader::GetUniformLocation(prog, "u_palette_low"),
                                 palette_low[0], palette_low[1], palette_low[2]);
            rev::shader::SetVec3(prog, rev::shader::GetUniformLocation(prog, "u_palette_mid"),
                                 palette_mid[0], palette_mid[1], palette_mid[2]);
            rev::shader::SetVec3(prog, rev::shader::GetUniformLocation(prog, "u_palette_high"),
                                 palette_high[0], palette_high[1], palette_high[2]);
            rev::shader::SetFloat(prog, rev::shader::GetUniformLocation(prog, "u_speed"), speed);
            rev::shader::SetFloat(prog, rev::shader::GetUniformLocation(prog, "u_intensity"), intensity);
            rev::shader::SetFloat(prog, rev::shader::GetUniformLocation(prog, "u_warp"), warp);
            rev::shader::SetFloat(prog, rev::shader::GetUniformLocation(prog, "u_exposure_base"), exposure);
            rev::shader::SetFloat(prog, rev::shader::GetUniformLocation(prog, "u_fade_base"), fade);

            glDrawArrays(GL_TRIANGLES, 0, 3);
        }

        glDisable(GL_BLEND);
    }

    // --- Unified layered draw pass ---
    // Collect all active image/text/scroll/mesh cues across all scenes, sort by
    // layer_order (ascending = drawn first = further back), draw in order.
    if (editor->project && (editor->sprite_shader || editor->mesh_shader)) {
        auto* sprite_prog = editor->sprite_shader ? (rev::shader::Program*)editor->sprite_shader : nullptr;
        auto* mesh_prog   = editor->mesh_shader   ? (rev::shader::Program*)editor->mesh_shader   : nullptr;

        // Pre-load GL extension procs
        typedef void (*PFNGLACTIVETEXTUREPROC)(unsigned int texture);
        auto glActiveTexture_fn = (PFNGLACTIVETEXTUREPROC)wglGetProcAddress("glActiveTexture");
        auto glUniformMatrix4fv = (void(*)(int,int,unsigned char,const float*))wglGetProcAddress("glUniformMatrix4fv");
        typedef void (*PFNGLDEPTHFUNCPROC)(unsigned int func);
        auto glDepthFunc_fn = (PFNGLDEPTHFUNCPROC)wglGetProcAddress("glDepthFunc");
        typedef void (*PFNGLUNIFORM4FVPROC)(int, int, const float*);
        auto glUniform4fv_fn = (PFNGLUNIFORM4FVPROC)wglGetProcAddress("glUniform4fv");
        typedef void (*PFNGLDEPTHMASKPROC)(unsigned char flag);
        auto glDepthMask_fn = (PFNGLDEPTHMASKPROC)wglGetProcAddress("glDepthMask");

        // Pre-compute 3D camera (reused for every mesh item)
        float mesh_aspect = (editor->preview_height > 0)
            ? (float)editor->preview_width / (float)editor->preview_height : 1.0f;
        float eye[3]       = {0.0f, 0.0f, 5.0f};
        float center3[3]   = {0.0f, 0.0f, 0.0f};
        float up3[3]       = {0.0f, 1.0f, 0.0f};
        float light_pos[3] = {3.0f, 5.0f, 4.0f};
        float view_mat[16], proj_mat[16];
        rev::runtime::Mat4Perspective(proj_mat, 3.14159265f * 0.25f, mesh_aspect, 0.1f, 100.0f);
        rev::runtime::Mat4LookAt(view_mat, eye, center3, up3);

        // Upload view/proj/lighting once if we have a mesh shader
        int mp_model = -1, mp_view = -1, mp_proj = -1, mp_light = -1,
            mp_vpos  = -1, mp_col  = -1, mp_metal = -1, mp_rough = -1;
        if (mesh_prog) {
            mp_model = rev::shader::GetUniformLocation(mesh_prog, "u_model");
            mp_view  = rev::shader::GetUniformLocation(mesh_prog, "u_view");
            mp_proj  = rev::shader::GetUniformLocation(mesh_prog, "u_projection");
            mp_light = rev::shader::GetUniformLocation(mesh_prog, "u_light_pos");
            mp_vpos  = rev::shader::GetUniformLocation(mesh_prog, "u_view_pos");
            mp_col   = rev::shader::GetUniformLocation(mesh_prog, "u_color");
            mp_metal = rev::shader::GetUniformLocation(mesh_prog, "u_metallic");
            mp_rough = rev::shader::GetUniformLocation(mesh_prog, "u_roughness");
            rev::shader::Use(mesh_prog);
            if (glUniformMatrix4fv) {
                glUniformMatrix4fv(mp_view, 1, 0, view_mat);
                glUniformMatrix4fv(mp_proj, 1, 0, proj_mat);
            }
            rev::shader::SetVec3(mesh_prog, mp_light, light_pos[0], light_pos[1], light_pos[2]);
            rev::shader::SetVec3(mesh_prog, mp_vpos, eye[0], eye[1], eye[2]);
        }

        // Sprite shader uniform locations
        int sp_tex = sprite_prog ? rev::shader::GetUniformLocation(sprite_prog, "u_texture")  : -1;
        int sp_pos = sprite_prog ? rev::shader::GetUniformLocation(sprite_prog, "u_position") : -1;
        int sp_sz  = sprite_prog ? rev::shader::GetUniformLocation(sprite_prog, "u_size")     : -1;
        int sp_opa = sprite_prog ? rev::shader::GetUniformLocation(sprite_prog, "u_opacity")  : -1;

        // Build unified draw list: type 0=image 1=text 2=mesh 3=scroll text
        struct DrawItem { int type; void* cue; int layer_order; float scene_start_time; };
        static const int kMaxItems = 512;
        DrawItem items[kMaxItems];
        int item_count = 0;

        // Calculate scene start times for absolute time comparison
        float item_scene_start = 0.0f;
        for (int s = 0; s < editor->project->scene_count && item_count < kMaxItems; s++) {
            SceneBlock* scene = &editor->project->scenes[s];
            bool is_last_scene = (s == editor->project->scene_count - 1);
            
            for (int i = 0; i < scene->image_cue_count && item_count < kMaxItems; i++) {
                ImageCue* cue = &scene->image_cues[i];
                float end = (cue->cue_end < 0.0f) ? scene->duration : cue->cue_end;
                float absolute_start = item_scene_start + cue->cue_start;
                float absolute_end = item_scene_start + end;
                bool time_in_range = is_last_scene
                    ? (editor->current_time >= absolute_start && editor->current_time <= absolute_end)
                    : (editor->current_time >= absolute_start && editor->current_time < absolute_end);
                if (time_in_range)
                    items[item_count++] = { 0, cue, cue->layer_order, item_scene_start };
            }
            for (int i = 0; i < scene->text_cue_count && item_count < kMaxItems; i++) {
                TextCue* cue = &scene->text_cues[i];
                float end = (cue->cue_end < 0.0f) ? scene->duration : cue->cue_end;
                float absolute_start = item_scene_start + cue->cue_start;
                float absolute_end = item_scene_start + end;
                bool time_in_range = is_last_scene
                    ? (editor->current_time >= absolute_start && editor->current_time <= absolute_end)
                    : (editor->current_time >= absolute_start && editor->current_time < absolute_end);
                if (time_in_range && cue->text[0])
                    items[item_count++] = { 1, cue, cue->layer_order, item_scene_start };
            }
            for (int i = 0; i < scene->scroll_text_cue_count && item_count < kMaxItems; i++) {
                ScrollTextCue* cue = &scene->scroll_text_cues[i];
                float end = (cue->cue_end < 0.0f) ? scene->duration : cue->cue_end;
                float absolute_start = item_scene_start + cue->cue_start;
                float absolute_end = item_scene_start + end;
                bool time_in_range = is_last_scene
                    ? (editor->current_time >= absolute_start && editor->current_time <= absolute_end)
                    : (editor->current_time >= absolute_start && editor->current_time < absolute_end);
                if (time_in_range && cue->text[0])
                    items[item_count++] = { 3, cue, cue->layer_order, item_scene_start };
            }
            for (int i = 0; i < scene->mesh_cue_count && item_count < kMaxItems; i++) {
                MeshCue* cue = &scene->mesh_cues[i];
                float end = (cue->cue_end < 0.0f) ? scene->duration : cue->cue_end;
                float absolute_start = item_scene_start + cue->cue_start;
                float absolute_end = item_scene_start + end;
                bool time_in_range = is_last_scene
                    ? (editor->current_time >= absolute_start && editor->current_time <= absolute_end)
                    : (editor->current_time >= absolute_start && editor->current_time < absolute_end);
                if (time_in_range)
                    items[item_count++] = { 2, cue, cue->layer_order, item_scene_start };
            }
            
            // Advance scene start time for next scene
            item_scene_start += scene->duration;
        }

        // Stable bubble sort by layer_order ascending
        // Tie-break rule for same layer: mesh behind image behind text/scroll.
        auto draw_priority = [](int type) {
            // type: 0=image, 1=text, 2=mesh, 3=scroll text
            if (type == 2) return 0; // mesh first (back)
            if (type == 0) return 1; // image middle
            return 2;                // text/scroll front
        };
        for (int i = 0; i < item_count - 1; i++)
            for (int j = 0; j < item_count - i - 1; j++)
                if (items[j].layer_order > items[j+1].layer_order ||
                    (items[j].layer_order == items[j+1].layer_order &&
                     draw_priority(items[j].type) > draw_priority(items[j+1].type))) {
                    DrawItem tmp = items[j]; items[j] = items[j+1]; items[j+1] = tmp;
                }

        bool blend_on = false, depth_on = false;

        for (int idx = 0; idx < item_count; idx++) {
            DrawItem& item = items[idx];

            if (item.type == 0 || item.type == 1 || item.type == 3) {
                // Sprite (image, text, or scroll text)
                if (!sprite_prog) continue;
                
                // When switching from mesh to sprite, rebind dummy VAO for gl_VertexID rendering
                if (depth_on && editor->preview_vao) {
                    typedef void (*PFNGLBINDVERTEXARRAYPROC)(unsigned int array);
                    auto glBindVertexArray = (PFNGLBINDVERTEXARRAYPROC)wglGetProcAddress("glBindVertexArray");
                    if (glBindVertexArray) glBindVertexArray(editor->preview_vao);
                }
                
                // Don't clear depth here - already cleared once before layer pass
                
                // Disable depth testing for 2D sprites
                glDisable(0x0B71);  // GL_DEPTH_TEST
                if (glDepthMask_fn) glDepthMask_fn(0);  // GL_FALSE
                depth_on = false;
                
                // Enable blending for sprites
                glEnable(GL_BLEND);
                blend_on = true;

                int sprite_blend_mode = 0;
                if (item.type == 0) {
                    sprite_blend_mode = ((ImageCue*)item.cue)->blend_mode;
                } else if (item.type == 1) {
                    sprite_blend_mode = ((TextCue*)item.cue)->blend_mode;
                } else {
                    sprite_blend_mode = ((ScrollTextCue*)item.cue)->blend_mode;
                }
                ApplySpriteBlendMode(sprite_blend_mode);
                
                rev::shader::Use(sprite_prog);

                unsigned int tex = 0;
                float norm_w = 0, norm_h = 0, pos_x = 0, pos_y = 0, opacity = 1.0f;

                if (item.type == 0) {
                    ImageCue* cue = (ImageCue*)item.cue;
                    
                    // Evaluate curves for animation
                    float anim_x = cue->x;
                    float anim_y = cue->y;
                    float anim_scale = cue->scale;
                    float anim_opacity = cue->opacity;
                    
                    // Calculate elapsed time from cue start (convert scene-relative to absolute)
                    float absolute_cue_start = item.scene_start_time + cue->cue_start;
                    float elapsed_time = editor->current_time - absolute_cue_start;
                    if (elapsed_time >= 0.0f) {
                        if (cue->curve_x >= 0 && cue->curve_x < editor->project->curve_count) {
                            rev::curve::Curve* curve = &editor->project->curves[cue->curve_x];
                            float t = elapsed_time / curve->duration;
                            anim_x = rev::curve::Evaluate(*curve, t);
                        }
                        if (cue->curve_y >= 0 && cue->curve_y < editor->project->curve_count) {
                            rev::curve::Curve* curve = &editor->project->curves[cue->curve_y];
                            float t = elapsed_time / curve->duration;
                            anim_y = rev::curve::Evaluate(*curve, t);
                        }
                        if (cue->curve_scale >= 0 && cue->curve_scale < editor->project->curve_count) {
                            rev::curve::Curve* curve = &editor->project->curves[cue->curve_scale];
                            float t = elapsed_time / curve->duration;
                            anim_scale = rev::curve::Evaluate(*curve, t);
                        }
                        if (cue->curve_opacity >= 0 && cue->curve_opacity < editor->project->curve_count) {
                            rev::curve::Curve* curve = &editor->project->curves[cue->curve_opacity];
                            float t = elapsed_time / curve->duration;
                            anim_opacity = rev::curve::Evaluate(*curve, t);
                        }
                    }
                    
                    char full_path[512];
                    snprintf(full_path, sizeof(full_path), "%s\\%s",
                             editor->project->assets_path, cue->asset_key);
                    rev::runtime::ImageTexture rt_img{};
                    if (!rev::runtime::LoadImageTexture(full_path, &rt_img)) continue;
                    tex    = rt_img.texture_id;
                    norm_w = (rt_img.width  * anim_scale) / editor->preview_width  * 2.0f;
                    norm_h = (rt_img.height * anim_scale) / editor->preview_height * 2.0f;
                    pos_x  =  (anim_x * 2.0f) - 1.0f;
                    pos_y  = -((anim_y * 2.0f) - 1.0f);
                    opacity = anim_opacity * rev::runtime::ComputeEffectOpacity(
                        cue->effect_type, cue->fade_in_start, cue->fade_in_end,
                        cue->fade_out_start, cue->fade_out_end, editor->current_time - item.scene_start_time);
                } else if (item.type == 1) {
                    TextCue* cue = (TextCue*)item.cue;
                    
                    // Evaluate curves for animation
                    float anim_x = cue->x;
                    float anim_y = cue->y;
                    float anim_size = cue->size;
                    float anim_color_r = cue->color.r;
                    float anim_color_g = cue->color.g;
                    float anim_color_b = cue->color.b;
                    
                    // Calculate elapsed time from cue start (convert scene-relative to absolute)
                    float absolute_cue_start = item.scene_start_time + cue->cue_start;
                    float elapsed_time = editor->current_time - absolute_cue_start;
                    if (elapsed_time >= 0.0f) {
                        if (cue->curve_x >= 0 && cue->curve_x < editor->project->curve_count) {
                            rev::curve::Curve* curve = &editor->project->curves[cue->curve_x];
                            float t = elapsed_time / curve->duration;
                            anim_x = rev::curve::Evaluate(*curve, t);
                        }
                        if (cue->curve_y >= 0 && cue->curve_y < editor->project->curve_count) {
                            rev::curve::Curve* curve = &editor->project->curves[cue->curve_y];
                            float t = elapsed_time / curve->duration;
                            anim_y = rev::curve::Evaluate(*curve, t);
                        }
                        if (cue->curve_size >= 0 && cue->curve_size < editor->project->curve_count) {
                            rev::curve::Curve* curve = &editor->project->curves[cue->curve_size];
                            float t = elapsed_time / curve->duration;
                            anim_size = rev::curve::Evaluate(*curve, t);
                        }
                        if (cue->curve_color_r >= 0 && cue->curve_color_r < editor->project->curve_count) {
                            rev::curve::Curve* curve = &editor->project->curves[cue->curve_color_r];
                            float t = elapsed_time / curve->duration;
                            anim_color_r = rev::curve::Evaluate(*curve, t);
                        }
                        if (cue->curve_color_g >= 0 && cue->curve_color_g < editor->project->curve_count) {
                            rev::curve::Curve* curve = &editor->project->curves[cue->curve_color_g];
                            float t = elapsed_time / curve->duration;
                            anim_color_g = rev::curve::Evaluate(*curve, t);
                        }
                        if (cue->curve_color_b >= 0 && cue->curve_color_b < editor->project->curve_count) {
                            rev::curve::Curve* curve = &editor->project->curves[cue->curve_color_b];
                            float t = elapsed_time / curve->duration;
                            anim_color_b = rev::curve::Evaluate(*curve, t);
                        }
                    }
                    
                        rev::runtime::TextEffectFrame fx = {};
                        float scene_time = editor->current_time - item.scene_start_time;
                        if (!rev::runtime::BuildTextEffectFrame(cue, scene_time, &fx)) continue;

                        rev::runtime::TextTexture rt_txt{};
                        if (!rev::runtime::RenderTextToTexture(
                            fx.text, cue->font_name, anim_size,
                            anim_color_r, anim_color_g, anim_color_b, &rt_txt)) continue;
                    tex    = rt_txt.texture_id;
                    norm_w = (float)rt_txt.width  / editor->preview_width  * 2.0f;
                    norm_h = (float)rt_txt.height / editor->preview_height * 2.0f;
                        pos_x  =  ((anim_x + fx.offset_x) * 2.0f) - 1.0f;
                        pos_y  = -(((anim_y + fx.offset_y) * 2.0f) - 1.0f);
                        opacity = rev::runtime::ComputeEffectOpacity(
                        cue->effect_type, cue->fade_in_start, cue->fade_in_end,
                        cue->fade_out_start, cue->fade_out_end, scene_time) * fx.opacity_mul;
                } else {
                    ScrollTextCue* cue = (ScrollTextCue*)item.cue;

                    float anim_x = cue->x;
                    float anim_y = cue->y;
                    float anim_speed = cue->speed;
                    float anim_size = cue->size;
                    float anim_opacity = cue->opacity;
                    float anim_color_r = cue->color.r;
                    float anim_color_g = cue->color.g;
                    float anim_color_b = cue->color.b;
                    float anim_wave_amp = cue->wave_amp;
                    float anim_wave_freq = cue->wave_freq;
                    float anim_jitter_amp = cue->jitter_amp;
                    float anim_jitter_freq = cue->jitter_freq;

                    float absolute_cue_start = item.scene_start_time + cue->cue_start;
                    float elapsed_time = editor->current_time - absolute_cue_start;
                    if (elapsed_time < 0.0f) elapsed_time = 0.0f;

                    if (cue->curve_x >= 0 && cue->curve_x < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_x];
                        float t = elapsed_time / curve->duration;
                        anim_x = rev::curve::Evaluate(*curve, t);
                    }
                    if (cue->curve_y >= 0 && cue->curve_y < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_y];
                        float t = elapsed_time / curve->duration;
                        anim_y = rev::curve::Evaluate(*curve, t);
                    }
                    if (cue->curve_speed >= 0 && cue->curve_speed < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_speed];
                        float t = elapsed_time / curve->duration;
                        anim_speed = rev::curve::Evaluate(*curve, t);
                    }
                    if (cue->curve_size >= 0 && cue->curve_size < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_size];
                        float t = elapsed_time / curve->duration;
                        anim_size = rev::curve::Evaluate(*curve, t);
                    }
                    if (cue->curve_opacity >= 0 && cue->curve_opacity < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_opacity];
                        float t = elapsed_time / curve->duration;
                        anim_opacity = rev::curve::Evaluate(*curve, t);
                    }
                    if (cue->curve_color_r >= 0 && cue->curve_color_r < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_color_r];
                        float t = elapsed_time / curve->duration;
                        anim_color_r = rev::curve::Evaluate(*curve, t);
                    }
                    if (cue->curve_color_g >= 0 && cue->curve_color_g < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_color_g];
                        float t = elapsed_time / curve->duration;
                        anim_color_g = rev::curve::Evaluate(*curve, t);
                    }
                    if (cue->curve_color_b >= 0 && cue->curve_color_b < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_color_b];
                        float t = elapsed_time / curve->duration;
                        anim_color_b = rev::curve::Evaluate(*curve, t);
                    }
                    if (cue->curve_wave_amp >= 0 && cue->curve_wave_amp < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_wave_amp];
                        float t = elapsed_time / curve->duration;
                        anim_wave_amp = rev::curve::Evaluate(*curve, t);
                    }
                    if (cue->curve_wave_freq >= 0 && cue->curve_wave_freq < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_wave_freq];
                        float t = elapsed_time / curve->duration;
                        anim_wave_freq = rev::curve::Evaluate(*curve, t);
                    }
                    if (cue->curve_jitter_amp >= 0 && cue->curve_jitter_amp < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_jitter_amp];
                        float t = elapsed_time / curve->duration;
                        anim_jitter_amp = rev::curve::Evaluate(*curve, t);
                    }
                    if (cue->curve_jitter_freq >= 0 && cue->curve_jitter_freq < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_jitter_freq];
                        float t = elapsed_time / curve->duration;
                        anim_jitter_freq = rev::curve::Evaluate(*curve, t);
                    }

                    auto clamp01 = [](float v) {
                        if (v < 0.0f) return 0.0f;
                        if (v > 1.0f) return 1.0f;
                        return v;
                    };

                    float scene_time = editor->current_time - item.scene_start_time;
                    float travel = 1.0f + cue->wrap_gap;
                    if (travel < 0.001f) travel = 0.001f;
                    float wrapped = elapsed_time * anim_speed;
                    if (cue->loop_mode == 0) {
                        float speed_abs = fabsf(anim_speed);
                        if (speed_abs < 0.0001f) speed_abs = 0.0001f;
                        float raw_cycle_duration = travel / speed_abs;
                        float cue_duration = cue->cue_end - cue->cue_start;
                        float loop_cycle_duration = raw_cycle_duration;
                        if (cue_duration > loop_cycle_duration) {
                            loop_cycle_duration = cue_duration;
                        }
                        if (loop_cycle_duration < 0.0001f) {
                            loop_cycle_duration = raw_cycle_duration;
                        }
                        float local_time = fmodf(elapsed_time, loop_cycle_duration);
                        if (local_time < 0.0f) local_time += loop_cycle_duration;
                        wrapped = local_time * anim_speed;
                    } else {
                        if (wrapped < 0.0f) wrapped = 0.0f;
                        if (wrapped > travel) wrapped = travel;
                    }

                    float dir_x = 0.0f;
                    float dir_y = 0.0f;
                    if (cue->direction == 0) dir_x = -1.0f;
                    else if (cue->direction == 1) dir_x = 1.0f;
                    else if (cue->direction == 2) dir_y = -1.0f;
                    else dir_y = 1.0f;

                    float wave_phase = elapsed_time * anim_wave_freq * 6.2831853f;
                    float jitter_phase = elapsed_time * anim_jitter_freq * 6.2831853f;
                    float wave_offset = sinf(wave_phase) * anim_wave_amp;
                    float jitter_x = sinf(jitter_phase * 1.7f) * anim_jitter_amp;
                    float jitter_y = cosf(jitter_phase * 1.3f) * anim_jitter_amp;
                    float distortion = cue->distortion * sinf(elapsed_time * 17.0f);

                    float glow_boost = cue->glow * 0.28f;
                    float chroma = cue->chroma_shift;
                    float draw_r = clamp01(anim_color_r + glow_boost + chroma * 0.25f);
                    float draw_g = clamp01(anim_color_g + glow_boost * 0.8f);
                    float draw_b = clamp01(anim_color_b + glow_boost + chroma * 0.6f);

                    float effective_size = anim_size * (1.0f + cue->outline * 0.08f + fabsf(distortion) * 0.05f);
                    if (effective_size < 4.0f) effective_size = 4.0f;

                    char scroll_text_buffer[512] = {};
                    if (cue->direction <= 1) {
                        snprintf(scroll_text_buffer, sizeof(scroll_text_buffer), "%s   %s", cue->text, cue->text);
                    } else {
                        strncpy_s(scroll_text_buffer, sizeof(scroll_text_buffer), cue->text, _TRUNCATE);
                    }

                    rev::runtime::TextTexture rt_txt{};
                    if (!rev::runtime::RenderTextToTexture(scroll_text_buffer, cue->font_name, effective_size,
                        draw_r, draw_g, draw_b, &rt_txt)) {
                        continue;
                    }

                    tex = rt_txt.texture_id;
                    float spacing_mul = (cue->spacing <= 0.01f) ? 0.01f : cue->spacing;
                    norm_w = ((float)rt_txt.width * spacing_mul) / editor->preview_width * 2.0f;
                    norm_h = (float)rt_txt.height / editor->preview_height * 2.0f;

                    float scroll_x = anim_x + dir_x * wrapped + jitter_x + distortion;
                    float scroll_y = anim_y + dir_y * wrapped + jitter_y;
                    if (cue->direction <= 1) scroll_y += wave_offset;
                    else scroll_x += wave_offset;

                    pos_x = (scroll_x * 2.0f) - 1.0f;
                    pos_y = -((scroll_y * 2.0f) - 1.0f);

                    float fade_mul = rev::runtime::ComputeEffectOpacity(
                        1, cue->fade_in_start, cue->fade_in_end,
                        cue->fade_out_start, cue->fade_out_end,
                        scene_time);
                    float style_mul = 1.0f + cue->shadow * 0.1f;
                    opacity = clamp01(anim_opacity * fade_mul * style_mul);
                }

                if (glActiveTexture_fn) glActiveTexture_fn(0x84C0); // GL_TEXTURE0
                glBindTexture(GL_TEXTURE_2D, tex);
                if (sp_tex >= 0) rev::shader::SetInt(sprite_prog, sp_tex, 0);
                if (sp_pos >= 0) rev::shader::SetVec2(sprite_prog, sp_pos, pos_x, pos_y);
                if (sp_sz  >= 0) rev::shader::SetVec2(sprite_prog, sp_sz, norm_w, norm_h);
                if (sp_opa >= 0) rev::shader::SetFloat(sprite_prog, sp_opa, opacity);
                glDrawArrays(GL_TRIANGLES, 0, 3);
                glDeleteTextures(1, &tex);

            } else {
                // Mesh (type == 2)
                if (!mesh_prog) continue;
                if (!depth_on) {
                    glEnable(0x0B71);                       // GL_DEPTH_TEST
                    if (glDepthFunc_fn) glDepthFunc_fn(0x0201); // GL_LESS
                    depth_on = true;
                }
                rev::shader::Use(mesh_prog);

                MeshCue* cue = (MeshCue*)item.cue;
                float opacity = rev::runtime::ComputeEffectOpacity(
                    cue->effect_type, cue->fade_in_start, cue->fade_in_end,
                    cue->fade_out_start, cue->fade_out_end, editor->current_time - item.scene_start_time);

                // Animated transform and material properties (start with cue values)
                float anim_pos[3] = {cue->pos[0], cue->pos[1], cue->pos[2]};
                float anim_rot[3] = {cue->rot[0], cue->rot[1], cue->rot[2]};
                float anim_scale[3] = {cue->scale[0], cue->scale[1], cue->scale[2]};
                float anim_color[4] = {cue->color[0], cue->color[1], cue->color[2], cue->color[3]};
                float anim_metallic = cue->metallic;
                float anim_roughness = cue->roughness;
                float anim_mesh_size = cue->mesh_size;
                
                // Calculate elapsed time from cue start for curve evaluation (convert scene-relative to absolute)
                float absolute_cue_start = item.scene_start_time + cue->cue_start;
                float elapsed_time = editor->current_time - absolute_cue_start;
                if (elapsed_time >= 0.0f) {
                    // Position curves
                    if (cue->curve_pos_x >= 0 && cue->curve_pos_x < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_pos_x];
                        float t = elapsed_time / curve->duration;
                        anim_pos[0] = rev::curve::Evaluate(*curve, t);
                    }
                    if (cue->curve_pos_y >= 0 && cue->curve_pos_y < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_pos_y];
                        float t = elapsed_time / curve->duration;
                        anim_pos[1] = rev::curve::Evaluate(*curve, t);
                    }
                    if (cue->curve_pos_z >= 0 && cue->curve_pos_z < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_pos_z];
                        float t = elapsed_time / curve->duration;
                        anim_pos[2] = rev::curve::Evaluate(*curve, t);
                    }
                    // Rotation curves
                    if (cue->curve_rot_x >= 0 && cue->curve_rot_x < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_rot_x];
                        float t = elapsed_time / curve->duration;
                        anim_rot[0] = rev::curve::Evaluate(*curve, t);
                    }
                    if (cue->curve_rot_y >= 0 && cue->curve_rot_y < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_rot_y];
                        float t = elapsed_time / curve->duration;
                        anim_rot[1] = rev::curve::Evaluate(*curve, t);
                    }
                    if (cue->curve_rot_z >= 0 && cue->curve_rot_z < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_rot_z];
                        float t = elapsed_time / curve->duration;
                        anim_rot[2] = rev::curve::Evaluate(*curve, t);
                    }
                    // Scale curves
                    if (cue->curve_scale_x >= 0 && cue->curve_scale_x < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_scale_x];
                        float t = elapsed_time / curve->duration;
                        anim_scale[0] = rev::curve::Evaluate(*curve, t);
                    }
                    if (cue->curve_scale_y >= 0 && cue->curve_scale_y < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_scale_y];
                        float t = elapsed_time / curve->duration;
                        anim_scale[1] = rev::curve::Evaluate(*curve, t);
                    }
                    if (cue->curve_scale_z >= 0 && cue->curve_scale_z < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_scale_z];
                        float t = elapsed_time / curve->duration;
                        anim_scale[2] = rev::curve::Evaluate(*curve, t);
                    }
                    // Color curves
                    if (cue->curve_color_r >= 0 && cue->curve_color_r < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_color_r];
                        float t = elapsed_time / curve->duration;
                        anim_color[0] = rev::curve::Evaluate(*curve, t);
                    }
                    if (cue->curve_color_g >= 0 && cue->curve_color_g < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_color_g];
                        float t = elapsed_time / curve->duration;
                        anim_color[1] = rev::curve::Evaluate(*curve, t);
                    }
                    if (cue->curve_color_b >= 0 && cue->curve_color_b < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_color_b];
                        float t = elapsed_time / curve->duration;
                        anim_color[2] = rev::curve::Evaluate(*curve, t);
                    }
                    if (cue->curve_color_a >= 0 && cue->curve_color_a < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_color_a];
                        float t = elapsed_time / curve->duration;
                        anim_color[3] = rev::curve::Evaluate(*curve, t);
                    }
                    // Material curves
                    if (cue->curve_metallic >= 0 && cue->curve_metallic < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_metallic];
                        float t = elapsed_time / curve->duration;
                        anim_metallic = rev::curve::Evaluate(*curve, t);
                    }
                    if (cue->curve_roughness >= 0 && cue->curve_roughness < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_roughness];
                        float t = elapsed_time / curve->duration;
                        anim_roughness = rev::curve::Evaluate(*curve, t);
                    }
                    // Mesh size curve
                    if (cue->curve_mesh_size >= 0 && cue->curve_mesh_size < editor->project->curve_count) {
                        rev::curve::Curve* curve = &editor->project->curves[cue->curve_mesh_size];
                        float t = elapsed_time / curve->duration;
                        anim_mesh_size = rev::curve::Evaluate(*curve, t);
                    }
                }

                float model[16];
                rev::runtime::Mat4Model(model, anim_pos, anim_rot, anim_scale);  // Use animated transform
                if (glUniformMatrix4fv) glUniformMatrix4fv(mp_model, 1, 0, model);
                if (glUniform4fv_fn) {
                    float col[4] = {anim_color[0], anim_color[1], anim_color[2], anim_color[3]*opacity};
                    glUniform4fv_fn(mp_col, 1, col);
                }
                if (mp_metal >= 0) rev::shader::SetFloat(mesh_prog, mp_metal, anim_metallic);
                if (mp_rough >= 0) rev::shader::SetFloat(mesh_prog, mp_rough, anim_roughness);

                float cue_col[4] = {anim_color[0], anim_color[1], anim_color[2], anim_color[3] * opacity};

                bool mesh_needs_blend = (cue_col[3] < 0.999f);

                float size  = anim_mesh_size > 0.0f ? anim_mesh_size : 1.0f;
                float param = cue->mesh_param > 0.0f ? cue->mesh_param : 16.0f;
                rev::mesh::Mesh* mesh = nullptr;
                switch (cue->mesh_type) {
                    case 0: mesh = rev::mesh::CreateCube(size); break;
                    case 1: mesh = rev::mesh::CreateSphere(size, (int)param); break;
                    case 2: mesh = rev::mesh::CreatePlane(size, param > 0.0f ? param : size); break;
                    case 3: mesh = rev::mesh::CreateTorus(size, param > 0.0f ? param : 0.3f, 32, 16); break;
                    case 4: {
                        if (cue->asset_path[0]) {
                            // Check cache for this mesh
                            uint64_t current_file_time = GetFileModificationTime(cue->asset_path);
                            rev::mesh::Mesh* cached = nullptr;
                            int cached_index = -1;
                            
                            for (int c = 0; c < editor->mesh_cache_count; ++c) {
                                if (strcmp(editor->mesh_cache[c].path, cue->asset_path) == 0) {
                                    // Found in cache - check if file has been modified
                                    if (editor->mesh_cache[c].last_write_time == current_file_time) {
                                        cached = (rev::mesh::Mesh*)editor->mesh_cache[c].mesh;
                                        cached_index = c;
                                    } else {
                                        // File modified - invalidate cache entry
                                        printf("[Cache] File modified, reloading: %s\n", cue->asset_path);
                                        if (editor->mesh_cache[c].mesh) {
                                            rev::mesh::DestroyMesh((rev::mesh::Mesh*)editor->mesh_cache[c].mesh);
                                            editor->mesh_cache[c].mesh = nullptr;
                                        }
                                    }
                                    break;
                                }
                            }
                            
                            if (cached) {
                                // Evaluate and apply animation transform based on scene time.
                                // Imported multi-node meshes use per-node delta matrices later during slot draws.
                                float* node_delta_mats = nullptr;
                                if (cached->current_animation >= 0) {
                                    rev::gltf::Animation* anims = (rev::gltf::Animation*)cached->animation_data;
                                    if (anims && (!cached->imported_nodes || cached->imported_node_count == 0)) {
                                        // Calculate animation time relative to cue start time
                                        float absolute_anim_start = absolute_cue_start;
                                        float absolute_fade_in_start = item.scene_start_time + cue->fade_in_start;
                                        if (absolute_fade_in_start > absolute_anim_start) {
                                            absolute_anim_start = absolute_fade_in_start;
                                        }
                                        float anim_time = editor->current_time - absolute_anim_start;
                                        
                                        // Handle looping
                                        if (cached->animation_loop && anims[cached->current_animation].duration > 0.0f) {
                                            float duration = anims[cached->current_animation].duration;
                                            if (anim_time < 0.0f) {
                                                anim_time = 0.0f;
                                            } else if (anim_time > duration) {
                                                anim_time = (float)std::fmod(anim_time, duration);
                                            }
                                        } else {
                                            // Clamp to animation duration if not looping
                                            if (anim_time < 0.0f) anim_time = 0.0f;
                                            if (anim_time > anims[cached->current_animation].duration) {
                                                anim_time = anims[cached->current_animation].duration;
                                            }
                                        }
                                        
                                        float translation[3], rotation[4], scale[3];
                                        rev::gltf::EvaluateAnimation(&anims[cached->current_animation],
                                                                    anim_time,
                                                                    translation, rotation, scale);
                                        rev::gltf::ApplyAnimationTransform(anim_pos, anim_rot, anim_scale,
                                                                          translation, rotation, scale);
                                        // Rebuild model matrix with animated transform
                                        rev::runtime::Mat4Model(model, anim_pos, anim_rot, anim_scale);
                                        if (glUniformMatrix4fv) glUniformMatrix4fv(mp_model, 1, 0, model);
                                    }
                                    if (anims && cached->animation_count > 0 && cached->imported_nodes && cached->imported_node_count > 0) {
                                        float absolute_anim_start = absolute_cue_start;
                                        float absolute_fade_in_start = item.scene_start_time + cue->fade_in_start;
                                        if (absolute_fade_in_start > absolute_anim_start) {
                                            absolute_anim_start = absolute_fade_in_start;
                                        }
                                        float anim_time = editor->current_time - absolute_anim_start;
                                        if (anim_time < 0.0f) anim_time = 0.0f;
                                        node_delta_mats = new float[cached->imported_node_count * 16];
                                        if (!rev::gltf::BuildAnimatedNodeDeltaMatricesAll(cached,
                                                                                          anims,
                                                                                          cached->animation_count,
                                                                                          anim_time,
                                                                                          cached->animation_loop,
                                                                                          node_delta_mats,
                                                                                          (int)cached->imported_node_count)) {
                                            delete[] node_delta_mats;
                                            node_delta_mats = nullptr;
                                        }
                                    }
                                }

                                float draw_light[3] = {3.0f, 5.0f, 4.0f};
                                if (cached->has_imported_light) {
                                    draw_light[0] = cached->imported_light_pos[0];
                                    draw_light[1] = cached->imported_light_pos[1];
                                    draw_light[2] = cached->imported_light_pos[2];
                                }
                                if (mp_light >= 0) rev::shader::SetVec3(mesh_prog, mp_light, draw_light[0], draw_light[1], draw_light[2]);

                                int loc_has_tex = rev::shader::GetUniformLocation(mesh_prog, "u_has_texture");
                                int loc_tex = rev::shader::GetUniformLocation(mesh_prog, "u_base_color_texture");
                                for (uint32_t si = 0; si < cached->material_slot_count; ++si) {
                                    const rev::mesh::MaterialSlot& slot = cached->material_slots[si];
                                    if (((slot.diffuse_color >> 24) & 0xFF) < 255) {
                                        mesh_needs_blend = true;
                                        break;
                                    }
                                }
                                if (mesh_needs_blend) {
                                    if (!blend_on) {
                                        glEnable(GL_BLEND);
                                        blend_on = true;
                                    }
                                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                                    if (glDepthMask_fn) glDepthMask_fn(0); // GL_FALSE
                                } else {
                                    if (blend_on) { glDisable(GL_BLEND); blend_on = false; }
                                    if (glDepthMask_fn) glDepthMask_fn(1); // GL_TRUE
                                }

                                bool rendered_slot = false;
                                for (uint32_t si = 0; si < cached->material_slot_count; ++si) {
                                    const rev::mesh::MaterialSlot& slot = cached->material_slots[si];
                                    if (mp_col >= 0 && glUniform4fv_fn) {
                                        float slot_r = (float)((slot.diffuse_color >> 0)  & 0xFF) / 255.0f;
                                        float slot_g = (float)((slot.diffuse_color >> 8)  & 0xFF) / 255.0f;
                                        float slot_b = (float)((slot.diffuse_color >> 16) & 0xFF) / 255.0f;
                                        float slot_a = (float)((slot.diffuse_color >> 24) & 0xFF) / 255.0f;
                                        float col[4] = {
                                            cue_col[0] * slot_r,
                                            cue_col[1] * slot_g,
                                            cue_col[2] * slot_b,
                                            cue_col[3] * slot_a
                                        };
                                        glUniform4fv_fn(mp_col, 1, col);
                                    }

                                    if (glUniformMatrix4fv && mp_model >= 0) {
                                        if (node_delta_mats && slot.source_node_index >= 0 &&
                                            slot.source_node_index < (int)cached->imported_node_count) {
                                            float slot_model[16] = {};
                                            rev::runtime::Mat4Multiply(slot_model, model, &node_delta_mats[slot.source_node_index * 16]);
                                            glUniformMatrix4fv(mp_model, 1, 0, slot_model);
                                        } else {
                                            glUniformMatrix4fv(mp_model, 1, 0, model);
                                        }
                                    }

                                    unsigned int tex_id = slot.base_color_texture;
                                    if (tex_id == 0) tex_id = cached->base_color_texture;
                                    if (tex_id != 0) {
                                        glBindTexture(0x0DE1, tex_id); // GL_TEXTURE_2D
                                        if (loc_tex >= 0) rev::shader::SetInt(mesh_prog, loc_tex, 0);
                                        if (loc_has_tex >= 0) rev::shader::SetInt(mesh_prog, loc_has_tex, 1);
                                    } else if (loc_has_tex >= 0) {
                                        rev::shader::SetInt(mesh_prog, loc_has_tex, 0);
                                    }
                                    rev::mesh::Render(cached, (int)si);
                                    rendered_slot = true;
                                }
                                if (!rendered_slot) {
                                    if (glUniformMatrix4fv && mp_model >= 0) {
                                        glUniformMatrix4fv(mp_model, 1, 0, model);
                                    }
                                    if (mp_col >= 0 && glUniform4fv_fn) {
                                        glUniform4fv_fn(mp_col, 1, cue_col);
                                    }
                                    if (cached->base_color_texture != 0) {
                                        glBindTexture(0x0DE1, cached->base_color_texture);
                                        if (loc_tex >= 0) rev::shader::SetInt(mesh_prog, loc_tex, 0);
                                        if (loc_has_tex >= 0) rev::shader::SetInt(mesh_prog, loc_has_tex, 1);
                                    } else if (loc_has_tex >= 0) {
                                        rev::shader::SetInt(mesh_prog, loc_has_tex, 0);
                                    }
                                    rev::mesh::Render(cached, -1);
                                }
                                delete[] node_delta_mats;
                                continue;
                            }
                            // Extract textures to assets folder
                            rev::gltf::ImportResult* ir = rev::gltf::LoadMesh(cue->asset_path, editor->project->assets_path);
                            if (ir && ir->ok) {
                                mesh = ir->mesh;
                                ir->mesh = nullptr;

                                if (mesh) {
                                    mesh->has_imported_light = ir->has_light;
                                    mesh->imported_light_pos[0] = ir->light_pos[0];
                                    mesh->imported_light_pos[1] = ir->light_pos[1];
                                    mesh->imported_light_pos[2] = ir->light_pos[2];
                                }
                                
                                // Load base color texture if present
                                if (ir->material.base_color_texture[0] != '\0') {
                                    rev::runtime::ImageTexture tex{};
                                    if (rev::runtime::LoadImageTexture(ir->material.base_color_texture, &tex)) {
                                        mesh->base_color_texture = tex.texture_id;
                                    }
                                }

                                // Load per-material textures and map to material slots.
                                if (mesh && ir->material_count > 0 && ir->materials) {
                                    unsigned int* material_textures = new unsigned int[ir->material_count];
                                    memset(material_textures, 0, sizeof(unsigned int) * ir->material_count);
                                    for (int mat_i = 0; mat_i < ir->material_count; ++mat_i) {
                                        const char* tex_path = ir->materials[mat_i].base_color_texture;
                                        if (tex_path[0] == '\0') continue;
                                        rev::runtime::ImageTexture tex{};
                                        if (rev::runtime::LoadImageTexture(tex_path, &tex)) {
                                            material_textures[mat_i] = tex.texture_id;
                                        }
                                    }
                                    for (uint32_t si = 0; si < mesh->material_slot_count; ++si) {
                                        rev::mesh::MaterialSlot& slot = mesh->material_slots[si];
                                        if (slot.material_index >= 0 && slot.material_index < ir->material_count) {
                                            slot.base_color_texture = material_textures[slot.material_index];
                                        }
                                    }
                                    delete[] material_textures;
                                }
                                
                                // Transfer animations to mesh
                                if (ir->animation_count > 0) {
                                    mesh->animation_data = ir->animations;
                                    mesh->animation_count = ir->animation_count;
                                    mesh->current_animation = 0;  // Start first animation
                                    mesh->animation_time = 0.0f;
                                    mesh->animation_speed = 1.0f;
                                    mesh->animation_loop = true;
                                    ir->animations = nullptr;  // Transfer ownership
                                }
                            }
                            if (ir) rev::gltf::FreeImportResult(ir);
                            if (mesh) {
                                rev::mesh::UploadToGPU(mesh);
                                
                                // Add to cache (or update existing slot if we just invalidated one)
                                int cache_slot = -1;
                                for (int c = 0; c < editor->mesh_cache_count; ++c) {
                                    if (editor->mesh_cache[c].mesh == nullptr) {
                                        cache_slot = c; // Reuse invalidated slot
                                        break;
                                    }
                                }
                                if (cache_slot < 0 && editor->mesh_cache_count < EditorContext::kMeshCacheSize) {
                                    cache_slot = editor->mesh_cache_count++;
                                }
                                
                                if (cache_slot >= 0) {
                                    auto& entry = editor->mesh_cache[cache_slot];
                                    strncpy_s(entry.path, cue->asset_path, _TRUNCATE);
                                    entry.mesh = mesh;
                                    entry.last_write_time = current_file_time;
                                }
                                
                                float draw_light[3] = {3.0f, 5.0f, 4.0f};
                                if (mesh->has_imported_light) {
                                    draw_light[0] = mesh->imported_light_pos[0];
                                    draw_light[1] = mesh->imported_light_pos[1];
                                    draw_light[2] = mesh->imported_light_pos[2];
                                }
                                if (mp_light >= 0) rev::shader::SetVec3(mesh_prog, mp_light, draw_light[0], draw_light[1], draw_light[2]);

                                float* node_delta_mats = nullptr;
                                if (mesh->animation_data && mesh->animation_count > 0 && mesh->imported_nodes && mesh->imported_node_count > 0) {
                                    rev::gltf::Animation* anims = (rev::gltf::Animation*)mesh->animation_data;
                                    if (anims) {
                                        float absolute_anim_start = absolute_cue_start;
                                        float absolute_fade_in_start = item.scene_start_time + cue->fade_in_start;
                                        if (absolute_fade_in_start > absolute_anim_start) {
                                            absolute_anim_start = absolute_fade_in_start;
                                        }
                                        float anim_time = editor->current_time - absolute_anim_start;
                                        if (anim_time < 0.0f) anim_time = 0.0f;
                                        node_delta_mats = new float[mesh->imported_node_count * 16];
                                        if (!rev::gltf::BuildAnimatedNodeDeltaMatricesAll(mesh,
                                                                                          anims,
                                                                                          mesh->animation_count,
                                                                                          anim_time,
                                                                                          mesh->animation_loop,
                                                                                          node_delta_mats,
                                                                                          (int)mesh->imported_node_count)) {
                                            delete[] node_delta_mats;
                                            node_delta_mats = nullptr;
                                        }
                                    }
                                }

                                int loc_has_tex = rev::shader::GetUniformLocation(mesh_prog, "u_has_texture");
                                int loc_tex = rev::shader::GetUniformLocation(mesh_prog, "u_base_color_texture");
                                for (uint32_t si = 0; si < mesh->material_slot_count; ++si) {
                                    const rev::mesh::MaterialSlot& slot = mesh->material_slots[si];
                                    if (((slot.diffuse_color >> 24) & 0xFF) < 255) {
                                        mesh_needs_blend = true;
                                        break;
                                    }
                                }
                                if (mesh_needs_blend) {
                                    if (!blend_on) {
                                        glEnable(GL_BLEND);
                                        blend_on = true;
                                    }
                                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                                    if (glDepthMask_fn) glDepthMask_fn(0); // GL_FALSE
                                } else {
                                    if (blend_on) { glDisable(GL_BLEND); blend_on = false; }
                                    if (glDepthMask_fn) glDepthMask_fn(1); // GL_TRUE
                                }

                                bool rendered_slot = false;
                                for (uint32_t si = 0; si < mesh->material_slot_count; ++si) {
                                    const rev::mesh::MaterialSlot& slot = mesh->material_slots[si];
                                    if (mp_col >= 0 && glUniform4fv_fn) {
                                        float slot_r = (float)((slot.diffuse_color >> 0)  & 0xFF) / 255.0f;
                                        float slot_g = (float)((slot.diffuse_color >> 8)  & 0xFF) / 255.0f;
                                        float slot_b = (float)((slot.diffuse_color >> 16) & 0xFF) / 255.0f;
                                        float slot_a = (float)((slot.diffuse_color >> 24) & 0xFF) / 255.0f;
                                        float col[4] = {
                                            cue_col[0] * slot_r,
                                            cue_col[1] * slot_g,
                                            cue_col[2] * slot_b,
                                            cue_col[3] * slot_a
                                        };
                                        glUniform4fv_fn(mp_col, 1, col);
                                    }

                                    if (glUniformMatrix4fv && mp_model >= 0) {
                                        if (node_delta_mats && slot.source_node_index >= 0 &&
                                            slot.source_node_index < (int)mesh->imported_node_count) {
                                            float slot_model[16] = {};
                                            rev::runtime::Mat4Multiply(slot_model, model, &node_delta_mats[slot.source_node_index * 16]);
                                            glUniformMatrix4fv(mp_model, 1, 0, slot_model);
                                        } else {
                                            glUniformMatrix4fv(mp_model, 1, 0, model);
                                        }
                                    }

                                    unsigned int tex_id = slot.base_color_texture;
                                    if (tex_id == 0) tex_id = mesh->base_color_texture;
                                    if (tex_id != 0) {
                                        glBindTexture(0x0DE1, tex_id); // GL_TEXTURE_2D
                                        if (loc_tex >= 0) rev::shader::SetInt(mesh_prog, loc_tex, 0);
                                        if (loc_has_tex >= 0) rev::shader::SetInt(mesh_prog, loc_has_tex, 1);
                                    } else if (loc_has_tex >= 0) {
                                        rev::shader::SetInt(mesh_prog, loc_has_tex, 0);
                                    }
                                    rev::mesh::Render(mesh, (int)si);
                                    rendered_slot = true;
                                }
                                if (!rendered_slot) {
                                    if (glUniformMatrix4fv && mp_model >= 0) {
                                        glUniformMatrix4fv(mp_model, 1, 0, model);
                                    }
                                    if (mp_col >= 0 && glUniform4fv_fn) {
                                        glUniform4fv_fn(mp_col, 1, cue_col);
                                    }
                                    if (mesh->base_color_texture != 0) {
                                        glBindTexture(0x0DE1, mesh->base_color_texture);
                                        if (loc_tex >= 0) rev::shader::SetInt(mesh_prog, loc_tex, 0);
                                        if (loc_has_tex >= 0) rev::shader::SetInt(mesh_prog, loc_has_tex, 1);
                                    } else if (loc_has_tex >= 0) {
                                        rev::shader::SetInt(mesh_prog, loc_has_tex, 0);
                                    }
                                    rev::mesh::Render(mesh, -1);
                                }
                                delete[] node_delta_mats;
                                continue;
                            }
                        }
                        mesh = rev::mesh::CreateCube(1.0f);
                        break;
                    }
                    default: mesh = rev::mesh::CreateCube(1.0f); break;
                }
                if (mesh) {
                    rev::mesh::UploadToGPU(mesh);
                    // Procedural meshes don't have textures
                    if (mp_light >= 0) rev::shader::SetVec3(mesh_prog, mp_light, 3.0f, 5.0f, 4.0f);
                    int loc_has_tex = rev::shader::GetUniformLocation(mesh_prog, "u_has_texture");
                    if (blend_on) { glDisable(GL_BLEND); blend_on = false; }
                    if (glDepthMask_fn) glDepthMask_fn(1); // GL_TRUE
                    if (loc_has_tex >= 0) rev::shader::SetInt(mesh_prog, loc_has_tex, 0);
                    rev::mesh::Render(mesh, -1);
                    rev::mesh::DestroyMesh(mesh);
                }
            }
        }

        if (blend_on) glDisable(GL_BLEND);
        if (depth_on) {
            if (glDepthMask_fn) glDepthMask_fn(1); // GL_TRUE
            glDisable(0x0B71); // GL_DEPTH_TEST
        }
    }

    // Unbind framebuffer (restore default)
    glBindFramebuffer(0x8D40, 0);
}

void UpdatePlayback(EditorContext* editor, float delta_time) {
    if (!editor || !editor->playing) return;
    
    editor->current_time += delta_time;
    
    // Clamp to project duration (use 10s default if duration is 0)
    if (editor->project) {
        float max_duration = editor->project->total_duration;
        if (max_duration <= 0.0f) max_duration = 10.0f; // Default playback duration
        
        if (editor->current_time >= max_duration) {
            editor->current_time = max_duration;
            editor->playing = false; // Stop at end
        }
    }
}

void RenderPreviewPanel(EditorContext* editor) {
    if (!editor || !editor->show_preview) return;
    
    ImGui::Begin("Preview", &editor->show_preview);
    
    // Initialize preview on first show
    if (!editor->preview_initialized) {
        InitializePreview(editor, 1920, 1080);
    }
    
    // Check if project is loaded
    if (!editor->project) {
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "No project loaded");
        ImGui::Text("Create or open a project to use the preview.");
        ImGui::End();
        return;
    }
    
    // Playback controls
    if (ImGui::Button(editor->playing ? "Pause" : "Play")) {
        editor->playing = !editor->playing;
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop")) {
        editor->playing = false;
        editor->current_time = 0.0f;
        // Mesh animations now driven by scene time, no need to reset them individually
    }
    ImGui::SameLine();
    
    // Show duration info
    float display_duration = editor->project->total_duration;
    if (display_duration <= 0.0f) {
        ImGui::Text("Time: %.2fs / %.2fs (no scenes, using default)", editor->current_time, 10.0f);
    } else {
        ImGui::Text("Time: %.2fs / %.2fs", editor->current_time, display_duration);
    }
    
    // Time slider
    float max_time = editor->project->total_duration;
    if (max_time <= 0.0f) max_time = 10.0f; // Default if no duration
    
    if (ImGui::SliderFloat("##timeline_scrub", &editor->current_time, 0.0f, max_time, "%.2fs")) {
        // Pause when manually scrubbing
        if (editor->playing) {
            editor->playing = false;
        }
    }

    // Show playback mode and which music cue would be active at current preview time.
    const char* active_music_key = "(none)";
    if (editor->project->scene_count > 0) {
        float scene_start_time = 0.0f;
        float t = editor->current_time;
        for (int s = 0; s < editor->project->scene_count; ++s) {
            SceneBlock* scene = &editor->project->scenes[s];
            float scene_end_time = scene_start_time + scene->duration;
            bool in_scene = (s == editor->project->scene_count - 1)
                ? (t >= scene_start_time && t <= scene_end_time)
                : (t >= scene_start_time && t < scene_end_time);
            if (in_scene) {
                float local_t = t - scene_start_time;
                float latest_start = -1.0f;
                for (int i = 0; i < scene->music_cue_count; ++i) {
                    MusicCue* cue = &scene->music_cues[i];
                    float cue_end = (cue->cue_end < 0.0f) ? scene->duration : cue->cue_end;
                    if (local_t >= cue->cue_start && local_t <= cue_end) {
                        if (cue->cue_start >= latest_start) {
                            latest_start = cue->cue_start;
                            active_music_key = (cue->asset_key[0] != '\0') ? cue->asset_key : "(no key)";
                        }
                    }
                }
                break;
            }
            scene_start_time = scene_end_time;
        }
    }

    ImGui::Text("Playback Mode: Intro %s | Music %s",
        editor->project->loop_intro ? "Loop" : "One-shot",
        editor->project->loop_music ? "Loop" : "One-shot");
    ImGui::Text("Active Music Cue @ %.2fs: %s", editor->current_time, active_music_key);
    
    ImGui::Separator();
    
    // Display preview texture
    if (editor->preview_texture) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        
        // Calculate size maintaining 16:9 aspect ratio
        float aspect = 16.0f / 9.0f;
        float w = avail.x;
        float h = w / aspect;
        
        if (h > avail.y) {
            h = avail.y;
            w = h * aspect;
        }

        // Center the preview
        float offset_x = (avail.x - w) * 0.5f;
        if (offset_x > 0.0f) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset_x);
        }
        
        // FBO stays at 1920x1080 — ImGui scales the texture to fit the panel.
        // All size formulas (tex_pixels / preview_width * 2) produce the same
        // proportional result as the final product at every panel size.
        ImGui::Image((ImTextureID)(intptr_t)editor->preview_texture, ImVec2(w, h), ImVec2(0, 1), ImVec2(1, 0));
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Preview not initialized");
    }
    
    ImGui::End();
}


void RandomizeShaderValues(ShaderCue* cue) {
    if (!cue) return;
    
    cue->speed = 0.5f + (float)rand() / RAND_MAX * 1.5f;      // 0.5-2.0
    cue->intensity = 0.5f + (float)rand() / RAND_MAX * 1.0f;  // 0.5-1.5
    cue->warp = (float)rand() / RAND_MAX;                     // 0.0-1.0
}

void ResetShaderValues(ShaderCue* cue) {
    if (!cue) return;
    
    // Default palette - visible colors (shader ID 0 can be set to black for fades)
    cue->palette_low = {0.1f, 0.3f, 0.8f};    // Blue
    cue->palette_mid = {0.8f, 0.4f, 0.2f};    // Orange
    cue->palette_high = {1.0f, 0.9f, 0.7f};   // Warm white
    
    // Default parameters
    cue->speed = 1.0f;
    cue->intensity = 1.0f;
    cue->warp = 0.5f;
    cue->exposure_base = 0.76f;
    cue->exposure_ramp = 0.02f;
    cue->fade_base = 1.0f;  // Full opacity (shaders use this as alpha channel)
    cue->fade_ramp = -0.04f;
    
    // Default timing
    cue->cue_start = 0.0f;
    cue->cue_end = -1.0f;  // Implicit scene end
    cue->fade_in = 0.5f;
    cue->fade_out = 0.5f;
    
    // Default layer
    cue->layer_role = 0;  // Background
    cue->opacity = 1.0f;
    cue->blend_mode = 0;  // Alpha
    cue->layer_order = 0;
    
    // No curves assigned
    cue->curve_speed = -1;
    cue->curve_intensity = -1;
    cue->curve_warp = -1;
    cue->curve_exposure = -1;
    cue->curve_fade = -1;
    cue->curve_palette_low_r = -1;
    cue->curve_palette_low_g = -1;
    cue->curve_palette_low_b = -1;
    cue->curve_palette_mid_r = -1;
    cue->curve_palette_mid_g = -1;
    cue->curve_palette_mid_b = -1;
    cue->curve_palette_high_r = -1;
    cue->curve_palette_high_g = -1;
    cue->curve_palette_high_b = -1;
    cue->curve_opacity = -1;
    cue->curve_exposure_ramp = -1;
    cue->curve_fade_ramp = -1;
}

}  // namespace editor
}  // namespace rev
