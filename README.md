# HiMYM - Modular C++ Intro Framework

A lightweight, modular framework for creating demoscene intros and small demos. Built with size optimization and simplicity in mind.

## Project Status

**Phase 1: Foundation Libraries** ✅ COMPLETE

- ✅ `rev_platform` - Win32 window + WGL OpenGL context + timing
- ✅ `rev_shader` - GLSL shader compilation and uniform management  
- ✅ `rev_xm` - XM music playback (stub, needs libxm)
- ✅ Minimal intro integration test

**Next: Phase 2 - Animation Libraries**

## Quick Start

### Prerequisites

- Visual Studio 2022 (or Clang)
- CMake 3.20+
- Windows 10/11

### Build

```powershell
# Configure project
cmake -B build -G "Visual Studio 17 2022"

# Build
cmake --build build --config Release

# Run test
.\build\bin\Release\minimal_intro.exe
```

### Expected Output

The minimal intro test creates a window with an animated pulsing gradient. It runs for 10 seconds or exits on ESC.

## Project Structure

```
himym/
├── PR/                          # All project documentation
│   ├── ROADMAP.md              # Implementation plan
│   ├── LIBRARY_DESIGN.md       # API specifications
│   ├── FROM_SCRATCH_V2.md      # Architecture overview
│   └── guides/                 # Shader, editor, controls guides
├── revision_libs/              # Core libraries
│   ├── rev_platform/           # Platform abstraction (Win32/WGL)
│   ├── rev_shader/             # Shader compilation
│   └── rev_xm/                 # XM music player (needs libxm)
├── examples/
│   └── minimal_intro/          # Phase 1 integration test
└── CMakeLists.txt              # Root build configuration
```

## Library Overview

### rev_platform
Win32 window creation, WGL OpenGL 3.3 context, high-precision timing, keyboard/mouse input.

**Size**: ~15 KB

### rev_shader
GLSL shader compilation, program linking, uniform setters (float, vec2/3/4, mat4, int).

**Size**: ~10 KB

### rev_xm
XM module playback. Currently a stub - requires libxm integration (see `revision_libs/rev_xm/README.md`).

**Size**: ~30 KB (with libxm)

## Documentation

All project documentation is in the `PR/` folder:

- **[ROADMAP.md](PR/ROADMAP.md)** - 6-week implementation plan with milestones
- **[LIBRARY_DESIGN.md](PR/LIBRARY_DESIGN.md)** - Complete API reference for all libraries
- **[FROM_SCRATCH_V2.md](PR/FROM_SCRATCH_V2.md)** - Architecture rationale and design decisions
- **[SHADER_GUIDE.md](PR/guides/SHADER_GUIDE.md)** - Shader authoring workflow
- **[EDITOR_GUIDE.md](PR/guides/EDITOR_GUIDE.md)** - Editor usage (Phase 3+)

## Next Steps

1. **Add libxm** for audio playback
   - Download from https://github.com/Artefact2/libxm
   - Follow instructions in `revision_libs/rev_xm/README.md`

2. **Phase 2: Animation Libraries** (see ROADMAP.md)
   - `rev_curve` - Time-value curves with easing
   - `rev_sequence` - Timeline and cue system

3. **Phase 3: Editor** (see ROADMAP.md)
   - ImGui integration
   - Timeline editor
   - Shader parameter editing
   - Project export to runtime format

## Target Goals

- **Intro size**: <30 KB compressed (kkrunchy/Crinkler)
- **Editor size**: <300 KB
- **Build time**: <5 seconds
- **No external runtime dependencies** (except Win32 + OpenGL)

## Development

### Coding Guidelines

- C++17 standard
- No exceptions or RTTI (size optimization)
- Minimal STL usage in runtime code
- Each library is independent and reusable
- Follow APIs exactly as specified in LIBRARY_DESIGN.md

### Build Flags

Release builds use aggressive size optimization:
- MSVC: `/O1 /GS- /GL /MT` + `/LTCG /OPT:REF /OPT:ICF`
- GCC/Clang: `-Os -fno-exceptions -fno-rtti`

## Contributing

This is a personal demoscene project following a specific design philosophy. The architecture and APIs are deliberately minimalist to achieve extreme size optimization.

If you find this useful, feel free to fork and adapt for your own productions!

## License

See individual library files for license information. Most code is intended to be public domain or MIT licensed for demoscene use.

---

**Created**: May 30, 2026  
**Phase**: 1 of 6  
**Status**: Foundation Complete ✅
