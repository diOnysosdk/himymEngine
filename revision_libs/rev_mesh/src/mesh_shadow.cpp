#include "rev_mesh_shadow.h"
#include "rev_mesh.h"
#include "rev_shader.h"
#include "rev_runtime.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <gl/gl.h>
#include <cmath>
#include <cstring>

namespace rev {
namespace mesh {

typedef void (APIENTRY *GenFramebuffersProc)(GLsizei, GLuint*);
typedef void (APIENTRY *BindFramebufferProc)(GLenum, GLuint);
typedef void (APIENTRY *FramebufferTexture2DProc)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum (APIENTRY *CheckFramebufferStatusProc)(GLenum);
typedef void (APIENTRY *DeleteFramebuffersProc)(GLsizei, const GLuint*);
typedef void (APIENTRY *UniformMatrix4fvProc)(GLint, GLsizei, GLboolean, const GLfloat*);
typedef void (APIENTRY *ActiveTextureProc)(GLenum);

static const int kShadowSize = 1024;

static const char* kDepthVertex = R"(
#version 330 core
layout(location=0) in vec3 a_pos;
uniform mat4 u_model;
uniform mat4 u_light_matrix;
void main(){ gl_Position = u_light_matrix * u_model * vec4(a_pos,1.0); }
)";
static const char* kDepthFragment = R"(
#version 330 core
void main(){}
)";

