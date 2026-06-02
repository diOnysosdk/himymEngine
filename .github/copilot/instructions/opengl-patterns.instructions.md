---
applyTo:
  - "revision_libs/rev_platform/**"
  - "revision_libs/rev_shader/**"
  - "revision_libs/rev_mesh/**"
  - "examples/**/*.cpp"
---

# OpenGL Usage Patterns

## OpenGL Version

**Target**: OpenGL 3.3 Core Profile
- **Core functions**: From `<gl/gl.h>` (OpenGL 1.1)
- **Extensions**: Loaded via `wglGetProcAddress`

## Function Loading

### Core OpenGL 1.1 Functions

```cpp
#include <gl/gl.h>

// No loading needed - directly callable
glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
glViewport(0, 0, width, height);
glEnable(GL_DEPTH_TEST);
glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, 0);
```

### OpenGL 3.3 Extensions

```cpp
#include <windows.h>
#include <gl/gl.h>

// Function pointer types
typedef void (APIENTRY *GLFUNC_glGenVertexArrays)(GLsizei, GLuint*);
typedef void (APIENTRY *GLFUNC_glBindVertexArray)(GLuint);
// ... etc

// Load extension
GLFUNC_glGenVertexArrays glGenVertexArrays = nullptr;
glGenVertexArrays = (GLFUNC_glGenVertexArrays)wglGetProcAddress("glGenVertexArrays");

if (!glGenVertexArrays) {
    // Extension not available
    return false;
}
```

### Batch Loading Pattern

```cpp
bool LoadGLFunctions()
{
    #define LOAD_GL_FUNC(name, type) \
        name = (type)wglGetProcAddress(#name); \
        if (!name) return false;
    
    // Vertex Array Objects
    LOAD_GL_FUNC(glGenVertexArrays, GLFUNC_glGenVertexArrays);
    LOAD_GL_FUNC(glBindVertexArray, GLFUNC_glBindVertexArray);
    LOAD_GL_FUNC(glDeleteVertexArrays, GLFUNC_glDeleteVertexArrays);
    
    // Buffers
    LOAD_GL_FUNC(glGenBuffers, GLFUNC_glGenBuffers);
    LOAD_GL_FUNC(glBindBuffer, GLFUNC_glBindBuffer);
    LOAD_GL_FUNC(glBufferData, GLFUNC_glBufferData);
    LOAD_GL_FUNC(glDeleteBuffers, GLFUNC_glDeleteBuffers);
    
    // Vertex Attributes
    LOAD_GL_FUNC(glEnableVertexAttribArray, GLFUNC_glEnableVertexAttribArray);
    LOAD_GL_FUNC(glVertexAttribPointer, GLFUNC_glVertexAttribPointer);
    
    // Shaders
    LOAD_GL_FUNC(glCreateShader, GLFUNC_glCreateShader);
    LOAD_GL_FUNC(glShaderSource, GLFUNC_glShaderSource);
    LOAD_GL_FUNC(glCompileShader, GLFUNC_glCompileShader);
    LOAD_GL_FUNC(glGetShaderiv, GLFUNC_glGetShaderiv);
    LOAD_GL_FUNC(glGetShaderInfoLog, GLFUNC_glGetShaderInfoLog);
    LOAD_GL_FUNC(glDeleteShader, GLFUNC_glDeleteShader);
    
    // Programs
    LOAD_GL_FUNC(glCreateProgram, GLFUNC_glCreateProgram);
    LOAD_GL_FUNC(glAttachShader, GLFUNC_glAttachShader);
    LOAD_GL_FUNC(glLinkProgram, GLFUNC_glLinkProgram);
    LOAD_GL_FUNC(glGetProgramiv, GLFUNC_glGetProgramiv);
    LOAD_GL_FUNC(glGetProgramInfoLog, GLFUNC_glGetProgramInfoLog);
    LOAD_GL_FUNC(glUseProgram, GLFUNC_glUseProgram);
    LOAD_GL_FUNC(glDeleteProgram, GLFUNC_glDeleteProgram);
    
    // Uniforms
    LOAD_GL_FUNC(glGetUniformLocation, GLFUNC_glGetUniformLocation);
    LOAD_GL_FUNC(glUniform1f, GLFUNC_glUniform1f);
    LOAD_GL_FUNC(glUniform2f, GLFUNC_glUniform2f);
    LOAD_GL_FUNC(glUniform3f, GLFUNC_glUniform3f);
    LOAD_GL_FUNC(glUniform4f, GLFUNC_glUniform4f);
    LOAD_GL_FUNC(glUniformMatrix4fv, GLFUNC_glUniformMatrix4fv);
    
    #undef LOAD_GL_FUNC
    return true;
}
```

