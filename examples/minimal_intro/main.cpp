#include <windows.h>
#include <gl/gl.h>
#include "rev_platform.h"
#include "rev_shader.h"
#include "rev_xm.h"
#include <cstdio>
#include <cstring>

// Define missing GL constants
#ifndef GL_COLOR_BUFFER_BIT
#define GL_COLOR_BUFFER_BIT 0x00004000
#endif
#ifndef GL_TRIANGLE_STRIP
#define GL_TRIANGLE_STRIP 0x0005
#endif

// GL 3.3 function pointers (VAO support required for core profile)
typedef void (APIENTRY *PFNGLGENVERTEXARRAYSPROC)(GLsizei n, GLuint* arrays);
typedef void (APIENTRY *PFNGLBINDVERTEXARRAYPROC)(GLuint array);

static PFNGLGENVERTEXARRAYSPROC glGenVertexArrays = nullptr;
static PFNGLBINDVERTEXARRAYPROC glBindVertexArray = nullptr;

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
    
    // Load GL 3.3 VAO functions (required for core profile)
    glGenVertexArrays = (PFNGLGENVERTEXARRAYSPROC)rev::platform::GetProcAddress("glGenVertexArrays");
    glBindVertexArray = (PFNGLBINDVERTEXARRAYPROC)rev::platform::GetProcAddress("glBindVertexArray");
    
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
    
    // Get uniform locations
    int u_time_loc = rev::shader::GetUniformLocation(shader, "u_time");
    int u_resolution_loc = rev::shader::GetUniformLocation(shader, "u_resolution");
    int u_palette_low_loc = rev::shader::GetUniformLocation(shader, "u_palette_low");
    int u_palette_mid_loc = rev::shader::GetUniformLocation(shader, "u_palette_mid");
    int u_palette_high_loc = rev::shader::GetUniformLocation(shader, "u_palette_high");
    int u_speed_loc = rev::shader::GetUniformLocation(shader, "u_speed");
    int u_intensity_loc = rev::shader::GetUniformLocation(shader, "u_intensity");
    int u_warp_loc = rev::shader::GetUniformLocation(shader, "u_warp");
    
    // Main loop
    double start_time = rev::platform::GetTime();
    
    while (rev::platform::PollEvents(window) && !window->should_close) {
        double current_time = rev::platform::GetTime();
        float time = static_cast<float>(current_time - start_time);
        
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
        
        // Draw fullscreen quad (3 vertices for triangle strip)
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        
        // Swap buffers
        rev::platform::SwapBuffers(window);
        
        // Exit after 10 seconds or on ESC
        if (time > 10.0f || rev::platform::IsKeyPressed(window, VK_ESCAPE)) {
            break;
        }
    }
    
    // Cleanup
    rev::shader::DestroyProgram(shader);
    rev::platform::DestroyIntroWindow(window);
    
    return 0;
}
