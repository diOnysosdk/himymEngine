#include "rev_platform.h"
#include "rev_shader.h"
#include <windows.h>
#include <cmath>

// OpenGL function declarations
extern "C" {
    void glClear(unsigned int mask);
    void glClearColor(float red, float green, float blue, float alpha);
    void glViewport(int x, int y, int width, int height);
}

// Simple vertex shader
const char* vertex_shader = R"(
#version 330 core
layout(location = 0) in vec3 a_pos;
uniform mat4 u_mvp;
void main() {
    gl_Position = u_mvp * vec4(a_pos, 1.0);
}
)";

// Simple fragment shader - solid color
const char* fragment_shader = R"(
#version 330 core
out vec4 frag_color;
uniform vec3 u_color;
void main() {
    frag_color = vec4(u_color, 1.0);
}
)";

int main() {
    // Create window
    rev::platform::WindowConfig config;
    config.width = 1280;
    config.height = 720;
    config.fullscreen = false;
    config.title = "Simple Triangle Test";
    
    rev::platform::Window* window = rev::platform::CreateIntroWindow(config);
    if (!window) {
        MessageBox(nullptr, "Failed to create window", "Error", MB_OK);
        return -1;
    }
    
    rev::platform::LoadGLFunctions();
    glViewport(0, 0, config.width, config.height);
    
    // Compile shader
    rev::shader::Program* shader = rev::shader::CompileFromSource(vertex_shader, fragment_shader);
    if (!shader) {
        MessageBox(nullptr, "Failed to compile shader", "Error", MB_OK);
        rev::platform::DestroyIntroWindow(window);
        return -1;
    }
    
    // Simple identity matrix for MVP (object at origin, no transformation)
    float mvp[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    
    int u_mvp_loc = rev::shader::GetUniformLocation(shader, "u_mvp");
    int u_color_loc = rev::shader::GetUniformLocation(shader, "u_color");
    
    // VAO/VBO for triangle
    typedef void (*PFNGLGENVERTEXARRAYSPROC)(int n, unsigned int* arrays);
    typedef void (*PFNGLBINDVERTEXARRAYPROC)(unsigned int array);
    typedef void (*PFNGLGENBUFFERSPROC)(int n, unsigned int* buffers);
    typedef void (*PFNGLBINDBUFFERPROC)(unsigned int target, unsigned int buffer);
    typedef void (*PFNGLBUFFERDATAPROC)(unsigned int target, ptrdiff_t size, const void* data, unsigned int usage);
    typedef void (*PFNGLENABLEVERTEXATTRIBARRAYPROC)(unsigned int index);
    typedef void (*PFNGLVERTEXATTRIBPOINTERPROC)(unsigned int index, int size, unsigned int type, unsigned char normalized, int stride, const void* pointer);
    typedef void (*PFNGLDRAWARRAYSPROC)(unsigned int mode, int first, int count);
    
    PFNGLGENVERTEXARRAYSPROC glGenVertexArrays = (PFNGLGENVERTEXARRAYSPROC)wglGetProcAddress("glGenVertexArrays");
    PFNGLBINDVERTEXARRAYPROC glBindVertexArray = (PFNGLBINDVERTEXARRAYPROC)wglGetProcAddress("glBindVertexArray");
    PFNGLGENBUFFERSPROC glGenBuffers = (PFNGLGENBUFFERSPROC)wglGetProcAddress("glGenBuffers");
    PFNGLBINDBUFFERPROC glBindBuffer = (PFNGLBINDBUFFERPROC)wglGetProcAddress("glBindBuffer");
    PFNGLBUFFERDATAPROC glBufferData = (PFNGLBUFFERDATAPROC)wglGetProcAddress("glBufferData");
    PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC)wglGetProcAddress("glEnableVertexAttribArray");
    PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer = (PFNGLVERTEXATTRIBPOINTERPROC)wglGetProcAddress("glVertexAttribPointer");
    PFNGLDRAWARRAYSPROC glDrawArrays = (PFNGLDRAWARRAYSPROC)wglGetProcAddress("glDrawArrays");
    
    // Triangle vertices (in normalized device coordinates -1 to 1)
    float vertices[] = {
         0.0f,  0.5f, 0.0f,  // Top
        -0.5f, -0.5f, 0.0f,  // Bottom Left
         0.5f, -0.5f, 0.0f   // Bottom Right
    };
    
    unsigned int vao, vbo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    
    glBindVertexArray(vao);
    glBindBuffer(0x8892, vbo);  // GL_ARRAY_BUFFER
    glBufferData(0x8892, sizeof(vertices), vertices, 0x88E4);  // GL_STATIC_DRAW
    
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, 0x1406, 0, 3 * sizeof(float), (void*)0);  // GL_FLOAT
    
    glBindVertexArray(0);
    
    // Render loop
    double start_time = rev::platform::GetTime();
    glClearColor(0.2f, 0.3f, 0.4f, 1.0f);
    
    bool shown_message = false;
    
    while (!window->should_close && rev::platform::PollEvents(window)) {
        double current_time = rev::platform::GetTime();
        float time = (float)(current_time - start_time);
        
        if (time > 1.0f && !shown_message) {
            MessageBox(nullptr, "If you see a bright yellow triangle, rendering works!", "Test", MB_OK);
            shown_message = true;
        }
        
        if (time > 10.0f || rev::platform::IsKeyPressed(window, VK_ESCAPE)) {
            break;
        }
        
        glClear(0x00004000);  // GL_COLOR_BUFFER_BIT
        
        rev::shader::Use(shader);
        rev::shader::SetMat4(shader, u_mvp_loc, mvp);
        rev::shader::SetVec3(shader, u_color_loc, 1.0f, 1.0f, 0.0f);  // Bright yellow
        
        glBindVertexArray(vao);
        glDrawArrays(0x0004, 0, 3);  // GL_TRIANGLES
        glBindVertexArray(0);
        
        rev::platform::SwapBuffers(window);
    }
    
    // Cleanup
    rev::shader::DestroyProgram(shader);
    rev::platform::DestroyIntroWindow(window);
    
    return 0;
}
