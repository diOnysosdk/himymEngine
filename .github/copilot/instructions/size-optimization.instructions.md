---
applyTo:
  - "examples/minimal_intro/**"
  - "examples/animated_intro/**"
  - "examples/demo_intro/**"
---

# Size Optimization Techniques

## Target Sizes

- **4KB Intro**: < 4096 bytes executable
- **16KB Intro**: < 16384 bytes executable (common)
- **32KB Intro**: < 32768 bytes executable
- **64KB Intro**: < 65536 bytes executable ("full" intro)

**Note**: Sizes are measured **after compression** (kkrunchy, UPX, etc.)

## Compiler Optimization

### MSVC Flags
```cmake
# Size optimization
target_compile_options(${TARGET} PRIVATE
    /O1          # Optimize for size
    /Os          # Favor small code  
    /Oy          # Omit frame pointers
    /GS-         # Disable buffer security check
    /GR-         # Disable RTTI
    /EHsc-       # Disable exceptions
)

target_link_options(${TARGET} PRIVATE
    /GL          # Whole program optimization
    /LTCG        # Link-time code generation
    /OPT:REF     # Remove unreferenced functions
    /OPT:ICF     # COMDAT folding
    /MERGE:.rdata=.text   # Merge sections
    /ENTRY:mainCRTStartup # Custom entry point
    /SUBSYSTEM:WINDOWS    # No console
)
```

### GCC/Clang Flags
```cmake
target_compile_options(${TARGET} PRIVATE
    -Os                              # Optimize for size
    -ffunction-sections              # Each function in own section
    -fdata-sections                  # Each data in own section
    -fno-exceptions                  # No exception handling
    -fno-rtti                        # No run-time type info
    -fno-asynchronous-unwind-tables  # No unwind tables
    -ffast-math                      # Fast math (smaller)
)

target_link_options(${TARGET} PRIVATE
    -Wl,--gc-sections    # Garbage collect unused sections
    -flto                # Link-time optimization
    -s                   # Strip symbols
)
```

## Code Size Reduction

### 1. Minimize CRT Usage

```cpp
// Custom entry point (no CRT initialization)
#ifdef _WIN32
int WINAPI WinMainCRTStartup()
{
    // Your code here
    ExitProcess(0);
    return 0;
}
#endif
```

### 2. Avoid Standard Library

```cpp
// Bad (large binary)
#include <vector>
#include <string>
std::vector<float> data;

// Good (minimal size)
float data[100];
int data_count = 0;
```

### 3. Inline Small Functions

```cpp
// Inline to avoid function call overhead
inline float Lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}
```

### 4. Use Direct Win32 API

```cpp
// Minimal window creation
HWND hwnd = CreateWindowExA(
    0, "STATIC", 0, WS_POPUP | WS_VISIBLE,
    0, 0, 1280, 720,
    0, 0, 0, 0
);
HDC hdc = GetDC(hwnd);
```

### 5. Manual OpenGL Loading

```cpp
// Only load functions you actually use
GLFUNC_glCreateShader glCreateShader = 
    (GLFUNC_glCreateShader)wglGetProcAddress("glCreateShader");
```

### 6. Shader String Optimization

```cpp
// Bad (large string literals)
const char* vs = 
    "#version 330 core\n"
    "layout(location = 0) in vec3 aPos;\n"
    "void main() {\n"
    "    gl_Position = vec4(aPos, 1.0);\n"
    "}\n";

// Good (minimal whitespace, shorter names)
const char* vs = 
    "#version 330\n"
    "layout(location=0)in vec3 a;"
    "void main(){gl_Position=vec4(a,1.);}";
```

### 7. Hardcode Constants

```cpp
// Bad (data section)
float fov = 45.0f;
float aspect = 1.777f;

// Good (compile-time constants)
#define FOV 45.0f
#define ASPECT 1.777f
```

### 8. Share Code Between Shaders

```cpp
// Shared utility functions
const char* shader_utils = 
    "float hash(float n){return fract(sin(n)*43758.5);}";

// Build vertex shader
char vs[512];
sprintf(vs, "#version 330\n%s\nvoid main(){...}", shader_utils);
```

## Data Size Reduction

### 1. Compress Static Data

```cpp
// Embed compressed XM music
extern const unsigned char music_xm_compressed[];
extern const size_t music_xm_compressed_len;

// Decompress at runtime (if compressor supports it)
unsigned char* music_xm = Decompress(music_xm_compressed, ...);
```

### 2. Procedural Generation

```cpp
// Generate geometry at runtime instead of storing vertices
Mesh* sphere = CreateSphere(1.0f, 16, 8);  // Only stores algorithm
```

### 3. Use Smaller Data Types

```cpp
// Bad (4 bytes per value)
uint32_t indices[300];

// Good (2 bytes per value, if < 65536 vertices)
uint16_t indices[300];
```

### 4. Packed Structures

