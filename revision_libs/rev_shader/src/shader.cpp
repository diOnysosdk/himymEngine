#include "rev_shader.h"
#include <windows.h>
#include <gl/gl.h>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

// Define missing GL constants
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS 0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS 0x8B82
#endif
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#endif
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#endif

// GL types
typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;

// GL function pointer types
typedef GLuint (APIENTRY *PFNGLCREATESHADERPROC)(GLenum type);
typedef void (APIENTRY *PFNGLSHADERSOURCEPROC)(GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length);
typedef void (APIENTRY *PFNGLCOMPILESHADERPROC)(GLuint shader);
typedef void (APIENTRY *PFNGLGETSHADERIVPROC)(GLuint shader, GLenum pname, GLint* params);
typedef void (APIENTRY *PFNGLGETSHADERINFOLOGPROC)(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
typedef GLuint (APIENTRY *PFNGLCREATEPROGRAMPROC)();
typedef void (APIENTRY *PFNGLATTACHSHADERPROC)(GLuint program, GLuint shader);
typedef void (APIENTRY *PFNGLLINKPROGRAMPROC)(GLuint program);
typedef void (APIENTRY *PFNGLGETPROGRAMIVPROC)(GLuint program, GLenum pname, GLint* params);
typedef void (APIENTRY *PFNGLGETPROGRAMINFOLOGPROC)(GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
typedef void (APIENTRY *PFNGLUSEPROGRAMPROC)(GLuint program);
typedef void (APIENTRY *PFNGLDELETESHADERPROC)(GLuint shader);
typedef void (APIENTRY *PFNGLDELETEPROGRAMPROC)(GLuint program);
typedef GLint (APIENTRY *PFNGLGETUNIFORMLOCATIONPROC)(GLuint program, const GLchar* name);
typedef void (APIENTRY *PFNGLUNIFORM1FPROC)(GLint location, GLfloat v0);
typedef void (APIENTRY *PFNGLUNIFORM2FPROC)(GLint location, GLfloat v0, GLfloat v1);
typedef void (APIENTRY *PFNGLUNIFORM3FPROC)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
typedef void (APIENTRY *PFNGLUNIFORM4FPROC)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
typedef void (APIENTRY *PFNGLUNIFORM1IPROC)(GLint location, GLint v0);
typedef void (APIENTRY *PFNGLUNIFORMMATRIX4FVPROC)(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);

// GL function pointers
static PFNGLCREATESHADERPROC glCreateShader = nullptr;
static PFNGLSHADERSOURCEPROC glShaderSource = nullptr;
static PFNGLCOMPILESHADERPROC glCompileShader = nullptr;
static PFNGLGETSHADERIVPROC glGetShaderiv = nullptr;
static PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog = nullptr;
static PFNGLCREATEPROGRAMPROC glCreateProgram = nullptr;
static PFNGLATTACHSHADERPROC glAttachShader = nullptr;
static PFNGLLINKPROGRAMPROC glLinkProgram = nullptr;
static PFNGLGETPROGRAMIVPROC glGetProgramiv = nullptr;
static PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog = nullptr;
static PFNGLUSEPROGRAMPROC glUseProgram = nullptr;
static PFNGLDELETESHADERPROC glDeleteShader = nullptr;
static PFNGLDELETEPROGRAMPROC glDeleteProgram = nullptr;
static PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation = nullptr;
static PFNGLUNIFORM1FPROC glUniform1f = nullptr;
static PFNGLUNIFORM2FPROC glUniform2f = nullptr;
static PFNGLUNIFORM3FPROC glUniform3f = nullptr;
static PFNGLUNIFORM4FPROC glUniform4f = nullptr;
static PFNGLUNIFORM1IPROC glUniform1i = nullptr;
static PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv = nullptr;

#ifndef HIMYM_CRINKLER_BUILD
static std::once_flag g_gl_load_once;
#endif
static bool g_gl_functions_ready = false;

static void LoadGLFunctionsDirect() {
        glCreateShader = (PFNGLCREATESHADERPROC)wglGetProcAddress("glCreateShader");
        glShaderSource = (PFNGLSHADERSOURCEPROC)wglGetProcAddress("glShaderSource");
        glCompileShader = (PFNGLCOMPILESHADERPROC)wglGetProcAddress("glCompileShader");
        glGetShaderiv = (PFNGLGETSHADERIVPROC)wglGetProcAddress("glGetShaderiv");
        glGetShaderInfoLog = (PFNGLGETSHADERINFOLOGPROC)wglGetProcAddress("glGetShaderInfoLog");
        glCreateProgram = (PFNGLCREATEPROGRAMPROC)wglGetProcAddress("glCreateProgram");
        glAttachShader = (PFNGLATTACHSHADERPROC)wglGetProcAddress("glAttachShader");
        glLinkProgram = (PFNGLLINKPROGRAMPROC)wglGetProcAddress("glLinkProgram");
        glGetProgramiv = (PFNGLGETPROGRAMIVPROC)wglGetProcAddress("glGetProgramiv");
        glGetProgramInfoLog = (PFNGLGETPROGRAMINFOLOGPROC)wglGetProcAddress("glGetProgramInfoLog");
        glUseProgram = (PFNGLUSEPROGRAMPROC)wglGetProcAddress("glUseProgram");
        glDeleteShader = (PFNGLDELETESHADERPROC)wglGetProcAddress("glDeleteShader");
        glDeleteProgram = (PFNGLDELETEPROGRAMPROC)wglGetProcAddress("glDeleteProgram");
        glGetUniformLocation = (PFNGLGETUNIFORMLOCATIONPROC)wglGetProcAddress("glGetUniformLocation");
        glUniform1f = (PFNGLUNIFORM1FPROC)wglGetProcAddress("glUniform1f");
        glUniform2f = (PFNGLUNIFORM2FPROC)wglGetProcAddress("glUniform2f");
        glUniform3f = (PFNGLUNIFORM3FPROC)wglGetProcAddress("glUniform3f");
        glUniform4f = (PFNGLUNIFORM4FPROC)wglGetProcAddress("glUniform4f");
        glUniform1i = (PFNGLUNIFORM1IPROC)wglGetProcAddress("glUniform1i");
        glUniformMatrix4fv = (PFNGLUNIFORMMATRIX4FVPROC)wglGetProcAddress("glUniformMatrix4fv");
        g_gl_functions_ready = glCreateShader && glShaderSource && glCompileShader &&
            glGetShaderiv && glGetShaderInfoLog && glCreateProgram && glAttachShader &&
            glLinkProgram && glGetProgramiv && glGetProgramInfoLog && glUseProgram &&
            glDeleteShader && glDeleteProgram && glGetUniformLocation && glUniform1f &&
            glUniform2f && glUniform3f && glUniform4f && glUniform1i && glUniformMatrix4fv;
}

static void LoadGLFunctions() {
#ifdef HIMYM_CRINKLER_BUILD
    // The competition runtime initializes and renders on one thread. Avoid
    // std::call_once here because the modern MSVC helper is forwarded through
    // a kernel32 API-set cycle that Crinkler 3.0b cannot resolve.
    if (!g_gl_functions_ready) LoadGLFunctionsDirect();
#else
    std::call_once(g_gl_load_once, LoadGLFunctionsDirect);
#endif
}

namespace rev {
namespace shader {

static const char* SkipUtf8Bom(const char* source) {
    if (source && (unsigned char)source[0] == 0xEF &&
        (unsigned char)source[1] == 0xBB && (unsigned char)source[2] == 0xBF)
        return source + 3;
    return source;
}

FragmentSourceVersionStatus GetFragmentSourceVersionStatus(const char* fragment_src) {
    const char* source = SkipUtf8Bom(fragment_src);
    if (!source) return FragmentSourceVersionMissing;
    const char* version = strstr(source, "#version");
    if (!version) return FragmentSourceVersionMissing;
    const char* line_end = strpbrk(version, "\r\n");
    const size_t line_length = line_end ? (size_t)(line_end - version) : strlen(version);
    std::string directive(version, line_length);
    return directive.find("330 core") != std::string::npos
        ? FragmentSourceVersionReady : FragmentSourceVersionConverted;
}

static std::string NormalizeFragmentVersion(const char* fragment_src) {
    const char* source = SkipUtf8Bom(fragment_src);
    if (!source) return {};
    const char* version = strstr(source, "#version");
    if (!version)
        return std::string("#version 330 core\n") + source;
    const char* line_end = strpbrk(version, "\r\n");
    std::string prefix(source, (size_t)(version - source));
    if (!line_end) return std::string("#version 330 core\n") + prefix;
    while (*line_end == '\r' || *line_end == '\n') ++line_end;
    return std::string("#version 330 core\n") + prefix + line_end;
}

static GLuint CompileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    
    GLint status;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        // In a real intro, you might want to handle this error silently or show it
        OutputDebugStringA("Shader compilation error: ");
        OutputDebugStringA(log);
        OutputDebugStringA("\n");
        return 0;
    }
    
    return shader;
}

Program* CompileFromSource(const char* vertex_src, const char* fragment_src) {
    LoadGLFunctions();
    if (!g_gl_functions_ready || !vertex_src || !fragment_src) {
        OutputDebugStringA("Required OpenGL 3.3 shader functions are unavailable.\n");
        return nullptr;
    }

    // Normalize imported desktop/ES Shadertoy sources to the engine's OpenGL
    // 3.3 contract before adding the mainImage bridge.
    std::string normalized_fragment = NormalizeFragmentVersion(fragment_src);
    fragment_src = normalized_fragment.c_str();

    // Accept the conventional Shadertoy entry point directly.  Keep this
    // adaptation here so editor preview, standalone playback, and packed
    // playback compile exactly the same source contract.
    std::string adapted_fragment;
    if (fragment_src && strstr(fragment_src, "mainImage") && !strstr(fragment_src, "void main(")) {
        const char* version_end = strchr(fragment_src, '\n');
        if (version_end) {
            adapted_fragment.assign(fragment_src, static_cast<size_t>(version_end - fragment_src + 1));
            adapted_fragment +=
                "in vec2 uv;\n"
                "out vec4 fragColor;\n"
                "uniform vec3 iResolution;\n"
                "uniform float iTime;\n"
                "uniform float iTimeDelta;\n"
                "uniform int iFrame;\n"
                "uniform vec4 iMouse;\n"
                "uniform sampler2D iChannel0;\n"
                "uniform sampler2D iChannel1;\n"
                "uniform sampler2D iChannel2;\n"
                "uniform sampler2D iChannel3;\n";
            adapted_fragment += version_end + 1;
            adapted_fragment +=
                "\nvoid main() { mainImage(fragColor, uv * iResolution.xy); }\n";
            fragment_src = adapted_fragment.c_str();
        }
    }
    
    // Compile vertex shader
    GLuint vertex_shader = CompileShader(GL_VERTEX_SHADER, vertex_src);
    if (!vertex_shader) return nullptr;
    
    // Compile fragment shader
    GLuint fragment_shader = CompileShader(GL_FRAGMENT_SHADER, fragment_src);
    if (!fragment_shader) {
        glDeleteShader(vertex_shader);
        return nullptr;
    }
    
    // Link program
    GLuint program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);
    
    GLint status;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (!status) {
        char log[512];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        OutputDebugStringA("Program linking error: ");
        OutputDebugStringA(log);
        OutputDebugStringA("\n");
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        glDeleteProgram(program);
        return nullptr;
    }
    
    // Clean up shaders (they're linked into the program now)
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    
    Program* prog = new Program();
    prog->gl_program = program;
    return prog;
}

void DestroyProgram(Program* program) {
    if (!program) return;
    glDeleteProgram(program->gl_program);
    delete program;
}

void Use(Program* program) {
    if (program) {
        glUseProgram(program->gl_program);
    }
}

int GetUniformLocation(Program* program, const char* name) {
    if (!program) return -1;
    return glGetUniformLocation(program->gl_program, name);
}

void SetFloat(Program* program, int location, float value) {
    if (location >= 0) {
        glUniform1f(location, value);
    }
}

void SetVec2(Program* program, int location, float x, float y) {
    if (location >= 0) {
        glUniform2f(location, x, y);
    }
}

void SetVec3(Program* program, int location, float x, float y, float z) {
    if (location >= 0) {
        glUniform3f(location, x, y, z);
    }
}

void SetVec4(Program* program, int location, float x, float y, float z, float w) {
    if (location >= 0) {
        glUniform4f(location, x, y, z, w);
    }
}

void SetMat4(Program* program, int location, const float* matrix) {
    if (location >= 0) {
        glUniformMatrix4fv(location, 1, GL_FALSE, matrix);
    }
}

void SetInt(Program* program, int location, int value) {
    if (location >= 0) {
        glUniform1i(location, value);
    }
}

void SetTextureUnit(Program* program, int location, int texture_unit) {
    SetInt(program, location, texture_unit);
}

}  // namespace shader
}  // namespace rev
