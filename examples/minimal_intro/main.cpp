#include "rev_platform.h"
#include "rev_shader.h"
#include "rev_xm.h"

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

// Minimal fragment shader - animated gradient
const char* fragment_shader = R"(
#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform float u_time;
uniform vec2 u_resolution;

void main() {
    vec2 p = uv * 2.0 - 1.0;
    p.x *= u_resolution.x / u_resolution.y;
    
    float d = length(p);
    float t = u_time * 0.5;
    
    vec3 col = vec3(
        0.5 + 0.5 * sin(t + d * 3.0),
        0.5 + 0.5 * sin(t + d * 3.0 + 2.094),
        0.5 + 0.5 * sin(t + d * 3.0 + 4.188)
    );
    
    fragColor = vec4(col, 1.0);
}
)";

int main() {
    // Create window and OpenGL context
    rev::platform::WindowConfig config;
    config.width = 1920;
    config.height = 1080;
    config.fullscreen = false;  // Windowed for testing
    config.title = "HiMYM - Minimal Intro Test";
    
    rev::platform::Window* window = rev::platform::CreateWindow(config);
    if (!window) {
        return -1;
    }
    
    // Load OpenGL functions
    rev::platform::LoadGLFunctions();
    
    // Compile shader
    rev::shader::Program* shader = rev::shader::CompileFromSource(vertex_shader, fragment_shader);
    if (!shader) {
        rev::platform::DestroyWindow(window);
        return -1;
    }
    
    // Get uniform locations
    int u_time_loc = rev::shader::GetUniformLocation(shader, "u_time");
    int u_resolution_loc = rev::shader::GetUniformLocation(shader, "u_resolution");
    
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
    rev::platform::DestroyWindow(window);
    
    return 0;
}
