#pragma once

#include <cstdint>

namespace rev {
namespace shader {

// Opaque shader program handle
struct Program {
    uint32_t gl_program;
};

// Compilation
Program* CompileFromSource(const char* vertex_src, const char* fragment_src);
void DestroyProgram(Program* program);
void Use(Program* program);

// Uniforms
int GetUniformLocation(Program* program, const char* name);
void SetFloat(Program* program, int location, float value);
void SetVec2(Program* program, int location, float x, float y);
void SetVec3(Program* program, int location, float x, float y, float z);
void SetVec4(Program* program, int location, float x, float y, float z, float w);
void SetMat4(Program* program, int location, const float* matrix);
void SetInt(Program* program, int location, int value);

}  // namespace shader
}  // namespace rev
