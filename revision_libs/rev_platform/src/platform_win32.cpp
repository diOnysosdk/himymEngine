#include "rev_platform.h"
#include <windows.h>
#include <gl/gl.h>

namespace rev {
namespace platform {

// WGL extension function pointers
typedef HGLRC (WINAPI * PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC, HGLRC, const int*);
typedef BOOL (WINAPI * PFNWGLCHOOSEPIXELFORMATARBPROC)(HDC, const int*, const FLOAT*, UINT, int*, UINT*);

static PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB = nullptr;
static PFNWGLCHOOSEPIXELFORMATARBPROC wglChoosePixelFormatARB = nullptr;

// Window procedure
static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    Window* window = reinterpret_cast<Window*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    
    // Allow external message handler (e.g., ImGui)
    if (window && window->message_callback) {
        LRESULT result = window->message_callback(hwnd, msg, wparam, lparam);
        if (result != 0) {
            return result;
        }
    }
    
    switch (msg) {
        case WM_CLOSE:
        case WM_DESTROY:
            if (window) {
                window->should_close = true;
            }
            return 0;
            
        case WM_KEYDOWN:
            return 0;
    }
    
    return DefWindowProc(hwnd, msg, wparam, lparam);
}

Window* CreateIntroWindow(const WindowConfig& config) {
    Window* window = new Window();
    window->should_close = false;
    window->message_callback = nullptr;
    window->win_width  = 0;
    window->win_height = 0;

    // Declare per-monitor DPI awareness so Windows does not virtualize pixel
    // sizes on high-DPI displays (125%, 150%, etc.).  Must be set before any
    // window or message-loop interaction.  The call is benign if already set.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Register window class
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = "IntroWindow";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassEx(&wc);
    
    // Window style — fullscreen uses WS_POPUP (no border/titlebar), windowed uses WS_OVERLAPPEDWINDOW.
    // Window size matches config.width/height; glViewport in the render loop explicitly sets the
    // GL viewport to config.width x config.height each frame so render proportions always match
    // the preview FBO (also 1920×1080).
    DWORD style = config.fullscreen ? WS_POPUP : WS_OVERLAPPEDWINDOW;
    int x = config.fullscreen ? 0 : CW_USEDEFAULT;
    int y = config.fullscreen ? 0 : CW_USEDEFAULT;
    int width  = config.width;
    int height = config.height;

    if (config.fullscreen) {
        // Use the actual physical screen dimensions so the window fills the
        // display regardless of the configured render resolution.
        width  = GetSystemMetrics(SM_CXSCREEN);
        height = GetSystemMetrics(SM_CYSCREEN);
    }

    window->win_width  = width;
    window->win_height = height;
    
    // Create window
    HWND hwnd = CreateWindowEx(
        0, "IntroWindow", config.title,
        style,
        x, y, width, height,
        nullptr, nullptr, GetModuleHandle(nullptr), nullptr
    );
    
    window->hwnd = hwnd;
    SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
    
    // Get device context
    HDC hdc = GetDC(hwnd);
    window->hdc = hdc;
    
    // Set pixel format
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    
    int pixel_format = ChoosePixelFormat(hdc, &pfd);
    SetPixelFormat(hdc, pixel_format, &pfd);
    
    // Create temporary context to load WGL extensions
    HGLRC temp_context = wglCreateContext(hdc);
    wglMakeCurrent(hdc, temp_context);
    
    // Load WGL extensions
    wglCreateContextAttribsARB = (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");
    
    // Create modern OpenGL 3.3 context
    const int attribs[] = {
        0x2091, 3,  // WGL_CONTEXT_MAJOR_VERSION_ARB
        0x2092, 3,  // WGL_CONTEXT_MINOR_VERSION_ARB
        0x9126, 0x00000001,  // WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB
        0
    };
    
    HGLRC hglrc = wglCreateContextAttribsARB(hdc, nullptr, attribs);
    wglMakeCurrent(hdc, hglrc);
    wglDeleteContext(temp_context);
    
    window->hglrc = hglrc;
    
    // Show window
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    
    if (config.fullscreen) {
        ShowCursor(FALSE);
    }
    
    return window;
}

void DestroyIntroWindow(Window* window) {
    if (!window) return;
    
    HWND hwnd = static_cast<HWND>(window->hwnd);
    HDC hdc = static_cast<HDC>(window->hdc);
    HGLRC hglrc = static_cast<HGLRC>(window->hglrc);
    
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(hglrc);
    ReleaseDC(hwnd, hdc);
    ::DestroyWindow(hwnd);  // Use Windows API DestroyWindow
    
    delete window;
}

bool PollEvents(Window* window) {
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return !window->should_close;
}

void SwapBuffers(Window* window) {
    HDC hdc = static_cast<HDC>(window->hdc);
    ::SwapBuffers(hdc);
}

bool IsKeyPressed(Window* window, int vk_code) {
    return (GetAsyncKeyState(vk_code) & 0x8000) != 0;
}

void SetMessageCallback(Window* window, MessageCallbackFn callback) {
    if (window) {
        window->message_callback = callback;
    }
}

bool IsMouseButtonPressed(Window* window, int button) {
    return (GetAsyncKeyState(button) & 0x8000) != 0;
}

void GetMousePosition(Window* window, int* x, int* y) {
    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(static_cast<HWND>(window->hwnd), &pt);
    if (x) *x = pt.x;
    if (y) *y = pt.y;
}

}  // namespace platform
}  // namespace rev
