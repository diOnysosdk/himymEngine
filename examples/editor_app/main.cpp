#include <windows.h>
#include <gl/gl.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
#include "rev_platform.h"
#include "rev_editor.h"

// Define missing GL constants
#ifndef GL_COLOR_BUFFER_BIT
#define GL_COLOR_BUFFER_BIT 0x00004000
#endif

// GL 3.3 function pointers (VAO support required for core profile)
typedef void (APIENTRY *PFNGLGENVERTEXARRAYSPROC)(GLsizei n, GLuint* arrays);
typedef void (APIENTRY *PFNGLBINDVERTEXARRAYPROC)(GLuint array);

static PFNGLGENVERTEXARRAYSPROC glGenVertexArrays = nullptr;
static PFNGLBINDVERTEXARRAYPROC glBindVertexArray = nullptr;

int main() {
    // Create window and OpenGL context
    rev::platform::WindowConfig config;
    config.width = 1600;
    config.height = 900;
    config.fullscreen = false;
    config.title = "HiMYM - Scene Editor";
    
    rev::platform::Window* window = rev::platform::CreateIntroWindow(config);
    if (!window) {
        return -1;
    }
    
    // Load OpenGL functions
    rev::platform::LoadGLFunctions();
    
    // Initialize GDI+ for image loading
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);
    
    glGenVertexArrays = (PFNGLGENVERTEXARRAYSPROC)rev::platform::GetProcAddress("glGenVertexArrays");
    glBindVertexArray = (PFNGLBINDVERTEXARRAYPROC)rev::platform::GetProcAddress("glBindVertexArray");
    
    // Create and bind VAO
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    
    // Create editor
    rev::editor::EditorContext* editor = rev::editor::CreateEditor(window);
    if (!editor) {
        rev::platform::DestroyIntroWindow(window);
        return -1;
    }
    
    // Start with blank project (user can Open to choose file)
    rev::editor::NewProject(editor);
    
    // Main loop
    while (rev::platform::PollEvents(window) && !window->should_close) {
        // Clear background
        glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        
        // Editor UI
        rev::editor::BeginFrame(editor);
        rev::editor::RenderUI(editor);
        rev::editor::EndFrame(editor);
        
        // Swap buffers
        rev::platform::SwapBuffers(window);
        
        // Exit on ESC
        if (rev::platform::IsKeyPressed(window, VK_ESCAPE)) {
            break;
        }
    }
    
    // Cleanup
    rev::editor::DestroyEditor(editor);
    Gdiplus::GdiplusShutdown(gdiplusToken);
    rev::platform::DestroyIntroWindow(window);
    
    return 0;
}
