#include <windows.h>
#include <gl/gl.h>
#include <gdiplus.h>
#include "rev_platform.h"
#include "rev_shader.h"
#include "rev_xm.h"
#include <cstdio>
#include <cstring>

#pragma comment(lib, "gdiplus.lib")

// Global debug log file
static FILE* g_logfile = nullptr;

// Define missing GL constants
#ifndef GL_COLOR_BUFFER_BIT
#define GL_COLOR_BUFFER_BIT 0x00004000
#endif
#ifndef GL_TRIANGLE_STRIP
#define GL_TRIANGLE_STRIP 0x0005
#endif
#ifndef GL_CLAMP
#define GL_CLAMP 0x2900
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#endif

// GL 3.3 function pointers (VAO support required for core profile)
typedef void (APIENTRY *PFNGLGENVERTEXARRAYSPROC)(GLsizei n, GLuint* arrays);
typedef void (APIENTRY *PFNGLBINDVERTEXARRAYPROC)(GLuint array);
typedef void (APIENTRY *PFNGLACTIVETEXTUREPROC)(GLenum texture);

static PFNGLGENVERTEXARRAYSPROC glGenVertexArrays = nullptr;
static PFNGLBINDVERTEXARRAYPROC glBindVertexArray = nullptr;
static PFNGLACTIVETEXTUREPROC glActiveTexture = nullptr;

// Shader cue data structure
struct ShaderCue {
    int shader_scene_id;
    float palette_low[3];
    float palette_mid[3];
    float palette_high[3];
    float speed;
    float intensity;
    float warp;
    float exposure_base;
    float cue_start;
    float cue_end;
};

// Music cue data structure
struct MusicCue {
    char asset_key[64];
    char asset_path[512];
    float cue_start;
    float cue_end;
};

// Image cue data structure
struct ImageCue {
    char asset_key[64];
    char asset_path[512];
    float x;
    float y;
    float scale;
    float opacity;
    float cue_start;
    float cue_end;
};

// Image texture data
struct ImageTexture {
    GLuint texture_id;
    int width;
    int height;
};

// Text cue data structure
struct TextCue {
    char text[256];
    char font_name[64];
    float x;
    float y;
    int size;
    float color_r;
    float color_g;
    float color_b;
    int effect_type;  // 0=None, 1=Fade In Out, 2=Scroll
    float cue_start;
    float cue_end;
    float effect_start;
    float effect_end;
};

// Text texture data (same as ImageTexture)
typedef ImageTexture TextTexture;

// Parse cues.txt and extract first shader cue
bool LoadShaderCue(const char* path, ShaderCue* cue) {
    FILE* f = nullptr;
    fopen_s(&f, path, "r");
    if (!f) return false;
    
    char line[512];
    bool in_shader_cues = false;
    bool found = false;
    
    while (fgets(line, sizeof(line), f)) {
        // Trim whitespace
        char* start = line;
        while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') start++;
        
        if (strstr(start, "[shader_cues]")) {
            in_shader_cues = true;
            continue;
        }
        
        if (start[0] == '[' && in_shader_cues) {
            break;  // End of shader_cues section
        }
        
        if (in_shader_cues && start[0] != '#' && start[0] != '\0' && start[0] != '\n') {
            // Parse shader cue line
            if (sscanf_s(start, "%d|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f|%f",
                &cue->shader_scene_id,
                &cue->palette_low[0], &cue->palette_low[1], &cue->palette_low[2],
                &cue->palette_mid[0], &cue->palette_mid[1], &cue->palette_mid[2],
                &cue->palette_high[0], &cue->palette_high[1], &cue->palette_high[2],
                &cue->speed, &cue->intensity, &cue->warp, &cue->exposure_base) >= 14) {
                found = true;
                break;
            }
        }
    }
    
    fclose(f);
    return found;
}

// Parse cues.txt and extract first music cue
bool LoadMusicCue(const char* path, MusicCue* cue) {
    FILE* f = nullptr;
    fopen_s(&f, path, "r");
    if (!f) return false;
    
    char line[1024];
    bool in_music_cues = false;
    bool found = false;
    
    while (fgets(line, sizeof(line), f)) {
        // Trim whitespace
        char* start = line;
        while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') start++;
        
        if (strstr(start, "[music_cues]")) {
            in_music_cues = true;
            continue;
        }
        
        if (start[0] == '[' && in_music_cues) {
            break;  // End of music_cues section
        }
        
        if (in_music_cues && start[0] != '#' && start[0] != '\0' && start[0] != '\n') {
            // Parse music cue line: asset_key|asset_path|cue_start|cue_end
            char asset_key[64] = {};
            char asset_path[512] = {};
            float cue_start = 0.0f;
            float cue_end = 0.0f;
            
            // Use strchr to split by pipes
            char* token1 = strchr(start, '|');
            if (!token1) continue;
            
            // Extract asset_key
            size_t key_len = token1 - start;
            if (key_len >= sizeof(asset_key)) key_len = sizeof(asset_key) - 1;
            strncpy_s(cue->asset_key, start, key_len);
            cue->asset_key[key_len] = '\0';
            
            // Move past first pipe
            start = token1 + 1;
            token1 = strchr(start, '|');
            if (!token1) continue;
            
            // Extract asset_path
            size_t path_len = token1 - start;
            if (path_len >= sizeof(asset_path)) path_len = sizeof(asset_path) - 1;
            strncpy_s(cue->asset_path, start, path_len);
            cue->asset_path[path_len] = '\0';
            
            // Parse floats
            start = token1 + 1;
            if (sscanf_s(start, "%f|%f", &cue->cue_start, &cue->cue_end) == 2) {
                found = true;
                break;
            }
        }
    }
    
    fclose(f);
    return found;
}

