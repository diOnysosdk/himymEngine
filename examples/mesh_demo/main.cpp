#include "rev_platform.h"
#include "rev_shader.h"
#include "rev_mesh.h"
#include <windows.h>
#include <cmath>

// OpenGL function declarations
extern "C" {
    void glEnable(unsigned int cap);
    void glClear(unsigned int mask);
    void glClearColor(float red, float green, float blue, float alpha);
}

const char* vertex_shader = R"(
#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;

out vec3 v_normal;
out vec3 v_pos;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_projection;

void main() {
    v_pos = (u_model * vec4(a_pos, 1.0)).xyz;
    v_normal = mat3(transpose(inverse(u_model))) * a_normal;
    gl_Position = u_projection * u_view * vec4(v_pos, 1.0);
}
)";

const char* fragment_shader = R"(
#version 330 core
in vec3 v_normal;
in vec3 v_pos;

out vec4 frag_color;

uniform vec3 u_light_pos;
uniform vec3 u_view_pos;
uniform vec3 u_color;

void main() {
    // Ambient
    float ambient_strength = 0.1;
    vec3 ambient = ambient_strength * u_color;
    
    // Diffuse
    vec3 norm = normalize(v_normal);
    vec3 light_dir = normalize(u_light_pos - v_pos);
    float diff = max(dot(norm, light_dir), 0.0);
    vec3 diffuse = diff * u_color;
    
    // Specular
    float specular_strength = 0.5;
    vec3 view_dir = normalize(u_view_pos - v_pos);
    vec3 reflect_dir = reflect(-light_dir, norm);
    float spec = pow(max(dot(view_dir, reflect_dir), 0.0), 32.0);
    vec3 specular = specular_strength * spec * vec3(1.0);
    
    vec3 result = ambient + diffuse + specular;
    frag_color = vec4(result, 1.0);
}
)";

