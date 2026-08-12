#include "rev_platform.h"
#include <windows.h>
#include <gl/gl.h>
#include <cstdio>

#ifndef GL_SHADING_LANGUAGE_VERSION
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C
#endif

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

bool GetOpenGLInfo(OpenGLInfo* info) {
    if (!info || !wglGetCurrentContext()) return false;
    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    if (!version) return false;
    int major = 0;
    int minor = 0;
    if (sscanf_s(version, "%d.%d", &major, &minor) != 2) return false;
    info->major = major;
    info->minor = minor;
    info->version = version;
    info->glsl_version = reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));
    info->vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    info->renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    return true;
}

}  // namespace platform
}  // namespace rev