// Parse cues.txt and extract first image cue
bool LoadImageCue(const char* path, ImageCue* cue) {
    FILE* f = nullptr;
    fopen_s(&f, path, "r");
    if (!f) {
        if (g_logfile) {
            fprintf(g_logfile, "[LoadImageCue] FAILED to open file: %s\n", path);
            fflush(g_logfile);
        }
        return false;
    }
    
    if (g_logfile) fprintf(g_logfile, "[LoadImageCue] File opened: %s\n", path);
    
    char line[1024];
    bool in_image_cues = false;
    bool found = false;
    
    while (fgets(line, sizeof(line), f)) {
        char* start = line;
        while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') start++;
        
        if (strstr(start, "[image_cues]")) {
            in_image_cues = true;
            if (g_logfile) fprintf(g_logfile, "[LoadImageCue] Found [image_cues] section\n");
            continue;
        }
        
        if (start[0] == '[' && in_image_cues) {
            if (g_logfile) fprintf(g_logfile, "[LoadImageCue] Left [image_cues] section\n");
            break;
        }
        
        if (in_image_cues && start[0] != '#' && start[0] != '\0' && start[0] != '\n') {
            if (g_logfile) fprintf(g_logfile, "[LoadImageCue] Parsing line: %s", line);
            // Parse: asset_key|asset_path|x|y|scale|opacity|cue_start|cue_end|layer_order
            char* pipe1 = strchr(start, '|');
            if (!pipe1) continue;
            *pipe1 = '\0';
            strncpy_s(cue->asset_key, start, _TRUNCATE);
            
            char* pipe2 = strchr(pipe1 + 1, '|');
            if (!pipe2) continue;
            *pipe2 = '\0';
            strncpy_s(cue->asset_path, pipe1 + 1, _TRUNCATE);
            
            int layer_order = 0;
            if (sscanf_s(pipe2 + 1, "%f|%f|%f|%f|%f|%f|%d",
                &cue->x, &cue->y, &cue->scale, &cue->opacity,
                &cue->cue_start, &cue->cue_end, &layer_order) >= 6) {
                found = true;
                if (g_logfile) fprintf(g_logfile, "[LoadImageCue] Successfully parsed image cue\n");
                break;
            }
        }
    }
    
    if (g_logfile) {
        fprintf(g_logfile, "[LoadImageCue] Result: %s\n", found ? "SUCCESS" : "FAILED");
        fflush(g_logfile);
    }
    fclose(f);
    return found;
}

// Load image file using GDI+ and create OpenGL texture
bool LoadImageTexture(const char* path, ImageTexture* tex) {
    // Convert path to wide string
    wchar_t wpath[512];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 512);
    
    // Load image with GDI+
    Gdiplus::Bitmap* bitmap = new Gdiplus::Bitmap(wpath);
    if (bitmap->GetLastStatus() != Gdiplus::Ok) {
        delete bitmap;
        return false;
    }
    
    tex->width = bitmap->GetWidth();
    tex->height = bitmap->GetHeight();
    
    // Lock bitmap data
    Gdiplus::Rect rect(0, 0, tex->width, tex->height);
    Gdiplus::BitmapData bitmapData;
    if (bitmap->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bitmapData) != Gdiplus::Ok) {
        delete bitmap;
        return false;
    }
    
    // Create OpenGL texture
    glGenTextures(1, &tex->texture_id);
    glBindTexture(GL_TEXTURE_2D, tex->texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    
    // Upload BGRA data (GDI+ format) - need to swap to RGBA
    unsigned char* pixels = new unsigned char[tex->width * tex->height * 4];
    unsigned char* src = (unsigned char*)bitmapData.Scan0;
    for (int i = 0; i < tex->width * tex->height; i++) {
        pixels[i*4 + 0] = src[i*4 + 2]; // R
        pixels[i*4 + 1] = src[i*4 + 1]; // G
        pixels[i*4 + 2] = src[i*4 + 0]; // B
        pixels[i*4 + 3] = src[i*4 + 3]; // A
    }
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex->width, tex->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    
    delete[] pixels;
    bitmap->UnlockBits(&bitmapData);
    delete bitmap;
    
    return true;
}