// Simple matrix helper functions
void MatrixIdentity(float* m) {
    for (int i = 0; i < 16; ++i) m[i] = 0.0f;
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

void MatrixPerspective(float* m, float fov, float aspect, float znear, float zfar) {
    float f = 1.0f / tanf(fov * 0.5f);
    MatrixIdentity(m);
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (zfar + znear) / (znear - zfar);
    m[11] = -1.0f;
    m[14] = (2.0f * zfar * znear) / (znear - zfar);
    m[15] = 0.0f;
}

void MatrixLookAt(float* m, float eye_x, float eye_y, float eye_z, float center_x, float center_y, float center_z) {
    float fx = center_x - eye_x;
    float fy = center_y - eye_y;
    float fz = center_z - eye_z;
    float len = sqrtf(fx*fx + fy*fy + fz*fz);
    fx /= len; fy /= len; fz /= len;
    
    float ux = 0.0f, uy = 1.0f, uz = 0.0f;
    
    float sx = fy * uz - fz * uy;
    float sy = fz * ux - fx * uz;
    float sz = fx * uy - fy * ux;
    len = sqrtf(sx*sx + sy*sy + sz*sz);
    sx /= len; sy /= len; sz /= len;
    
    ux = sy * fz - sz * fy;
    uy = sz * fx - sx * fz;
    uz = sx * fy - sy * fx;
    
    MatrixIdentity(m);
    m[0] = sx; m[4] = ux; m[8] = -fx; m[12] = -(sx*eye_x + ux*eye_y - fx*eye_z);
    m[1] = sy; m[5] = uy; m[9] = -fy; m[13] = -(sy*eye_x + uy*eye_y - fy*eye_z);
    m[2] = sz; m[6] = uz; m[10] = -fz; m[14] = -(sz*eye_x + uz*eye_y - fz*eye_z);
}

void MatrixRotateY(float* m, float angle) {
    float c = cosf(angle);
    float s = sinf(angle);
    MatrixIdentity(m);
    m[0] = c;
    m[2] = s;
    m[8] = -s;
    m[10] = c;
}

int main() {
    // Create window
    rev::platform::WindowConfig config;
    config.width = 1280;
    config.height = 720;
    config.fullscreen = false;
    config.title = "3D Mesh Rendering Demo";
    
    rev::platform::Window* window = rev::platform::CreateIntroWindow(config);
    rev::platform::LoadGLFunctions();
    
    // Compile shader
    rev::shader::Program* shader = rev::shader::CompileFromSource(vertex_shader, fragment_shader);
    
    // Create meshes
    rev::mesh::Mesh* cube = rev::mesh::CreateCube(1.0f);
    rev::mesh::Mesh* sphere = rev::mesh::CreateSphere(0.8f, 32);
    rev::mesh::Mesh* torus = rev::mesh::CreateTorus(0.8f, 0.3f, 32, 16);
    
    rev::mesh::UploadToGPU(cube);
    rev::mesh::UploadToGPU(sphere);
    rev::mesh::UploadToGPU(torus);
    
    // Get uniform locations
    int u_model_loc = rev::shader::GetUniformLocation(shader, "u_model");
    int u_view_loc = rev::shader::GetUniformLocation(shader, "u_view");
    int u_projection_loc = rev::shader::GetUniformLocation(shader, "u_projection");
    int u_light_pos_loc = rev::shader::GetUniformLocation(shader, "u_light_pos");
    int u_view_pos_loc = rev::shader::GetUniformLocation(shader, "u_view_pos");
    int u_color_loc = rev::shader::GetUniformLocation(shader, "u_color");
    
    // Setup matrices
    float projection[16];
    MatrixPerspective(projection, 3.14159f / 4.0f, 1280.0f / 720.0f, 0.1f, 100.0f);
    
    float view[16];
    MatrixLookAt(view, 0.0f, 2.0f, 8.0f, 0.0f, 0.0f, 0.0f);
    
    double start_time = rev::platform::GetTime();
    
    // Enable depth testing
    glEnable(0x0B71);  // GL_DEPTH_TEST
    glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
    
    // Render loop
    while (!window->should_close && rev::platform::PollEvents(window)) {
        double current_time = rev::platform::GetTime();
        float time = (float)(current_time - start_time);
        
        // Exit after 20 seconds or on ESC
        if (time > 20.0f || rev::platform::IsKeyPressed(window, VK_ESCAPE)) {
            break;
        }
        
        glClear(0x00004100);  // GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT
        
        rev::shader::Use(shader);
        rev::shader::SetMat4(shader, u_view_loc, view);
        rev::shader::SetMat4(shader, u_projection_loc, projection);
        rev::shader::SetVec3(shader, u_light_pos_loc, 5.0f, 5.0f, 5.0f);
        rev::shader::SetVec3(shader, u_view_pos_loc, 0.0f, 2.0f, 8.0f);
        
        // Render cube (left)
        float cube_model[16];
        MatrixRotateY(cube_model, time * 0.5f);
        cube_model[12] = -2.5f;  // Translate left
        rev::shader::SetMat4(shader, u_model_loc, cube_model);
        rev::shader::SetVec3(shader, u_color_loc, 1.0f, 0.3f, 0.3f);
        rev::mesh::Render(cube);
        
        // Render sphere (center)
        float sphere_model[16];
        MatrixRotateY(sphere_model, time * 0.7f);
        sphere_model[12] = 0.0f;  // Center
        rev::shader::SetMat4(shader, u_model_loc, sphere_model);
        rev::shader::SetVec3(shader, u_color_loc, 0.3f, 1.0f, 0.3f);
        rev::mesh::Render(sphere);
        
        // Render torus (right)
        float torus_model[16];
        MatrixRotateY(torus_model, time * 0.9f);
        torus_model[12] = 2.5f;  // Translate right
        rev::shader::SetMat4(shader, u_model_loc, torus_model);
        rev::shader::SetVec3(shader, u_color_loc, 0.3f, 0.3f, 1.0f);
        rev::mesh::Render(torus);
        
        rev::platform::SwapBuffers(window);
    }
    
    // Cleanup
    rev::mesh::DestroyMesh(cube);
    rev::mesh::DestroyMesh(sphere);
    rev::mesh::DestroyMesh(torus);
    rev::shader::DestroyProgram(shader);
    rev::platform::DestroyIntroWindow(window);
    
    return 0;
}
