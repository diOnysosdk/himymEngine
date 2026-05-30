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

// GL function pointers
typedef void (APIENTRY *PFNGLCREATESHADERPROC)(unsigned int type);
typedef void (APIENTRY *PFNGLSHADERSOURCEPROC)(unsigned int shader, int count, const char** string, const int* length);
typedef void (APIENTRY *PFNGLCOMPILESHADERPROC)(unsigned int shader);
typedef void (APIENTRY *PFNGLCREATEPROGRAMPROC)();
typedef void (APIENTRY *PFNGLATTACHSHADERPROC)(unsigned int program, unsigned int shader);
typedef void (APIENTRY *PFNGLLINKPROGRAMPROC)(unsigned int program);
typedef void (APIENTRY *PFNGLUSEPROGRAMPROC)(unsigned int program);
typedef void (APIENTRY *PFNGLGETUNIFORMLOCATIONPROC)(unsigned int program, const char* name);
typedef void (APIENTRY *PFNGLUNIFORM1FPROC)(int location, float v0);
typedef void (APIENTRY *PFNGLUNIFORM2FPROC)(int location, float v0, float v1);
typedef void (APIENTRY *PFNGLUNIFORM3FPROC)(int location, float v0, float v1, float v2);
typedef void (APIENTRY *PFNGLUNIFORM4FPROC)(int location, float v0, float v1, float v2, float v3);
typedef void (APIENTRY *PFNGLUNIFORM1IPROC)(int location, int v0);
typedef void (APIENTRY *PFNGLUNIFORMMATRIX4FVPROC)(int location, int count, unsigned char transpose, const float* value);

// GL 3.3 function pointers (declared in a way that can be linked externally)
// These are intentionally left as simple declarations for now
// In a real implementation, these would be loaded dynamically

bool LoadGLFunctions() {
    // In a minimal intro, we typically load only the functions we need
    // For now, this is a stub that returns true
    // Real implementation would use GetProcAddress for each GL function
    
    // Example:
    // glCreateShader = (PFNGLCREATESHADERPROC)GetProcAddress("glCreateShader");
    // glShaderSource = (PFNGLSHADERSOURCEPROC)GetProcAddress("glShaderSource");
    // etc.
    
    return true;
}

}  // namespace platform
}  // namespace rev