// Parse cues.txt and extract first text cue
bool LoadTextCue(const char* path, TextCue* cue) {
    FILE* f = nullptr;
    fopen_s(&f, path, "r");
    if (!f) return false;
    
    char line[2048];
    bool in_text_cues = false;
    bool found = false;
    
    while (fgets(line, sizeof(line), f)) {
        char* start = line;
        while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') start++;
        
        if (strstr(start, "[text_cues]")) {
            in_text_cues = true;
            continue;
        }
        
        if (start[0] == '[' && in_text_cues) {
            break;
        }
        
        if (in_text_cues && start[0] != '#' && start[0] != '\0' && start[0] != '\n') {
            // Parse: text|font_name|x|y|size|color_r|color_g|color_b|effect_type|cue_start|cue_end|effect_start|effect_end
            char* pipe1 = strchr(start, '|');
            if (!pipe1) continue;
            *pipe1 = '\0';
            strncpy_s(cue->text, start, _TRUNCATE);
            
            char* pipe2 = strchr(pipe1 + 1, '|');
            if (!pipe2) continue;
            *pipe2 = '\0';
            strncpy_s(cue->font_name, pipe1 + 1, _TRUNCATE);
            
            if (sscanf_s(pipe2 + 1, "%f|%f|%d|%f|%f|%f|%d|%f|%f|%f|%f",
                &cue->x, &cue->y, &cue->size,
                &cue->color_r, &cue->color_g, &cue->color_b,
                &cue->effect_type,
                &cue->cue_start, &cue->cue_end,
                &cue->effect_start, &cue->effect_end) == 11) {
                found = true;
                break;
            }
        }
    }
    
    fclose(f);
    return found;
}

// Render text to texture using GDI+
bool RenderTextToTexture(const char* text, const char* font_name, int font_size, float r, float g, float b, TextTexture* tex) {
    // Convert text to wide string
    wchar_t wtext[256];
    MultiByteToWideChar(CP_UTF8, 0, text, -1, wtext, 256);
    
    wchar_t wfont[64];
    MultiByteToWideChar(CP_UTF8, 0, font_name, -1, wfont, 64);
    
    // Create temporary bitmap to measure text
    Gdiplus::Bitmap temp_bitmap(1, 1, PixelFormat32bppARGB);
    Gdiplus::Graphics temp_graphics(&temp_bitmap);
    
    // Create font
    Gdiplus::Font font(wfont, (Gdiplus::REAL)font_size, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    
    // Measure text size
    Gdiplus::RectF layoutRect(0.0f, 0.0f, 2048.0f, 2048.0f);
    Gdiplus::RectF boundingBox;
    temp_graphics.MeasureString(wtext, -1, &font, layoutRect, &boundingBox);
    
    int width = (int)(boundingBox.Width) + 8;
    int height = (int)(boundingBox.Height) + 8;
    
    if (width <= 0 || height <= 0) return false;
    
    // Create bitmap for text rendering
    Gdiplus::Bitmap* bitmap = new Gdiplus::Bitmap(width, height, PixelFormat32bppARGB);
    Gdiplus::Graphics* graphics = new Gdiplus::Graphics(bitmap);
    
    // Clear with transparent
    graphics->Clear(Gdiplus::Color(0, 0, 0, 0));
    
    // Enable antialiasing
    graphics->SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
    
    // Create brush with color
    Gdiplus::SolidBrush brush(Gdiplus::Color(255, (BYTE)(r * 255), (BYTE)(g * 255), (BYTE)(b * 255)));
    
    // Draw text
    Gdiplus::PointF origin(4.0f, 4.0f);
    graphics->DrawString(wtext, -1, &font, origin, &brush);
    
    delete graphics;
    
    // Lock bitmap and create OpenGL texture
    tex->width = width;
    tex->height = height;
    
    Gdiplus::Rect rect(0, 0, width, height);
    Gdiplus::BitmapData bitmapData;
    if (bitmap->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bitmapData) != Gdiplus::Ok) {
        delete bitmap;
        return false;
    }
    
    glGenTextures(1, &tex->texture_id);
    glBindTexture(GL_TEXTURE_2D, tex->texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    
    // Upload BGRA → RGBA
    unsigned char* pixels = new unsigned char[width * height * 4];
    unsigned char* src = (unsigned char*)bitmapData.Scan0;
    for (int i = 0; i < width * height; i++) {
        pixels[i*4 + 0] = src[i*4 + 2]; // R
        pixels[i*4 + 1] = src[i*4 + 1]; // G
        pixels[i*4 + 2] = src[i*4 + 0]; // B
        pixels[i*4 + 3] = src[i*4 + 3]; // A
    }
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    
    delete[] pixels;
    bitmap->UnlockBits(&bitmapData);
    delete bitmap;
    
    return true;
}

// Minimal vertex shader - fullscreen quad
const char* vertex_shader = R"(
#version 330 core
out vec2 uv;
void main() {
    float x = -1.0 + float((gl_VertexID & 1) << 2);
    float y = -1.0 + float((gl_VertexID & 2) << 1);
    uv = vec2((x + 1.0) * 0.5, (y + 1.0) * 0.5);
    gl_Position = vec4(x, y, 0.0, 1.0);
}
)";
// Sprite vertex shader - textured quad with position/scale
const char* sprite_vertex_shader = R"(
#version 330 core
out vec2 uv;
uniform vec2 u_position;  // -1 to 1
uniform vec2 u_size;      // width, height in normalized coords
void main() {
    float x = -1.0 + float((gl_VertexID & 1) << 2);
    float y = -1.0 + float((gl_VertexID & 2) << 1);
    uv = vec2((x + 1.0) * 0.5, 1.0 - (y + 1.0) * 0.5);  // Flip V coordinate
    gl_Position = vec4(u_position.x + x * u_size.x, u_position.y + y * u_size.y, 0.0, 1.0);
}
)";