## Rendering Pipeline

### Layered Mesh + Sprite State Rules (HiMYM)

- In mixed 3D/2D frames, always rebind the fullscreen quad VAO before sprite/text fullscreen draws.
- Clear depth each frame with depth writes enabled:

```cpp
glDepthMask(GL_TRUE);
glClear(GL_DEPTH_BUFFER_BIT);
```

- 2D overlays should disable depth test and depth writes, but mesh draws must restore both:

```cpp
// 2D overlay phase
glDisable(GL_DEPTH_TEST);
glDepthMask(GL_FALSE);

// Back to mesh phase
glEnable(GL_DEPTH_TEST);
glDepthMask(GL_TRUE);
```

- Leave depth writes enabled at end of frame/layer pass so next-frame depth clear remains effective.

### 1. VAO/VBO/IBO Setup

```cpp
// Create VAO
GLuint vao;
glGenVertexArrays(1, &vao);
glBindVertexArray(vao);

// Create VBO (vertex buffer)
GLuint vbo;
glGenBuffers(1, &vbo);
glBindBuffer(GL_ARRAY_BUFFER, vbo);
glBufferData(GL_ARRAY_BUFFER, 
             vertex_count * sizeof(Vertex), 
             vertices, 
             GL_STATIC_DRAW);

// Create IBO (index buffer)
GLuint ibo;
glGenBuffers(1, &ibo);
glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
glBufferData(GL_ELEMENT_ARRAY_BUFFER, 
             index_count * sizeof(uint32_t), 
             indices, 
             GL_STATIC_DRAW);

// Configure vertex attributes
glEnableVertexAttribArray(0);  // Position
glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 
                       sizeof(Vertex), (void*)offsetof(Vertex, pos));

glEnableVertexAttribArray(1);  // Normal
glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 
                       sizeof(Vertex), (void*)offsetof(Vertex, normal));

glEnableVertexAttribArray(2);  // UV
glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 
                       sizeof(Vertex), (void*)offsetof(Vertex, uv));

// Unbind
glBindVertexArray(0);
```

### 2. Shader Compilation

```cpp
GLuint CompileShader(const char* source, GLenum type)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    
    // Check errors
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, 512, nullptr, log);
        // Handle error
        glDeleteShader(shader);
        return 0;
    }
    
    return shader;
}

GLuint CreateProgram(const char* vs_src, const char* fs_src)
{
    GLuint vs = CompileShader(vs_src, GL_VERTEX_SHADER);
    GLuint fs = CompileShader(fs_src, GL_FRAGMENT_SHADER);
    
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }
    
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    
    // Check link errors
    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(program, 512, nullptr, log);
        // Handle error
        glDeleteProgram(program);
        program = 0;
    }
    
    // Cleanup shaders (no longer needed after linking)
    glDeleteShader(vs);
    glDeleteShader(fs);
    
    return program;
}
```

### 3. Rendering

```cpp
// Per-frame
glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

// Use shader program
glUseProgram(shader_program);

// Set uniforms
GLint loc_time = glGetUniformLocation(shader_program, "u_time");
glUniform1f(loc_time, time);

GLint loc_mvp = glGetUniformLocation(shader_program, "u_mvp");
glUniformMatrix4fv(loc_mvp, 1, GL_FALSE, mvp_matrix);

// Bind VAO and draw
glBindVertexArray(vao);
glDrawElements(GL_TRIANGLES, index_count, GL_UNSIGNED_INT, 0);
glBindVertexArray(0);

// Swap buffers
SwapBuffers(hdc);
```

## Common Patterns

### Fullscreen Quad (Raymarching)

