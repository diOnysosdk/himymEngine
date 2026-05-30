#include <windows.h>
#include <gl/gl.h>
#include "rev_platform.h"
#include "rev_shader.h"
#include "rev_curve.h"
#include "rev_sequence.h"

// GL constants
#ifndef GL_COLOR_BUFFER_BIT
#define GL_COLOR_BUFFER_BIT 0x00004000
#endif
#ifndef GL_TRIANGLE_STRIP
#define GL_TRIANGLE_STRIP 0x0005
#endif

// GL 3.3 VAO functions
typedef void (APIENTRY *PFNGLGENVERTEXARRAYSPROC)(GLsizei n, GLuint* arrays);
typedef void (APIENTRY *PFNGLBINDVERTEXARRAYPROC)(GLuint array);

static PFNGLGENVERTEXARRAYSPROC glGenVertexArrays = nullptr;
static PFNGLBINDVERTEXARRAYPROC glBindVertexArray = nullptr;

// Vertex shader - fullscreen quad
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

// Scene 1: Plasma waves
const char* scene1_fragment = R"(
#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform float u_time;
uniform vec2 u_resolution;
uniform float u_speed;
uniform float u_complexity;

void main() {
    vec2 p = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0);
    float t = u_time * u_speed;
    
    // Plasma effect
    float v = sin(p.x * 10.0 + t);
    v += sin(p.y * 10.0 + t * 1.2);
    v += sin((p.x + p.y) * 10.0 + t * 0.8);
    v += sin(length(p) * u_complexity + t * 1.5);
    v *= 0.25;
    
    vec3 col = vec3(
        0.5 + 0.5 * sin(v * 3.14159 + 0.0),
        0.5 + 0.5 * sin(v * 3.14159 + 2.094),
        0.5 + 0.5 * sin(v * 3.14159 + 4.188)
    );
    
    fragColor = vec4(col, 1.0);
}
)";

// Scene 2: Tunnel effect
const char* scene2_fragment = R"(
#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform float u_time;
uniform vec2 u_resolution;
uniform float u_speed;
uniform float u_twist;

void main() {
    vec2 p = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0);
    float t = u_time * u_speed;
    
    // Tunnel coordinates
    float r = length(p);
    float a = atan(p.y, p.x) + u_twist * t;
    
    float tunnel_u = 5.0 / r + t;
    float tunnel_v = a / 3.14159;
    
    // Stripes pattern
    float pattern = sin(tunnel_u * 10.0) * sin(tunnel_v * 20.0);
    
    // Color based on depth
    vec3 col = vec3(
        0.5 + 0.5 * sin(tunnel_u + pattern),
        0.5 + 0.5 * sin(tunnel_u * 1.3 + pattern + 2.0),
        0.5 + 0.5 * sin(tunnel_u * 1.7 + pattern + 4.0)
    );
    
    // Vignette
    col *= 1.0 - smoothstep(0.5, 1.5, r);
    
    fragColor = vec4(col, 1.0);
}
)";

// Scene 3: Fractal zoom
const char* scene3_fragment = R"(
#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform float u_time;
uniform vec2 u_resolution;
uniform float u_zoom;
uniform float u_rotation;

void main() {
    vec2 p = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0);
    float t = u_time;
    
    // Rotate
    float a = u_rotation * t;
    float ca = cos(a), sa = sin(a);
    p = mat2(ca, -sa, sa, ca) * p;
    
    // Zoom
    p *= exp(-u_zoom);
    
    // Mandelbrot-like iteration
    vec2 z = p;
    float iter = 0.0;
    for (int i = 0; i < 32; i++) {
        z = vec2(z.x * z.x - z.y * z.y, 2.0 * z.x * z.y) + p * 0.5;
        if (dot(z, z) > 4.0) break;
        iter += 1.0;
    }
    
    float brightness = iter / 32.0;
    vec3 col = vec3(
        0.5 + 0.5 * sin(brightness * 6.28 + t),
        0.5 + 0.5 * sin(brightness * 6.28 + t + 2.0),
        0.5 + 0.5 * sin(brightness * 6.28 + t + 4.0)
    );
    
    fragColor = vec4(col, 1.0);
}
)";

// Scene 4: Radial burst
const char* scene4_fragment = R"(
#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform float u_time;
uniform vec2 u_resolution;
uniform float u_rays;
uniform float u_pulse;