// Sprite fragment shader - textured with opacity
const char* sprite_fragment_shader = R"(
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
// Fragment shaders - 10 presets
const char* fragment_shaders[] = {
    // 0: Plasma Vibrant
    R"(
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
)",
    
    // 1: Tunnel Neon
    R"(
#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform float u_time;
uniform vec2 u_resolution;
uniform vec3 u_palette_low;
uniform vec3 u_palette_mid;
uniform vec3 u_palette_high;
uniform float u_speed;

void main() {
    vec2 p = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0);
    float t = u_time * u_speed;
    
    float r = length(p);
    float a = atan(p.y, p.x);
    float d = 1.0 / (r + 0.1);
    
    float tunnel = fract(d - t * 0.5);
    float rings = abs(sin(tunnel * 20.0));
    
    vec3 col = mix(u_palette_low, u_palette_high, rings);
    col = mix(col, u_palette_mid, smoothstep(0.3, 0.7, tunnel));
    
    fragColor = vec4(col, 1.0);
}
)",
    
    // 2: Raymarcher SDF
    R"(
#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform float u_time;
uniform vec2 u_resolution;
uniform vec3 u_palette_low;
uniform vec3 u_palette_high;
uniform float u_speed;

float sdSphere(vec3 p, float r) { return length(p) - r; }

void main() {
    vec2 p = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0);
    float t = u_time * u_speed;
    
    vec3 ro = vec3(0, 0, -3);
    vec3 rd = normalize(vec3(p, 1.0));
    
    float dist = 0.0;
    for (int i = 0; i < 32; i++) {
        vec3 pos = ro + rd * dist;
        float d = sdSphere(pos - vec3(sin(t) * 0.5, cos(t * 0.7) * 0.5, 0), 0.5);
        if (d < 0.001) break;
        dist += d;
    }
    
    vec3 col = mix(u_palette_low, u_palette_high, smoothstep(2.0, 5.0, dist));
    fragColor = vec4(col, 1.0);
}
)",
    
    // 3: Fractal Mandelbrot
    R"(
#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform float u_time;
uniform vec2 u_resolution;
uniform vec3 u_palette_low;
uniform vec3 u_palette_mid;
uniform vec3 u_palette_high;
uniform float u_speed;

void main() {
    vec2 p = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0) * 2.0;
    float t = u_time * u_speed * 0.2;
    p += vec2(sin(t) * 0.3, cos(t * 0.7) * 0.3);
    
    vec2 c = p;
    vec2 z = vec2(0.0);
    float iter = 0.0;
    
    for (int i = 0; i < 64; i++) {
        z = vec2(z.x * z.x - z.y * z.y, 2.0 * z.x * z.y) + c;
        if (length(z) > 4.0) break;
        iter += 1.0;
    }
    
    float v = iter / 64.0;
    vec3 col = mix(mix(u_palette_low, u_palette_mid, v), u_palette_high, smoothstep(0.7, 1.0, v));
    
    fragColor = vec4(col, 1.0);
}
)",
    
    // 4: Voronoi Cells
    R"(
#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform float u_time;
uniform vec2 u_resolution;
uniform vec3 u_palette_low;
uniform vec3 u_palette_mid;
uniform vec3 u_palette_high;
uniform float u_speed;

vec2 hash2(vec2 p) {
    p = vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)));
    return fract(sin(p) * 43758.5453);
}

void main() {
    vec2 p = uv * 8.0 * vec2(u_resolution.x / u_resolution.y, 1.0);
    float t = u_time * u_speed;
    
    vec2 ip = floor(p);
    vec2 fp = fract(p);
    
    float minDist = 1.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 offset = vec2(x, y);
            vec2 h = hash2(ip + offset);
            vec2 pt = offset + sin(h * 6.28 + t) * 0.5 + 0.5;
            float d = length(pt - fp);
            minDist = min(minDist, d);
        }
    }
    
    vec3 col = mix(mix(u_palette_low, u_palette_mid, minDist), u_palette_high, smoothstep(0.5, 1.0, minDist));
    
    fragColor = vec4(col, 1.0);
}
)",
    
    // 5: Wave Distortion
    R"(
#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform float u_time;
uniform vec2 u_resolution;
uniform vec3 u_palette_low;
uniform vec3 u_palette_high;
uniform float u_speed;
uniform float u_warp;

void main() {
    vec2 p = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0);
    float t = u_time * u_speed;
    
    p.x += sin(p.y * 5.0 + t) * u_warp;
    p.y += cos(p.x * 5.0 + t * 0.7) * u_warp;
    
    float d = length(p);
    float wave = sin(d * 10.0 - t * 2.0) * 0.5 + 0.5;
    
    vec3 col = mix(u_palette_low, u_palette_high, wave);
    
    fragColor = vec4(col, 1.0);
}
)",
    
    // 6: Particle System
    R"(