```cpp
// Two triangles covering screen
float quad_vertices[] = {
    -1.0f, -1.0f,
     1.0f, -1.0f,
    -1.0f,  1.0f,
     1.0f,  1.0f
};

GLuint vbo;
glGenBuffers(1, &vbo);
glBindBuffer(GL_ARRAY_BUFFER, vbo);
glBufferData(GL_ARRAY_BUFFER, sizeof(quad_vertices), quad_vertices, GL_STATIC_DRAW);

glEnableVertexAttribArray(0);
glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);

// Draw
glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
```

### Matrix Math (Column-Major)

```cpp
// OpenGL uses column-major matrices
void MatrixIdentity(float* m)
{
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

void MatrixPerspective(float* m, float fov, float aspect, float near, float far)
{
    float f = 1.0f / tanf(fov * 0.5f);
    memset(m, 0, 16 * sizeof(float));
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (far + near) / (near - far);
    m[11] = -1.0f;
    m[14] = (2.0f * far * near) / (near - far);
}

void MatrixMultiply(float* result, const float* a, const float* b)
{
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            result[col * 4 + row] = 0.0f;
            for (int k = 0; k < 4; k++) {
                result[col * 4 + row] += a[k * 4 + row] * b[col * 4 + k];
            }
        }
    }
}
```

### Depth Testing

```cpp
// Enable depth test
glEnable(GL_DEPTH_TEST);
glDepthFunc(GL_LESS);  // Pass if incoming depth < stored depth

// Clear depth buffer every frame
glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
```

### Blending (Transparency)

```cpp
// Enable alpha blending
glEnable(GL_BLEND);
glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

// Render transparent objects back-to-front
```

### Face Culling

```cpp
// Enable backface culling (optimize)
glEnable(GL_CULL_FACE);
glCullFace(GL_BACK);
glFrontFace(GL_CCW);  // Counter-clockwise winding
```

## Error Checking

### Debug Build Only

```cpp
#ifdef _DEBUG
void CheckGLError(const char* stmt, const char* fname, int line)
{
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        printf("OpenGL error 0x%x at %s:%d - %s\n", 
               err, fname, line, stmt);
    }
}

#define GL_CHECK(stmt) do { \
        stmt; \
        CheckGLError(#stmt, __FILE__, __LINE__); \
    } while (0)
#else
#define GL_CHECK(stmt) stmt
#endif

// Usage
GL_CHECK(glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, 0));
```

## Performance Tips

### 1. Minimize State Changes

```cpp
// Bad: Many state changes
for (mesh : meshes) {
    glUseProgram(mesh.program);
    glBindVertexArray(mesh.vao);
    glDrawElements(...);
}

// Good: Sort by state
for (program : programs) {
    glUseProgram(program);
    for (mesh : meshes_using_program) {
        glBindVertexArray(mesh.vao);
        glDrawElements(...);
    }
}
```

### 2. Reuse Buffers

```cpp
// Create buffer once
GLuint vbo;
glGenBuffers(1, &vbo);

// Update with glBufferSubData or glMapBuffer
glBindBuffer(GL_ARRAY_BUFFER, vbo);
glBufferSubData(GL_ARRAY_BUFFER, 0, size, new_data);
```

### 3. Use Indexed Rendering

```cpp
// Indexed rendering (fewer vertices)
glDrawElements(GL_TRIANGLES, index_count, GL_UNSIGNED_INT, 0);

// vs. non-indexed (duplicate vertices)
glDrawArrays(GL_TRIANGLES, 0, vertex_count);
```

## Cleanup

```cpp
// Delete in reverse order of creation
glDeleteProgram(program);
glDeleteBuffers(1, &ibo);
glDeleteBuffers(1, &vbo);
glDeleteVertexArrays(1, &vao);
```

## Troubleshooting

### Black Screen
1. Check shader compilation/linking
2. Verify uniform locations
3. Check viewport size
4. Verify clear color
5. Check depth test settings

### Incorrect Rendering
1. Verify vertex attribute layout
2. Check index buffer contents
3. Verify matrix math (column-major)
4. Check face culling winding order
5. Verify uniform values

### Performance Issues
1. Minimize draw calls
2. Batch similar geometry
3. Reduce state changes
4. Use indexed rendering
5. Optimize shaders