# Build Instructions

Quick reference for building HiMYM.

## First Time Setup

```powershell
# Clone or navigate to project
cd E:\code\cpp\mono\himym

# Configure CMake (generates Visual Studio solution)
cmake -B build -G "Visual Studio 17 2022"
```

## Building

### Release Build (Optimized for size)
```powershell
cmake --build build --config Release
```

### Debug Build
```powershell
cmake --build build --config Debug
```

## Running Examples

### Minimal Intro
```powershell
.\build\bin\Release\minimal_intro.exe
```

## Clean Build

```powershell
# Remove build directory
Remove-Item -Recurse -Force build

# Reconfigure
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

## Alternative: Visual Studio

You can also open the generated solution:

```powershell
# After running cmake -B build
start build\HiMYM.sln
```

Then build normally in Visual Studio (Ctrl+Shift+B).

## Troubleshooting

### "CMake not found"
Install CMake from: https://cmake.org/download/
Or via chocolatey: `choco install cmake`

### "Cannot find Visual Studio"
Install Visual Studio 2022 with "Desktop development with C++" workload.

Or use Clang:
```powershell
cmake -B build -G "Ninja" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
```

### OpenGL errors at runtime
Make sure your GPU drivers are up to date. The framework requires OpenGL 3.3+.

### Linker errors
Make sure you're building with matching configurations (all Debug or all Release).
Clean and rebuild if you switched configurations.

## Size Check

After release build:

```powershell
# Check executable size
ls build\bin\Release\*.exe | Select-Object Name, Length

# For compressed size (requires kkrunchy or Crinkler)
# kkrunchy minimal_intro.exe --out minimal_intro_packed.exe
# ls minimal_intro_packed.exe
```

Target: <100 KB uncompressed, <30 KB compressed