#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform float u_time;
uniform vec2 u_resolution;
uniform vec3 u_palette_low;
uniform vec3 u_palette_mid;
uniform vec3 u_palette_high;
uniform float u_speed;

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void main() {
    vec2 p = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0);
    float t = u_time * u_speed;
    
    vec3 col = vec3(0.0);
    
    for (int i = 0; i < 32; i++) {
        float fi = float(i);
        float h = hash(vec2(fi, fi * 0.5));
        float angle = h * 6.28;
        float radius = fract(h * 7.13 + t * 0.3) * 2.0;
        
        vec2 pos = vec2(cos(angle), sin(angle)) * radius;
        float d = length(p - pos);
        float particle = smoothstep(0.05, 0.0, d);
        
        vec3 pcol = mix(u_palette_low, mix(u_palette_mid, u_palette_high, h), fract(radius));
        col += pcol * particle;
    }
    
    fragColor = vec4(col, 1.0);
}
)",
    
    // 7: Starfield
    R"(
#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform float u_time;
uniform vec2 u_resolution;
uniform vec3 u_palette_high;
uniform float u_speed;

float hash(vec3 p) {
    return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453);
}

void main() {
    vec2 p = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0);
    float t = u_time * u_speed;
    
    vec3 rd = normalize(vec3(p, 1.0));
    vec3 col = vec3(0.0);
    
    for (int i = 0; i < 64; i++) {
        float fi = float(i);
        vec3 star_pos = vec3(hash(vec3(fi, fi * 0.1, 0)) * 2.0 - 1.0,
                             hash(vec3(fi * 0.5, fi, 0)) * 2.0 - 1.0,
                             hash(vec3(fi, 0, fi * 0.7)) * 5.0 + 1.0);
        
        star_pos.z = fract(star_pos.z - t * 0.5) * 10.0;
        vec3 proj = star_pos / star_pos.z;
        
        float d = length(proj.xy - p);
        float star = smoothstep(0.02, 0.0, d) / star_pos.z;
        col += u_palette_high * star;
    }
    
    fragColor = vec4(col, 1.0);
}
)",
    
    // 8: Glow Orbs
    R"(
#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform float u_time;
uniform vec2 u_resolution;
uniform vec3 u_palette_low;
uniform vec3 u_palette_mid;
uniform vec3 u_palette_high;
uniform float u_speed;

void main() {
    vec2 p = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0);
    float t = u_time * u_speed;
    
    vec3 col = vec3(0.0);
    
    for (int i = 0; i < 5; i++) {
        float fi = float(i);
        vec2 orb_pos = vec2(sin(t * 0.5 + fi * 1.2) * 0.6, cos(t * 0.7 + fi * 0.8) * 0.6);
        float d = length(p - orb_pos);
        float glow = 0.02 / d;
        
        vec3 orb_col = mix(mix(u_palette_low, u_palette_mid, fi / 5.0), u_palette_high, smoothstep(0.3, 0.8, fi / 5.0));
        col += orb_col * glow;
    }
    
    fragColor = vec4(col, 1.0);
}
)",
    
    // 9: Matrix Rain
    R"(
#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform float u_time;
uniform vec2 u_resolution;
uniform vec3 u_palette_high;
uniform float u_speed;

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void main() {
    vec2 p = uv * vec2(40.0, 30.0);
    vec2 ip = floor(p);
    vec2 fp = fract(p);
    float t = u_time * u_speed;
    
    float h = hash(ip);
    float drop = fract(h * 7.13 - t * 0.5);
    
    float char_y = fract((ip.y + drop * 30.0) / 30.0);
    float char_brightness = smoothstep(0.0, 0.05, drop) * smoothstep(1.0, 0.8, drop);
    
    float char = step(0.3, hash(ip + floor(t * 10.0)));
    float glyph = char * char_brightness;
    
    vec3 col = u_palette_high * glyph;
    
    fragColor = vec4(col, 1.0);
}
)"
};