bool CreateShadowMap(ShadowMap* shadow) {
    if (!shadow) return false;
    if (shadow->ready) return true;
    memset(shadow, 0, sizeof(*shadow));
    auto gen = (GenFramebuffersProc)wglGetProcAddress("glGenFramebuffers");
    auto bind = (BindFramebufferProc)wglGetProcAddress("glBindFramebuffer");
    auto attach = (FramebufferTexture2DProc)wglGetProcAddress("glFramebufferTexture2D");
    auto check = (CheckFramebufferStatusProc)wglGetProcAddress("glCheckFramebufferStatus");
    if (!gen || !bind || !attach || !check) return false;
    shadow->depth_program = rev::shader::CompileFromSource(kDepthVertex, kDepthFragment);
    if (!shadow->depth_program) return false;
    glGenTextures(1, &shadow->depth_texture);
    glBindTexture(GL_TEXTURE_2D, shadow->depth_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, 0x81A6, kShadowSize, kShadowSize, 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
    gen(1, &shadow->framebuffer);
    bind(0x8D40, shadow->framebuffer);
    attach(0x8D40, 0x8D00, GL_TEXTURE_2D, shadow->depth_texture, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    shadow->ready = check(0x8D40) == 0x8CD5;
    bind(0x8D40, 0);
    if (!shadow->ready) DestroyShadowMap(shadow);
    return shadow->ready;
}

void DestroyShadowMap(ShadowMap* shadow) {
    if (!shadow) return;
    auto del = (DeleteFramebuffersProc)wglGetProcAddress("glDeleteFramebuffers");
    if (shadow->framebuffer && del) del(1, &shadow->framebuffer);
    if (shadow->depth_texture) glDeleteTextures(1, &shadow->depth_texture);
    if (shadow->depth_program) rev::shader::DestroyProgram(shadow->depth_program);
    memset(shadow, 0, sizeof(*shadow));
}

static void TransformPoint(const float m[16], const float p[3], float out[3]) {
    out[0]=m[0]*p[0]+m[4]*p[1]+m[8]*p[2]+m[12];
    out[1]=m[1]*p[0]+m[5]*p[1]+m[9]*p[2]+m[13];
    out[2]=m[2]*p[0]+m[6]*p[1]+m[10]*p[2]+m[14];
}

bool RenderDirectionalShadow(ShadowMap* shadow, Mesh* mesh,
                             const float model[16], const float light_direction[3]) {
    if (!shadow || !mesh || !model || !light_direction ||
        (!shadow->ready && !CreateShadowMap(shadow))) return false;
    float minp[3] = {1e30f,1e30f,1e30f}, maxp[3] = {-1e30f,-1e30f,-1e30f};
    for (uint32_t i=0; i<mesh->vertex_count; ++i) {
        float p[3]; TransformPoint(model, mesh->vertices[i].pos, p);
        for (int c=0;c<3;++c){ if(p[c]<minp[c])minp[c]=p[c]; if(p[c]>maxp[c])maxp[c]=p[c]; }
    }
    float center[3]={(minp[0]+maxp[0])*.5f,(minp[1]+maxp[1])*.5f,(minp[2]+maxp[2])*.5f};
    float dx=maxp[0]-minp[0],dy=maxp[1]-minp[1],dz=maxp[2]-minp[2];
    float radius=.5f*sqrtf(dx*dx+dy*dy+dz*dz); if(radius<.5f)radius=.5f;
    float d[3]={light_direction[0],light_direction[1],light_direction[2]};
    float len=sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]); if(len<.0001f)return false;
    d[0]/=len;d[1]/=len;d[2]/=len;
    float eye[3]={center[0]-d[0]*radius*2.0f,center[1]-d[1]*radius*2.0f,center[2]-d[2]*radius*2.0f};
    float up[3]={0,1,0}; if(fabsf(d[1])>.95f){up[0]=1;up[1]=0;}
    float view[16],proj[16];
    rev::runtime::Mat4LookAt(view,eye,center,up);
    rev::runtime::Mat4Orthographic(proj,radius*1.1f,radius*1.1f,.05f,radius*4.0f,0,0);
    rev::runtime::Mat4Multiply(shadow->light_matrix,proj,view);
    auto bind=(BindFramebufferProc)wglGetProcAddress("glBindFramebuffer");
    auto uniform=(UniformMatrix4fvProc)wglGetProcAddress("glUniformMatrix4fv");
    GLint old_fbo=0, viewport[4]={}, old_cull_mode=GL_BACK;
    const GLboolean old_depth_test = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean old_cull_face = glIsEnabled(GL_CULL_FACE);
    glGetIntegerv(0x8CA6,&old_fbo); glGetIntegerv(GL_VIEWPORT,viewport);
    glGetIntegerv(GL_CULL_FACE_MODE, &old_cull_mode);
    bind(0x8D40,shadow->framebuffer); glViewport(0,0,kShadowSize,kShadowSize);
    glClear(GL_DEPTH_BUFFER_BIT); glEnable(GL_DEPTH_TEST); glEnable(GL_CULL_FACE); glCullFace(GL_FRONT);
    rev::shader::Use(shadow->depth_program);
    int lm=rev::shader::GetUniformLocation(shadow->depth_program,"u_light_matrix");
    int mm=rev::shader::GetUniformLocation(shadow->depth_program,"u_model");
    if(uniform){uniform(lm,1,GL_FALSE,shadow->light_matrix);uniform(mm,1,GL_FALSE,model);}
    rev::mesh::Render(mesh,-1);
    glCullFace((GLenum)old_cull_mode);
    if (!old_cull_face) glDisable(GL_CULL_FACE);
    if (!old_depth_test) glDisable(GL_DEPTH_TEST);
    bind(0x8D40,(GLuint)old_fbo); glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
    return true;
}

void BindDirectionalShadow(const ShadowMap* shadow, rev::shader::Program* program, int texture_unit) {
    if (!program) return;
    int enabled=shadow&&shadow->ready ? 1:0;
    rev::shader::SetInt(program,rev::shader::GetUniformLocation(program,"u_shadow_enabled"),enabled);
    if(!enabled)return;
    auto active=(ActiveTextureProc)wglGetProcAddress("glActiveTexture");
    auto uniform=(UniformMatrix4fvProc)wglGetProcAddress("glUniformMatrix4fv");
    if(active)active(0x84C0+texture_unit);
    glBindTexture(GL_TEXTURE_2D,shadow->depth_texture);
    rev::shader::SetInt(program,rev::shader::GetUniformLocation(program,"u_shadow_map"),texture_unit);
    int loc=rev::shader::GetUniformLocation(program,"u_light_matrix");
    if(uniform&&loc>=0)uniform(loc,1,GL_FALSE,shadow->light_matrix);
    if(active)active(0x84C0);
}

} // namespace mesh
} // namespace rev
