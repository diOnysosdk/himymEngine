---
applyTo:
  - "**/*.cpp"
  - "**/*.h"
---

# Coding Standards for HiMYM

## C++17 Standard

- Language: **C++17** (no C++20 features)
- Exceptions: **Disabled** (`/EHsc-` or `-fno-exceptions`)
- RTTI: **Disabled** (`/GR-` or `-fno-rtti`)
- Standard library: **Minimal use** (prefer manual implementations)

## Naming Conventions

### Files
- Headers: `snake_case.h` (e.g., `rev_platform.h`)
- Source: `snake_case.cpp` (e.g., `platform_win32.cpp`)

### Functions
- **PascalCase** for public API: `CreateMesh`, `UploadToGPU`
- **snake_case** for internal: `load_gl_functions`, `create_vao`

### Variables
- **snake_case**: `vertex_count`, `fov_degrees`
- **ALL_CAPS** for macros: `REV_XM_ENABLED`

### Types
- **PascalCase**: `struct Mesh`, `struct Vertex`
- Namespaces: `rev::platform`, `rev::shader`

## Code Style

### Indentation
- **4 spaces** (no tabs)
- Brace style: **Allman** (braces on new line)

```cpp
void Function()
{
    if (condition)
    {
        // code
    }
}
```

### Line Length
- Prefer **80-100 characters** per line
- Break long function calls:

```cpp
rev::mesh::CreateSphere(
    radius,
    segments,
    rings
);
```

### Spacing
- Space after keywords: `if (`, `for (`, `while (`
- Space around operators: `x + y`, `a = b`
- No space in casts: `(float)x`

## Memory Management

### Allocation
- Prefer **stack allocation** when possible
- Use **new/delete** for dynamic memory (no smart pointers in intros)
- **Manual cleanup**: Always pair Create/Destroy functions

```cpp
Mesh* mesh = CreateMesh(100, 300);
// use mesh
DestroyMesh(mesh);
```

### Resource Ownership
- Clear ownership: who creates, who destroys
- Document lifetime in comments
- Avoid circular dependencies

## Error Handling

### Return Values
- Use **nullptr** for failure (pointers)
- Use **false** for failure (booleans)
- Use **0** for failure (handles/IDs)

```cpp
Mesh* CreateMesh(uint32_t vertices, uint32_t indices)
{
    if (vertices == 0 || indices == 0) {
        return nullptr;
    }
    // ...
}
```

### Error Checking
- Check return values: `if (!mesh) { /* error */ }`
- OpenGL errors: Use `glGetError()` in debug builds
- Graceful degradation when possible

## Platform Abstraction

### Windows-Specific Code
```cpp
#ifdef _WIN32
    // Windows implementation
#else
    #error "Unsupported platform"
#endif
```

### OpenGL Loading
- Core 1.1 functions: `#include <gl/gl.h>`
- Extensions: `wglGetProcAddress`

```cpp
// Core OpenGL (no loading needed)
glClear(GL_COLOR_BUFFER_BIT);

// Extension (must load)
GLFUNC_glGenVertexArrays glGenVertexArrays = 
    (GLFUNC_glGenVertexArrays)wglGetProcAddress("glGenVertexArrays");
```

## Size Optimization Patterns

### Avoid String Literals
```cpp
// Bad (large string table)
const char* error = "Failed to load";

// Good (no strings in release)
#ifdef _DEBUG
    printf("Error\n");
#endif
```

### Inline Small Functions
```cpp
inline float Clamp(float x, float min, float max)
{
    return x < min ? min : (x > max ? max : x);
}
```

### Use Fixed-Size Types
```cpp
#include <stdint.h>
uint32_t vertex_count;  // Explicit size
float position[3];      // Fixed array
```

## Documentation

### Header Comments
```cpp
/**
 * @file rev_mesh.h
 * @brief 3D mesh rendering library
 */
```

### Function Comments
```cpp
/**
 * @brief Create mesh with specified capacity
 * @param max_vertices Maximum number of vertices
 * @param max_indices Maximum number of indices  
 * @return Mesh pointer or nullptr on failure
 */
Mesh* CreateMesh(uint32_t max_vertices, uint32_t max_indices);
```

### Implementation Comments
```cpp
// Load OpenGL function pointers
if (!LoadGLFunctions()) {
    return nullptr;  // OpenGL 3.3+ not available
}
```

## Common Patterns

### Library Initialization
```cpp
namespace rev::mesh
{
    static bool initialized = false;
    
    bool Init()
    {
        if (initialized) return true;
        
        if (!LoadGLFunctions()) {
            return false;
        }
        
        initialized = true;
        return true;
    }
}
```

### Opaque Handles
```cpp
// Header
struct Mesh;  // Forward declaration
Mesh* CreateMesh(...);

// Implementation
struct Mesh
{
    uint32_t vbo, ibo, vao;
    // ... internal details hidden from API users
};
```

### Math Utilities
```cpp
inline float ToRadians(float degrees)
{
    return degrees * 0.01745329251f;  // PI/180
}

inline float ToDegrees(float radians)
{
    return radians * 57.2957795131f;  // 180/PI
}
```

## Anti-Patterns

### DON'T: Use STL containers in intros
```cpp
// Bad (large binary size)
std::vector<Vertex> vertices;

// Good (minimal size)
Vertex* vertices = new Vertex[max_vertices];
```

### DON'T: Use iostream
```cpp
// Bad
#include <iostream>
std::cout << "Error" << std::endl;

// Good
#ifdef _DEBUG
    printf("Error\n");
#endif
```

### DON'T: Allocate in tight loops
```cpp
// Bad
for (int i = 0; i < 1000; i++) {
    float* temp = new float[10];
    // ...
    delete[] temp;
}

// Good
float temp[10];
for (int i = 0; i < 1000; i++) {
    // ... use temp
}
```

## Testing

### Build All Configurations
- Debug: Full error checking
- Release: Size optimization, no checks

```bash
cmake --build build --config Debug
cmake --build build --config Release
```

### Size Check
```bash
Get-ChildItem build\bin\Release\*.exe | 
    Select-Object Name, @{Name="Size";Expression={$_.Length}}
```

## Code Review Checklist

- [ ] No exceptions thrown
- [ ] No RTTI usage
- [ ] Minimal STL usage
- [ ] Resources paired (Create/Destroy)
- [ ] Null checks on pointers
- [ ] OpenGL errors checked (debug)
- [ ] Fixed-size types used
- [ ] Comments on non-obvious code
- [ ] Release build tested
- [ ] Size impact verified