int main() {
    // Allocate console for debug output
    AllocConsole();
    FILE* fp;
    freopen_s(&fp, "CONOUT$", "w", stdout);
    freopen_s(&fp, "CONOUT$", "w", stderr);
    
    // Also log to file for debugging
    fopen_s(&g_logfile, "intro_debug.log", "w");
    
    printf("=== HiMYM Minimal Intro ===\n\n");
    if (g_logfile) fprintf(g_logfile, "=== HiMYM Minimal Intro Debug Log ===\n\n");
    
    // Load shader cue from assets/cues.txt
    ShaderCue cue = {};
    if (!LoadShaderCue("assets/cues.txt", &cue)) {
        // Default fallback
        cue.shader_scene_id = 0;
        cue.palette_low[0] = 0.1f; cue.palette_low[1] = 0.3f; cue.palette_low[2] = 0.8f;
        cue.palette_mid[0] = 0.45f; cue.palette_mid[1] = 0.25f; cue.palette_mid[2] = 0.7f;
        cue.palette_high[0] = 0.8f; cue.palette_high[1] = 0.2f; cue.palette_high[2] = 0.6f;
        cue.speed = 1.0f;
        cue.intensity = 1.0f;
        cue.warp = 0.5f;
        cue.exposure_base = 0.76f;
    }
    
    // Load music cue
    MusicCue music_cue = {};
    bool has_music = LoadMusicCue("assets/cues.txt", &music_cue);
    
    // Load image cue
    ImageCue image_cue = {};
    bool has_image = LoadImageCue("../../../assets/cues.txt", &image_cue);
    ImageTexture image_tex = {};
    bool image_loaded = false;
    
    printf("Image cue loaded: %s\n", has_image ? "YES" : "NO");
    if (g_logfile) {
        fprintf(g_logfile, "Image cue loaded: %s\n", has_image ? "YES" : "NO");
        fprintf(g_logfile, "Tried to load from: ../../../assets/cues.txt\n");
        fflush(g_logfile);
    }
    if (has_image) {
        printf("  Key: %s, Path: %s\n", image_cue.asset_key, image_cue.asset_path);
        if (g_logfile) fprintf(g_logfile, "  Key: %s, Path: %s\n", image_cue.asset_key, image_cue.asset_path);
        printf("  Pos: (%.2f,%.2f), Scale: %.2f, Time: %.1f-%.1fs\n",
               image_cue.x, image_cue.y, image_cue.scale, 
               image_cue.cue_start, image_cue.cue_end);
        if (g_logfile) fprintf(g_logfile, "  Pos: (%.2f,%.2f), Scale: %.2f, Time: %.1f-%.1fs\n",
               image_cue.x, image_cue.y, image_cue.scale, 
               image_cue.cue_start, image_cue.cue_end);
    }
    
    // Load text cue
    TextCue text_cue = {};
    bool has_text = LoadTextCue("../../../assets/cues.txt", &text_cue);
    TextTexture text_tex = {};
    bool text_loaded = false;
    
    printf("Text cue loaded: %s\n", has_text ? "YES" : "NO");
    if (has_text) {
        printf("  Text: \"%s\", Font: %s, Size: %d\n", text_cue.text, text_cue.font_name, text_cue.size);
        printf("  Time: %.1f-%.1fs\n", text_cue.cue_start, text_cue.cue_end);
    }
    
    // Clamp shader_scene_id to valid range
    if (cue.shader_scene_id < 0 || cue.shader_scene_id > 9) {
        cue.shader_scene_id = 0;
    }
    
    // Create window and OpenGL context
    rev::platform::WindowConfig config;
    config.width = 1920;
    config.height = 1080;
    config.fullscreen = false;  // Windowed for testing
    config.title = "HiMYM - Minimal Intro Test";
    
    rev::platform::Window* window = rev::platform::CreateIntroWindow(config);
    if (!window) {
        return -1;
    }
    
    // Load OpenGL functions
    rev::platform::LoadGLFunctions();
    
    // Initialize GDI+
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);
    
    // Load GL 3.3+ function pointers
    glGenVertexArrays = (PFNGLGENVERTEXARRAYSPROC)rev::platform::GetProcAddress("glGenVertexArrays");
    glBindVertexArray = (PFNGLBINDVERTEXARRAYPROC)rev::platform::GetProcAddress("glBindVertexArray");
    glActiveTexture = (PFNGLACTIVETEXTUREPROC)rev::platform::GetProcAddress("glActiveTexture");
    
    // Create and bind VAO (required for OpenGL 3.3 core)
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    
    // Compile shader with selected fragment shader
    const char* selected_frag_shader = fragment_shaders[cue.shader_scene_id];
    rev::shader::Program* shader = rev::shader::CompileFromSource(vertex_shader, selected_frag_shader);
    if (!shader) {
        rev::platform::DestroyIntroWindow(window);
        return -1;
    }
    
    // Compile sprite shader
    rev::shader::Program* sprite_shader = rev::shader::CompileFromSource(sprite_vertex_shader, sprite_fragment_shader);
    if (!sprite_shader) {
        printf("ERROR: Sprite shader failed to compile\n");
    } else {
        printf("Sprite shader OK\n");
    }
    
    // Get uniform locations
    int u_time_loc = rev::shader::GetUniformLocation(shader, "u_time");
    int u_resolution_loc = rev::shader::GetUniformLocation(shader, "u_resolution");
    int u_palette_low_loc = rev::shader::GetUniformLocation(shader, "u_palette_low");
    int u_palette_mid_loc = rev::shader::GetUniformLocation(shader, "u_palette_mid");
    int u_palette_high_loc = rev::shader::GetUniformLocation(shader, "u_palette_high");
    int u_speed_loc = rev::shader::GetUniformLocation(shader, "u_speed");
    int u_intensity_loc = rev::shader::GetUniformLocation(shader, "u_intensity");
    int u_warp_loc = rev::shader::GetUniformLocation(shader, "u_warp");
    
    // Load XM music if available
    rev::xm::Player* xm_player = nullptr;
    if (has_music && music_cue.asset_path[0] != '\0') {
        // Convert project-relative path to exe-relative path
        char music_path[512];
        if (strncmp(music_cue.asset_path, "assets/", 7) == 0) {
            snprintf(music_path, sizeof(music_path), "../../../%s", music_cue.asset_path);
        } else {
            strncpy_s(music_path, music_cue.asset_path, _TRUNCATE);
        }
        
        FILE* xm_file = nullptr;
        fopen_s(&xm_file, music_path, "rb");
        if (xm_file) {
            fseek(xm_file, 0, SEEK_END);
            long xm_size = ftell(xm_file);
            fseek(xm_file, 0, SEEK_SET);
            
            unsigned char* xm_data = new unsigned char[xm_size];
            fread(xm_data, 1, xm_size, xm_file);
            fclose(xm_file);
            
            xm_player = rev::xm::CreatePlayer(xm_data, xm_size);
            delete[] xm_data;
            
            if (xm_player) {
                printf("Loaded music: %s (start: %.2f, end: %.2f)\n", 
                       music_cue.asset_key, music_cue.cue_start, music_cue.cue_end);
            }
        }
    }
    
    // Load image texture (fix path to be relative from exe location)
    if (has_image && image_cue.asset_path[0] != '\0') {
        // Convert project-relative path to exe-relative path
        // Path from cues.txt is like "project_assets/logo.png"
        // Exe is at build/bin/Release/, so need ../../../
        char exe_relative_path[512];
        snprintf(exe_relative_path, sizeof(exe_relative_path), "../../../%s", image_cue.asset_path);
        
        // Convert forward slashes to backslashes for Windows GDI+
        for (char* p = exe_relative_path; *p; ++p) {
            if (*p == '/') *p = '\\\\';
        }
        
        printf("\nLoading image texture: %s\n", exe_relative_path);
        if (g_logfile) fprintf(g_logfile, "\nLoading image texture: %s\n", exe_relative_path);
        if (LoadImageTexture(exe_relative_path, &image_tex)) {
            image_loaded = true;
            printf("Image loaded: %dx%d, will show at %.1f-%.1fs\n",
                   image_tex.width, image_tex.height,
                   image_cue.cue_start, image_cue.cue_end);
            if (g_logfile) fprintf(g_logfile, "Image loaded: %dx%d, will show at %.1f-%.1fs\n",
                   image_tex.width, image_tex.height,
                   image_cue.cue_start, image_cue.cue_end);
        } else {
            printf("FAILED to load image\n");
            if (g_logfile) fprintf(g_logfile, "FAILED to load image from: %s\n", exe_relative_path);
        }
    }
    
    // Render text to texture
    if (has_text && text_cue.text[0] != '\0') {
        printf("\nRendering text: \"%s\"\n", text_cue.text);
        if (RenderTextToTexture(text_cue.text, text_cue.font_name, text_cue.size,
                                text_cue.color_r, text_cue.color_g, text_cue.color_b, &text_tex)) {
            text_loaded = true;
            printf("Text rendered: %dx%d, will show at %.1f-%.1fs\n",
                   text_tex.width, text_tex.height,
                   text_cue.cue_start, text_cue.cue_end);
        } else {
            printf("FAILED to render text\n");
        }
    }
    
    // Main loop
    double start_time = rev::platform::GetTime();
    bool music_started = false;
    
    while (rev::platform::PollEvents(window) && !window->should_close) {
        double current_time = rev::platform::GetTime();
        float time = static_cast<float>(current_time - start_time);
        
        // Music acknowledgment (audio playback requires audio device setup)
        if (xm_player && !music_started && time >= music_cue.cue_start) {
            music_started = true;
            printf("[%.2fs] Music cue active\n", time);
        }
        
        // Image activation/deactivation logging
        static bool image_was_active = false;
        bool image_active_now = (image_loaded && time >= image_cue.cue_start && time <= image_cue.cue_end);
        if (image_active_now && !image_was_active) {
            printf("[%.1fs] IMAGE ON (sprite_shader=%p)\n", time, sprite_shader);
            image_was_active = true;
        } else if (!image_active_now && image_was_active) {
            printf("[%.1fs] IMAGE OFF\n", time);
            image_was_active = false;
        }
        
        // Clear screen
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        
        // Use shader and set uniforms
        rev::shader::Use(shader);
        rev::shader::SetFloat(shader, u_time_loc, time);
        rev::shader::SetVec2(shader, u_resolution_loc, 
                            static_cast<float>(config.width), 
                            static_cast<float>(config.height));
        
        // Set palette and parameter uniforms
        if (u_palette_low_loc >= 0)
            rev::shader::SetVec3(shader, u_palette_low_loc, cue.palette_low[0], cue.palette_low[1], cue.palette_low[2]);
        if (u_palette_mid_loc >= 0)
            rev::shader::SetVec3(shader, u_palette_mid_loc, cue.palette_mid[0], cue.palette_mid[1], cue.palette_mid[2]);
        if (u_palette_high_loc >= 0)
            rev::shader::SetVec3(shader, u_palette_high_loc, cue.palette_high[0], cue.palette_high[1], cue.palette_high[2]);
        if (u_speed_loc >= 0)
            rev::shader::SetFloat(shader, u_speed_loc, cue.speed);
        if (u_intensity_loc >= 0)
            rev::shader::SetFloat(shader, u_intensity_loc, cue.intensity);
        if (u_warp_loc >= 0)
            rev::shader::SetFloat(shader, u_warp_loc, cue.warp);
        
        // Draw fullscreen quad (shader background)
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        
        // Draw image sprite if active
        if (sprite_shader && image_loaded && time >= image_cue.cue_start && time <= image_cue.cue_end) {
            // Use sprite shader
            rev::shader::Use(sprite_shader);
            
            // Ensure proper GL state for sprite rendering
            glDisable(GL_DEPTH_TEST);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            
            // Calculate sprite quad position/size (normalized coordinates)
            float aspect = (float)image_tex.width / (float)image_tex.height;
            float w = image_cue.scale * aspect;
            float h = image_cue.scale;
            float x = (image_cue.x * 2.0f - 1.0f);
            float y = (image_cue.y * 2.0f - 1.0f);
            
            // Set sprite uniforms
            int u_position_loc = rev::shader::GetUniformLocation(sprite_shader, "u_position");
            int u_size_loc = rev::shader::GetUniformLocation(sprite_shader, "u_size");
            int u_texture_loc = rev::shader::GetUniformLocation(sprite_shader, "u_texture");
            int u_opacity_loc = rev::shader::GetUniformLocation(sprite_shader, "u_opacity");
            
            if (u_position_loc >= 0)
                rev::shader::SetVec2(sprite_shader, u_position_loc, x, y);
            if (u_size_loc >= 0)
                rev::shader::SetVec2(sprite_shader, u_size_loc, w, h);
            if (u_texture_loc >= 0)
                rev::shader::SetInt(sprite_shader, u_texture_loc, 0);
            if (u_opacity_loc >= 0)
                rev::shader::SetFloat(sprite_shader, u_opacity_loc, image_cue.opacity);
            
            // Bind texture to unit 0
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, image_tex.texture_id);
            
            // Draw sprite quad (4 vertices, triangle strip)
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            
            glDisable(GL_BLEND);
        }
        
        // Draw text sprite if active
        if (sprite_shader && text_loaded && time >= text_cue.cue_start && time <= text_cue.cue_end) {
            // Use sprite shader
            rev::shader::Use(sprite_shader);
            
            // Ensure proper GL state
            glDisable(GL_DEPTH_TEST);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            
            // Calculate text position/size
            // Text uses pixel size, convert to normalized coordinates
            float text_scale = 0.3f;  // Base scale
            float aspect = (float)text_tex.width / (float)text_tex.height;
            float w = text_scale * aspect;
            float h = text_scale;
            float x = (text_cue.x * 2.0f - 1.0f);
            float y = (text_cue.y * 2.0f - 1.0f);
            
            // Calculate opacity based on effect
            float opacity = 1.0f;
            if (text_cue.effect_type == 1) {  // Fade In Out
                float fade_duration = text_cue.effect_end - text_cue.effect_start;
                if (time < text_cue.effect_start) {
                    opacity = 0.0f;
                } else if (time < text_cue.effect_start + fade_duration * 0.5f) {
                    // Fade in
                    opacity = (time - text_cue.effect_start) / (fade_duration * 0.5f);
                } else if (time > text_cue.effect_end - fade_duration * 0.5f) {
                    // Fade out
                    opacity = (text_cue.effect_end - time) / (fade_duration * 0.5f);
                }
            }
            
            // Set uniforms
            int u_position_loc = rev::shader::GetUniformLocation(sprite_shader, "u_position");
            int u_size_loc = rev::shader::GetUniformLocation(sprite_shader, "u_size");
            int u_texture_loc = rev::shader::GetUniformLocation(sprite_shader, "u_texture");
            int u_opacity_loc = rev::shader::GetUniformLocation(sprite_shader, "u_opacity");
            
            if (u_position_loc >= 0)
                rev::shader::SetVec2(sprite_shader, u_position_loc, x, y);
            if (u_size_loc >= 0)
                rev::shader::SetVec2(sprite_shader, u_size_loc, w, h);
            if (u_texture_loc >= 0)
                rev::shader::SetInt(sprite_shader, u_texture_loc, 0);
            if (u_opacity_loc >= 0)
                rev::shader::SetFloat(sprite_shader, u_opacity_loc, opacity);
            
            // Bind text texture
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, text_tex.texture_id);
            
            // Draw text quad
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            
            glDisable(GL_BLEND);
        }
        
        // Swap buffers
        rev::platform::SwapBuffers(window);
        
        // Exit after 10 seconds or on ESC
        if (time > 10.0f || rev::platform::IsKeyPressed(window, VK_ESCAPE)) {
            break;
        }
    }
    
    // Cleanup
    if (xm_player) {
        rev::xm::DestroyPlayer(xm_player);
    }
    if (sprite_shader) {
        rev::shader::DestroyProgram(sprite_shader);
    }
    rev::shader::DestroyProgram(shader);
    rev::platform::DestroyIntroWindow(window);
    
    Gdiplus::GdiplusShutdown(gdiplusToken);
    
    if (g_logfile) {
        fflush(g_logfile);
        fclose(g_logfile);
    }
    
    return 0;
}

