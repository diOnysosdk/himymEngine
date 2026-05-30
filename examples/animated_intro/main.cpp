#include <windows.h>
#include <gl/gl.h>
#include "rev_platform.h"
#include "rev_shader.h"
#include "rev_curve.h"
#include "rev_sequence.h"

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

// Fragment shader with animated parameters
const char* fragment_shader = R"(
#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform float u_time;
uniform vec2 u_resolution;
uniform float u_speed;      // Animated by curve
uniform float u_intensity;  // Animated by curve
uniform vec3 u_color;       // Animated by timeline fade

void main() {
    vec2 p = uv * 2.0 - 1.0;
    p.x *= u_resolution.x / u_resolution.y;
    
    float d = length(p);
    float t = u_time * u_speed;
    
    // Animated pattern
    float pattern = sin(t + d * 3.0 * u_intensity);
    
    vec3 col = u_color * (0.5 + 0.5 * pattern);
    
    fragColor = vec4(col, 1.0);
}
)";

int main() {
    // Create window and OpenGL context
    rev::platform::WindowConfig config;
    config.width = 1920;
    config.height = 1080;
    config.fullscreen = false;
    config.title = "HiMYM - Animated Intro (Phase 2 Test)";
    
    rev::platform::Window* window = rev::platform::CreateIntroWindow(config);
    if (!window) {
        return -1;
    }
    
    // Load OpenGL functions
    rev::platform::LoadGLFunctions();
    glGenVertexArrays = (PFNGLGENVERTEXARRAYSPROC)rev::platform::GetProcAddress("glGenVertexArrays");
    glBindVertexArray = (PFNGLBINDVERTEXARRAYPROC)rev::platform::GetProcAddress("glBindVertexArray");
    
    // Create and bind VAO
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    
    // Compile shader
    rev::shader::Program* shader = rev::shader::CompileFromSource(vertex_shader, fragment_shader);
    if (!shader) {
        rev::platform::DestroyIntroWindow(window);
        return -1;
    }
    
    // Get uniform locations
    int u_time_loc = rev::shader::GetUniformLocation(shader, "u_time");
    int u_resolution_loc = rev::shader::GetUniformLocation(shader, "u_resolution");
    int u_speed_loc = rev::shader::GetUniformLocation(shader, "u_speed");
    int u_intensity_loc = rev::shader::GetUniformLocation(shader, "u_intensity");
    int u_color_loc = rev::shader::GetUniformLocation(shader, "u_color");
    
    // Create animation curves
    // Speed curve: starts slow, speeds up, then slows down
    rev::curve::Curve speed_curve = rev::curve::CreateCurve(4);
    rev::curve::AddPoint(speed_curve, 0.0f, 0.3f, rev::curve::EaseMode::Linear);
    rev::curve::AddPoint(speed_curve, 5.0f, 2.0f, rev::curve::EaseMode::EaseInOut);
    rev::curve::AddPoint(speed_curve, 10.0f, 0.5f, rev::curve::EaseMode::EaseOut);
    rev::curve::AddPoint(speed_curve, 15.0f, 1.0f, rev::curve::EaseMode::Linear);
    rev::curve::SortPoints(speed_curve);
    
    // Intensity curve: pulses over time
    rev::curve::Curve intensity_curve = rev::curve::CreateCurve(5);
    rev::curve::AddPoint(intensity_curve, 0.0f, 1.0f, rev::curve::EaseMode::Smoothstep);
    rev::curve::AddPoint(intensity_curve, 3.0f, 3.0f, rev::curve::EaseMode::Smoothstep);
    rev::curve::AddPoint(intensity_curve, 6.0f, 0.5f, rev::curve::EaseMode::EaseInOut);
    rev::curve::AddPoint(intensity_curve, 10.0f, 2.5f, rev::curve::EaseMode::Smoothstep);
    rev::curve::AddPoint(intensity_curve, 15.0f, 1.5f, rev::curve::EaseMode::Linear);
    rev::curve::SortPoints(intensity_curve);
    
    // Create timeline with color transitions
    rev::sequence::Timeline timeline = rev::sequence::CreateTimeline(3);
    
    // Scene 1: Red (0-5s, fade in 1s, fade out 1s)
    rev::sequence::AddCue(timeline, 0.0f, 5.0f, 1.0f, 1.0f, 1);
    
    // Scene 2: Green (4-10s, fade in 2s, fade out 1s) - overlaps with scene 1
    rev::sequence::AddCue(timeline, 4.0f, 10.0f, 2.0f, 1.0f, 2);
    
    // Scene 3: Blue (9-15s, fade in 1.5s, no fade out)
    rev::sequence::AddCue(timeline, 9.0f, 15.0f, 1.5f, 0.0f, 3);
    
    rev::sequence::SortCues(timeline);
    
    // Main loop
    double start_time = rev::platform::GetTime();
    
    while (rev::platform::PollEvents(window) && !window->should_close) {
        double current_time = rev::platform::GetTime();
        float time = static_cast<float>(current_time - start_time);
        
        // Update timeline
        rev::sequence::SetTime(timeline, time);
        
        // Get active cues and blend colors
        rev::sequence::Cue* active_cues[8];
        int active_count = rev::sequence::GetActiveCues(timeline, active_cues, 8);
        
        // Blend colors based on active cues
        float r = 0.0f, g = 0.0f, b = 0.0f;
        float total_opacity = 0.0f;
        
        for (int i = 0; i < active_count; ++i) {
            float opacity = active_cues[i]->opacity;
            total_opacity += opacity;
            
            switch (active_cues[i]->id) {
                case 1: r += opacity; break;  // Red
                case 2: g += opacity; break;  // Green
                case 3: b += opacity; break;  // Blue
            }
        }
        
        // Normalize colors
        if (total_opacity > 0.0f) {
            r /= total_opacity;
            g /= total_opacity;
            b /= total_opacity;
        }
        
        // Evaluate curves
        float speed = rev::curve::Evaluate(speed_curve, time);
        float intensity = rev::curve::Evaluate(intensity_curve, time);
        
        // Clear screen
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        
        // Use shader and set uniforms
        rev::shader::Use(shader);
        rev::shader::SetFloat(shader, u_time_loc, time);
        rev::shader::SetVec2(shader, u_resolution_loc, 
                            static_cast<float>(config.width), 
                            static_cast<float>(config.height));
        rev::shader::SetFloat(shader, u_speed_loc, speed);
        rev::shader::SetFloat(shader, u_intensity_loc, intensity);
        rev::shader::SetVec3(shader, u_color_loc, r, g, b);
        
        // Draw fullscreen quad
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        
        // Swap buffers
        rev::platform::SwapBuffers(window);
        
        // Exit after 15 seconds or on ESC
        if (time > 15.0f || rev::platform::IsKeyPressed(window, VK_ESCAPE)) {
            break;
        }
    }
    
    // Cleanup
    rev::curve::DestroyCurve(speed_curve);
    rev::curve::DestroyCurve(intensity_curve);
    rev::sequence::DestroyTimeline(timeline);
    rev::shader::DestroyProgram(shader);
    rev::platform::DestroyIntroWindow(window);
    
    return 0;
}
