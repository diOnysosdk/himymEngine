#include "rev_platform.h"
#include "rev_shader.h"
#include "rev_mesh.h"
#include <windows.h>
#include <cmath>
#include <cstring>
#include <cstdio>

// OpenGL function declarations
extern "C" {
    void glEnable(unsigned int cap);
    void glClear(unsigned int mask);
    void glClearColor(float red, float green, float blue, float alpha);
    void glViewport(int x, int y, int width, int height);
    void glDepthFunc(unsigned int func);
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

void MatrixTranslate(float* m, float x, float y, float z) {
    MatrixIdentity(m);
    m[12] = x;
    m[13] = y;
    m[14] = z;
}

void MatrixMultiply(float* out, const float* a, const float* b) {
    float temp[16];
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            temp[row * 4 + col] = 
                a[row * 4 + 0] * b[0 * 4 + col] +
                a[row * 4 + 1] * b[1 * 4 + col] +
                a[row * 4 + 2] * b[2 * 4 + col] +
                a[row * 4 + 3] * b[3 * 4 + col];
        }
    }
    memcpy(out, temp, sizeof(temp));
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
    
    // Set viewport
    glViewport(0, 0, config.width, config.height);
    
    // Compile shader
    rev::shader::Program* shader = rev::shader::CompileFromSource(vertex_shader, fragment_shader);
    if (!shader) {
        MessageBox(nullptr, "Failed to compile shader", "Error", MB_OK | MB_ICONERROR);
        rev::platform::DestroyIntroWindow(window);
        return -1;
    }
    
    // Create meshes
    rev::mesh::Mesh* cube = rev::mesh::CreateCube(2.0f);
    rev::mesh::Mesh* sphere = rev::mesh::CreateSphere(1.5f, 32);
    rev::mesh::Mesh* torus = rev::mesh::CreateTorus(1.5f, 0.5f, 32, 16);
    
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
    MatrixLookAt(view, 0.0f, 0.0f, 15.0f, 0.0f, 0.0f, 0.0f);  // Camera directly in front, looking at origin
    
    double start_time = rev::platform::GetTime();
    
    // Enable depth testing
    glEnable(0x0B71);  // GL_DEPTH_TEST
    glDepthFunc(0x0203);  // GL_LEQUAL
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
        rev::shader::SetVec3(shader, u_light_pos_loc, 10.0f, 10.0f, 10.0f);
        rev::shader::SetVec3(shader, u_view_pos_loc, 0.0f, 0.0f, 15.0f);
        
        // Render cube (left)
        float cube_rotate[16], cube_translate[16], cube_model[16];
        MatrixRotateY(cube_rotate, time * 0.5f);
        MatrixTranslate(cube_translate, -5.0f, 0.0f, 0.0f);  // Spread more
        MatrixMultiply(cube_model, cube_translate, cube_rotate);
        rev::shader::SetMat4(shader, u_model_loc, cube_model);
        rev::shader::SetVec3(shader, u_color_loc, 1.0f, 0.3f, 0.3f);
        rev::mesh::Render(cube);
        
        // Render sphere (center)
        float sphere_rotate[16], sphere_translate[16], sphere_model[16];
        MatrixRotateY(sphere_rotate, time * 0.7f);
        MatrixTranslate(sphere_translate, 0.0f, 0.0f, 0.0f);  // Center at origin
        MatrixMultiply(sphere_model, sphere_translate, sphere_rotate);
        rev::shader::SetMat4(shader, u_model_loc, sphere_model);
        rev::shader::SetVec3(shader, u_color_loc, 0.3f, 1.0f, 0.3f);
        rev::mesh::Render(sphere);
        
        // Render torus (right)
        float torus_rotate[16], torus_translate[16], torus_model[16];
        MatrixRotateY(torus_rotate, time * 0.9f);
        MatrixTranslate(torus_translate, 5.0f, 0.0f, 0.0f);  // Spread more
        MatrixMultiply(torus_model, torus_translate, torus_rotate);
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
