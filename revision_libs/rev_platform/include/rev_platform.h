#pragma once

#include <cstdint>

namespace rev {
namespace platform {

// Message callback for external message handling (e.g., ImGui)
typedef long long (*MessageCallbackFn)(void* hwnd, unsigned int msg, unsigned long long wparam, long long lparam);

// Window configuration
struct WindowConfig {
    int width = 1920;
    int height = 1080;
    bool fullscreen = true;
    const char* title = "Intro";
};

// Opaque window handle
struct Window {
    void* hwnd;
    void* hdc;
    void* hglrc;
    bool should_close;
    MessageCallbackFn message_callback;
    int win_width;   // Actual framebuffer width  (physical pixels, set by CreateIntroWindow)
    int win_height;  // Actual framebuffer height (physical pixels, set by CreateIntroWindow)
};

// Lifecycle
Window* CreateIntroWindow(const WindowConfig& config);
void DestroyIntroWindow(Window* window);
bool PollEvents(Window* window);
void SwapBuffers(Window* window);

// Timing
double GetTime();  // Seconds since initialization
void Sleep(double seconds);

// Input
bool IsKeyPressed(Window* window, int vk_code);
bool IsMouseButtonPressed(Window* window, int button);
void GetMousePosition(Window* window, int* x, int* y);

// Message handling
void SetMessageCallback(Window* window, MessageCallbackFn callback);

// OpenGL
void* GetProcAddress(const char* name);
bool LoadGLFunctions();  // Load core GL 3.3 functions

}  // namespace platform
}  // namespace rev
