#pragma once

#include <cstdint>

namespace rev {
namespace platform {

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
};

// Lifecycle
Window* CreateWindow(const WindowConfig& config);
void DestroyWindow(Window* window);
bool PollEvents(Window* window);
void SwapBuffers(Window* window);

// Timing
double GetTime();  // Seconds since initialization
void Sleep(double seconds);

// Input
bool IsKeyPressed(Window* window, int vk_code);
bool IsMouseButtonPressed(Window* window, int button);
void GetMousePosition(Window* window, int* x, int* y);

// OpenGL
void* GetProcAddress(const char* name);
bool LoadGLFunctions();  // Load core GL 3.3 functions

}  // namespace platform
}  // namespace rev
