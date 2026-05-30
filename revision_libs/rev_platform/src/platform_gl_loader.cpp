#include "rev_platform.h"
#include <windows.h>
#include <gl/gl.h>

namespace rev {
namespace platform {

void* GetProcAddress(const char* name) {
    void* proc = (void*)wglGetProcAddress(name);
    if (!proc) {
        HMODULE module = LoadLibraryA("opengl32.dll");
        proc = (void*)::GetProcAddress(module, name);
    }
    return proc;
}

bool LoadGLFunctions() {
    // GL functions are loaded on-demand in rev_shader
    // This function exists for future expansion
    return true;
}

}  // namespace platform
}  // namespace rev
