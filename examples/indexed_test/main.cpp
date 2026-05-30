#include "rev_platform.h"
#include "rev_shader.h"
#include <windows.h>
#include <gl/gl.h>

// Simple vertex shader
const char* vertex_shader = R"(
#version 330 core
layout(location = 0) in vec3 a_pos;
void main() {
    gl_Position = vec4(a_pos, 1.0);
}
)";

// Simple fragment shader
const char* fragment_shader = R"(
#version 330 core
out vec4 frag_color;
void main() {
    frag_color = vec4(1.0, 0.0, 0.0, 1.0);  // Red
}
)";

int main() {
    rev::platform::WindowConfig config;
    config.width = 1280;
    config.height = 720;
    config.fullscreen = false;
    config.title = "Indexed Rendering Test";
    
    rev::platform::Window* window = rev::platform::CreateIntroWindow(config);
    rev::platform::LoadGLFunctions();
    
    rev::shader::Program* shader = rev::shader::CompileFromSource(vertex_shader, fragment_shader);
    if (!shader) {
        MessageBox(nullptr, "Shader failed!", "Error", MB_OK);
        return -1;
    }
    
    // GL function pointers
    typedef void (*PFNGLGENVERTEXARRAYSPROC)(int n, unsigned int* arrays);
    typedef void (*PFNGLBINDVERTEXARRAYPROC)(unsigned int array);
    typedef void (*PFNGLGENBUFFERSPROC)(int n, unsigned int* buffers);
    typedef void (*PFNGLBINDBUFFERPROC)(unsigned int target, unsigned int buffer);
    typedef void (*PFNGLBUFFERDATAPROC)(unsigned int target, ptrdiff_t size, const void* data, unsigned int usage);
    typedef void (*PFNGLENABLEVERTEXATTRIBARRAYPROC)(unsigned int index);
    typedef void (*PFNGLVERTEXATTRIBPOINTERPROC)(unsigned int index, int size, unsigned int type, unsigned char normalized, int stride, const void* pointer);
    
    PFNGLGENVERTEXARRAYSPROC glGenVertexArrays = (PFNGLGENVERTEXARRAYSPROC)wglGetProcAddress("glGenVertexArrays");
    PFNGLBINDVERTEXARRAYPROC glBindVertexArray = (PFNGLBINDVERTEXARRAYPROC)wglGetProcAddress("glBindVertexArray");
    PFNGLGENBUFFERSPROC glGenBuffers = (PFNGLGENBUFFERSPROC)wglGetProcAddress("glGenBuffers");
    PFNGLBINDBUFFERPROC glBindBuffer = (PFNGLBINDBUFFERPROC)wglGetProcAddress("glBindBuffer");
    PFNGLBUFFERDATAPROC glBufferData = (PFNGLBUFFERDATAPROC)wglGetProcAddress("glBufferData");
    PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC)wglGetProcAddress("glEnableVertexAttribArray");
    PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer = (PFNGLVERTEXATTRIBPOINTERPROC)wglGetProcAddress("glVertexAttribPointer");
    
    // Triangle vertices
    float vertices[] = {
         0.0f,  0.5f, 0.0f,  // Top
        -0.5f, -0.5f, 0.0f,  // Bottom Left
         0.5f, -0.5f, 0.0f   // Bottom Right
    };
    
    // Indices
    unsigned int indices[] = { 0, 1, 2 };
    
    unsigned int vao, vbo, ibo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ibo);
    
    glBindVertexArray(vao);
    
    // Upload vertex data
    glBindBuffer(0x8892, vbo);  // GL_ARRAY_BUFFER
    glBufferData(0x8892, sizeof(vertices), vertices, 0x88E4);  // GL_STATIC_DRAW
    
    // Upload index data
    glBindBuffer(0x8893, ibo);  // GL_ELEMENT_ARRAY_BUFFER
    glBufferData(0x8893, sizeof(indices), indices, 0x88E4);
    
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, 0x1406, 0, 3 * sizeof(float), (void*)0);  // GL_FLOAT
    
    glBindVertexArray(0);
    
    // Render loop
    double start_time = rev::platform::GetTime();
    
    bool shown_message = false;
    while (!window->should_close && rev::platform::PollEvents(window)) {
        double current_time = rev::platform::GetTime();
        float time = (float)(current_time - start_time);
        
        if (time > 1.0f && !shown_message) {
            MessageBox(nullptr, "Using glDrawElements with indices. Do you see a red triangle?", "Test", MB_OK);
            shown_message = true;
        }
        
        if (time > 10.0f || rev::platform::IsKeyPressed(window, VK_ESCAPE)) {
            break;
        }
        
        glClearColor(0.2f, 0.3f, 0.4f, 1.0f);
        glClear(0x00004000);  // GL_COLOR_BUFFER_BIT
        
        rev::shader::Use(shader);
        glBindVertexArray(vao);
        
        // Use glDrawElements
        glDrawElements(0x0004, 3, 0x1405, nullptr);  // GL_TRIANGLES, 3 indices, GL_UNSIGNED_INT
        
        glBindVertexArray(0);
        rev::platform::SwapBuffers(window);
    }
    
    rev::shader::DestroyProgram(shader);
    rev::platform::DestroyIntroWindow(window);
    return 0;
}