void main() {
    vec2 p = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0);
    float t = u_time;
    
    float r = length(p);
    float a = atan(p.y, p.x);
    
    // Radial rays
    float rays = sin(a * u_rays + t * 2.0) * 0.5 + 0.5;
    
    // Pulse from center
    float pulse = sin(r * 10.0 - t * u_pulse) * 0.5 + 0.5;
    
    float brightness = rays * pulse;
    
    vec3 col = vec3(
        brightness * (0.5 + 0.5 * sin(t + 0.0)),
        brightness * (0.5 + 0.5 * sin(t + 2.0)),
        brightness * (0.5 + 0.5 * sin(t + 4.0))
    );
    
    // Glow in center
    col += vec3(0.3, 0.2, 0.5) / (r * r * 10.0 + 1.0);
    
    fragColor = vec4(col, 1.0);
}
)";

int main() {
    // Create window
    rev::platform::WindowConfig config;
    config.width = 1920;
    config.height = 1080;
    config.fullscreen = true;
    config.title = "HiMYM Demo - 60 Second Intro";
    
    rev::platform::Window* window = rev::platform::CreateIntroWindow(config);
    if (!window) return -1;
    
    // Load GL functions
    rev::platform::LoadGLFunctions();
    glGenVertexArrays = (PFNGLGENVERTEXARRAYSPROC)rev::platform::GetProcAddress("glGenVertexArrays");
    glBindVertexArray = (PFNGLBINDVERTEXARRAYPROC)rev::platform::GetProcAddress("glBindVertexArray");
    
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    
    // Compile all scene shaders
    rev::shader::Program* scene1 = rev::shader::CompileFromSource(vertex_shader, scene1_fragment);
    rev::shader::Program* scene2 = rev::shader::CompileFromSource(vertex_shader, scene2_fragment);
    rev::shader::Program* scene3 = rev::shader::CompileFromSource(vertex_shader, scene3_fragment);
    rev::shader::Program* scene4 = rev::shader::CompileFromSource(vertex_shader, scene4_fragment);
    
    if (!scene1 || !scene2 || !scene3 || !scene4) {
        rev::platform::DestroyIntroWindow(window);
        return -1;
    }
    
    // Create timeline with 4 scenes
    rev::sequence::Timeline timeline = rev::sequence::CreateTimeline(4);
    rev::sequence::AddCue(timeline, 0.0f, 15.0f, 2.0f, 2.0f, 1);    // Scene 1: Plasma
    rev::sequence::AddCue(timeline, 13.0f, 30.0f, 2.0f, 2.0f, 2);   // Scene 2: Tunnel
    rev::sequence::AddCue(timeline, 28.0f, 45.0f, 2.0f, 2.0f, 3);   // Scene 3: Fractal
    rev::sequence::AddCue(timeline, 43.0f, 60.0f, 2.0f, 1.0f, 4);   // Scene 4: Burst
    rev::sequence::SortCues(timeline);
    
    // Animation curves for each scene
    // Scene 1: Speed curve
    rev::curve::Curve scene1_speed = rev::curve::CreateCurve(4);
    rev::curve::AddPoint(scene1_speed, 0.0f, 0.5f, rev::curve::EaseMode::EaseIn);
    rev::curve::AddPoint(scene1_speed, 5.0f, 1.5f, rev::curve::EaseMode::Smoothstep);
    rev::curve::AddPoint(scene1_speed, 10.0f, 2.0f, rev::curve::EaseMode::EaseOut);
    rev::curve::AddPoint(scene1_speed, 15.0f, 0.8f, rev::curve::EaseMode::Linear);
    rev::curve::SortPoints(scene1_speed);
    
    // Scene 1: Complexity curve
    rev::curve::Curve scene1_complexity = rev::curve::CreateCurve(3);
    rev::curve::AddPoint(scene1_complexity, 0.0f, 5.0f, rev::curve::EaseMode::Linear);
    rev::curve::AddPoint(scene1_complexity, 7.0f, 15.0f, rev::curve::EaseMode::Smoothstep);
    rev::curve::AddPoint(scene1_complexity, 15.0f, 8.0f, rev::curve::EaseMode::EaseOut);
    rev::curve::SortPoints(scene1_complexity);
    
    // Scene 2: Speed and twist
    rev::curve::Curve scene2_speed = rev::curve::CreateCurve(3);
    rev::curve::AddPoint(scene2_speed, 13.0f, 1.0f, rev::curve::EaseMode::EaseIn);
    rev::curve::AddPoint(scene2_speed, 22.0f, 2.5f, rev::curve::EaseMode::EaseOut);
    rev::curve::AddPoint(scene2_speed, 30.0f, 1.5f, rev::curve::EaseMode::Linear);
    rev::curve::SortPoints(scene2_speed);
    
    rev::curve::Curve scene2_twist = rev::curve::CreateCurve(2);
    rev::curve::AddPoint(scene2_twist, 13.0f, 0.5f, rev::curve::EaseMode::Smoothstep);
    rev::curve::AddPoint(scene2_twist, 30.0f, 2.0f, rev::curve::EaseMode::Linear);
    rev::curve::SortPoints(scene2_twist);
    
    // Scene 3: Zoom and rotation
    rev::curve::Curve scene3_zoom = rev::curve::CreateCurve(4);
    rev::curve::AddPoint(scene3_zoom, 28.0f, 0.0f, rev::curve::EaseMode::EaseIn);
    rev::curve::AddPoint(scene3_zoom, 33.0f, 3.0f, rev::curve::EaseMode::Linear);
    rev::curve::AddPoint(scene3_zoom, 40.0f, 5.0f, rev::curve::EaseMode::EaseOut);
    rev::curve::AddPoint(scene3_zoom, 45.0f, 6.0f, rev::curve::EaseMode::Linear);
    rev::curve::SortPoints(scene3_zoom);
    
    rev::curve::Curve scene3_rotation = rev::curve::CreateCurve(2);
    rev::curve::AddPoint(scene3_rotation, 28.0f, 0.3f, rev::curve::EaseMode::Linear);
    rev::curve::AddPoint(scene3_rotation, 45.0f, 1.0f, rev::curve::EaseMode::Smoothstep);
    rev::curve::SortPoints(scene3_rotation);
    
    // Scene 4: Rays and pulse
    rev::curve::Curve scene4_rays = rev::curve::CreateCurve(3);
    rev::curve::AddPoint(scene4_rays, 43.0f, 8.0f, rev::curve::EaseMode::EaseIn);
    rev::curve::AddPoint(scene4_rays, 50.0f, 16.0f, rev::curve::EaseMode::Smoothstep);
    rev::curve::AddPoint(scene4_rays, 60.0f, 24.0f, rev::curve::EaseMode::Linear);
    rev::curve::SortPoints(scene4_rays);
    
    rev::curve::Curve scene4_pulse = rev::curve::CreateCurve(3);
    rev::curve::AddPoint(scene4_pulse, 43.0f, 3.0f, rev::curve::EaseMode::Linear);
    rev::curve::AddPoint(scene4_pulse, 52.0f, 8.0f, rev::curve::EaseMode::EaseInOut);
    rev::curve::AddPoint(scene4_pulse, 60.0f, 12.0f, rev::curve::EaseMode::Linear);
    rev::curve::SortPoints(scene4_pulse);
    
    // Main loop
    double start_time = rev::platform::GetTime();
    
    while (rev::platform::PollEvents(window) && !window->should_close) {
        double current_time = rev::platform::GetTime();
        float time = static_cast<float>(current_time - start_time);
        
        // Update timeline
        rev::sequence::SetTime(timeline, time);
        
        // Get active scenes
        rev::sequence::Cue* active_cues[4];
        int active_count = rev::sequence::GetActiveCues(timeline, active_cues, 4);
        
        // Clear screen
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        
        // Render each active scene
        for (int i = 0; i < active_count; ++i) {
            const rev::sequence::Cue* cue = active_cues[i];
            float opacity = cue->opacity;
            
            rev::shader::Program* shader = nullptr;
            
            switch (cue->id) {
                case 1: { // Plasma scene
                    shader = scene1;
                    rev::shader::Use(shader);
                    int u_time = rev::shader::GetUniformLocation(shader, "u_time");
                    int u_resolution = rev::shader::GetUniformLocation(shader, "u_resolution");
                    int u_speed = rev::shader::GetUniformLocation(shader, "u_speed");
                    int u_complexity = rev::shader::GetUniformLocation(shader, "u_complexity");
                    
                    rev::shader::SetFloat(shader, u_time, time);
                    rev::shader::SetVec2(shader, u_resolution, 1920.0f, 1080.0f);
                    rev::shader::SetFloat(shader, u_speed, rev::curve::Evaluate(scene1_speed, time));
                    rev::shader::SetFloat(shader, u_complexity, rev::curve::Evaluate(scene1_complexity, time));
                    break;
                }
                case 2: { // Tunnel scene
                    shader = scene2;
                    rev::shader::Use(shader);
                    int u_time = rev::shader::GetUniformLocation(shader, "u_time");
                    int u_resolution = rev::shader::GetUniformLocation(shader, "u_resolution");
                    int u_speed = rev::shader::GetUniformLocation(shader, "u_speed");
                    int u_twist = rev::shader::GetUniformLocation(shader, "u_twist");
                    
                    rev::shader::SetFloat(shader, u_time, time);
                    rev::shader::SetVec2(shader, u_resolution, 1920.0f, 1080.0f);
                    rev::shader::SetFloat(shader, u_speed, rev::curve::Evaluate(scene2_speed, time));
                    rev::shader::SetFloat(shader, u_twist, rev::curve::Evaluate(scene2_twist, time));
                    break;
                }
                case 3: { // Fractal scene
                    shader = scene3;
                    rev::shader::Use(shader);
                    int u_time = rev::shader::GetUniformLocation(shader, "u_time");
                    int u_resolution = rev::shader::GetUniformLocation(shader, "u_resolution");
                    int u_zoom = rev::shader::GetUniformLocation(shader, "u_zoom");
                    int u_rotation = rev::shader::GetUniformLocation(shader, "u_rotation");
                    
                    rev::shader::SetFloat(shader, u_time, time);
                    rev::shader::SetVec2(shader, u_resolution, 1920.0f, 1080.0f);
                    rev::shader::SetFloat(shader, u_zoom, rev::curve::Evaluate(scene3_zoom, time));
                    rev::shader::SetFloat(shader, u_rotation, rev::curve::Evaluate(scene3_rotation, time));
                    break;
                }
                case 4: { // Burst scene
                    shader = scene4;
                    rev::shader::Use(shader);
                    int u_time = rev::shader::GetUniformLocation(shader, "u_time");
                    int u_resolution = rev::shader::GetUniformLocation(shader, "u_resolution");
                    int u_rays = rev::shader::GetUniformLocation(shader, "u_rays");
                    int u_pulse = rev::shader::GetUniformLocation(shader, "u_pulse");
                    
                    rev::shader::SetFloat(shader, u_time, time);
                    rev::shader::SetVec2(shader, u_resolution, 1920.0f, 1080.0f);
                    rev::shader::SetFloat(shader, u_rays, rev::curve::Evaluate(scene4_rays, time));
                    rev::shader::SetFloat(shader, u_pulse, rev::curve::Evaluate(scene4_pulse, time));
                    break;
                }
            }
            
            if (shader) {
                // TODO: Apply opacity for blending
                // For now, scenes will hard-cut based on timeline
                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            }
        }
        
        // Swap buffers
        rev::platform::SwapBuffers(window);
        
        // Exit after 60 seconds or on ESC
        if (time >= 60.0f || rev::platform::IsKeyPressed(window, VK_ESCAPE)) {
            break;
        }
    }
    
    // Cleanup
    rev::curve::DestroyCurve(scene1_speed);
    rev::curve::DestroyCurve(scene1_complexity);
    rev::curve::DestroyCurve(scene2_speed);
    rev::curve::DestroyCurve(scene2_twist);
    rev::curve::DestroyCurve(scene3_zoom);
    rev::curve::DestroyCurve(scene3_rotation);
    rev::curve::DestroyCurve(scene4_rays);
    rev::curve::DestroyCurve(scene4_pulse);
    rev::sequence::DestroyTimeline(timeline);
    rev::shader::DestroyProgram(scene1);
    rev::shader::DestroyProgram(scene2);
    rev::shader::DestroyProgram(scene3);
    rev::shader::DestroyProgram(scene4);
    rev::platform::DestroyIntroWindow(window);
    
    return 0;
}