```cpp
// Minimize padding
struct Vertex
{
    float pos[3];     // 12 bytes
    float normal[3];  // 12 bytes
    float uv[2];      // 8 bytes
} __attribute__((packed));  // GCC/Clang
// or
#pragma pack(push, 1)  // MSVC
```

## Linker Optimization

### 1. Merge Sections

```cmake
/MERGE:.rdata=.text  # Merge read-only data into code section
/MERGE:.data=.text   # Merge data into code section (careful!)
```

### 2. Remove Debug Info

```cmake
/DEBUG:NONE          # No debug information
```

### 3. Custom Subsystem

```cmake
/SUBSYSTEM:WINDOWS   # No console window
/ENTRY:WinMainCRTStartup  # Custom entry point
```

### 4. Strip Relocations

```cmake
/FIXED               # No relocation table
/BASE:0x400000       # Fixed base address
```

## Runtime Size Optimization

### 1. Lazy Initialization

```cpp
// Only initialize OpenGL functions when first used
static bool gl_loaded = false;
if (!gl_loaded) {
    LoadGLFunctions();
    gl_loaded = true;
}
```

### 2. Reuse Buffers

```cpp
// Single shared buffer for multiple purposes
static char shared_buffer[4096];

// Use for shader compilation
char* shader_src = shared_buffer;
// Later, reuse for other temporary data
```

### 3. Stack Allocation

```cpp
// Prefer stack over heap (no malloc overhead)
float matrix[16];
Vertex vertices[100];
```

## Compression

### kkrunchy (Demoscene Standard)

```bash
kkrunchy.exe \
    --best \
    --out=intro_packed.exe \
    intro.exe
```

**Compression ratio**: 30-50% typically
**Decompression**: Runs at startup (transparent to code)

### UPX (Alternative)

```bash
upx --best --ultra-brute intro.exe
```

**Pros**: Free, open-source
**Cons**: Lower compression ratio than kkrunchy

## Size Analysis

### Check Section Sizes (dumpbin)

```bash
dumpbin /headers intro.exe
```

Look for:
- `.text` (code)
- `.rdata` (read-only data)
- `.data` (writable data)

### Identify Large Functions (link map)

```cmake
/MAP:intro.map       # Generate map file
```

Review map file for large functions to optimize.

### Compare Sizes

```bash
# Before optimization
Get-Item intro.exe | Select-Object Length

# After optimization
Get-Item intro_opt.exe | Select-Object Length

# Calculate savings
$before = (Get-Item intro.exe).Length
$after = (Get-Item intro_opt.exe).Length
$savings = (1 - $after/$before) * 100
Write-Host "Reduced by ${savings}%"
```

## Size Budget Template

```
16KB Intro Budget
─────────────────
Code:           8 KB (50%)
- OpenGL init:  2 KB
- Shader code:  3 KB
- Rendering:    2 KB
- Math utils:   1 KB

Data:           4 KB (25%)
- Shaders:      2 KB
- Music:        2 KB

Runtime:        2 KB (12.5%)
- Stack:        1 KB
- Heap:         1 KB

Compression:    2 KB (12.5%)
- Overhead:     2 KB

Total:         16 KB
```

## Extreme Optimization (4KB)

### Techniques

1. **Single file**: No libraries, everything inlined
2. **Tiny shaders**: Minimal GLSL, use tricks
3. **No music**: Or very short/simple
4. **Fixed resolution**: No scaling
5. **Assembly**: Critical sections in ASM
6. **Import by ordinal**: Smaller import table

### Example 4KB Intro Structure

```cpp
// Minimal Win32 + OpenGL intro
#include <windows.h>
#include <gl/gl.h>

int WinMainCRTStartup()
{
    // Create window (no WNDCLASS registration)
    HWND hwnd = CreateWindowExA(0, "STATIC", 0, WS_POPUP|WS_VISIBLE, 
                                  0, 0, 1280, 720, 0, 0, 0, 0);
    HDC hdc = GetDC(hwnd);
    
    // Minimal pixel format
    SetPixelFormat(hdc, 1, &(PIXELFORMATDESCRIPTOR){sizeof(PIXELFORMATDESCRIPTOR), 1});
    wglMakeCurrent(hdc, wglCreateContext(hdc));
    
    // Minimal shader
    const char* vs = "void main(){gl_Position=vec4(0);}";
    // ... compile shader ...
    
    // Main loop
    MSG msg;
    while (GetMessage(&msg, 0, 0, 0)) {
        glClear(16384);  // GL_COLOR_BUFFER_BIT
        SwapBuffers(hdc);
    }
    
    ExitProcess(0);
}
```

## Verification Checklist

- [ ] Release build configured
- [ ] Size optimization flags enabled
- [ ] No debug symbols
- [ ] STL usage minimized
- [ ] Strings minimized
- [ ] Unused code removed
- [ ] Sections merged
- [ ] Compressed with kkrunchy/UPX
- [ ] Size within target (4KB/16KB/32KB/64KB)
- [ ] Executable runs correctly