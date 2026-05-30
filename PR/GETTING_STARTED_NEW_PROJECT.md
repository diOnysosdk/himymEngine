# Getting Started: New C++ Project from PR Documentation

**How to use these docs to build a fresh intro framework**

---

## Step 1: Create New Project Folder

```powershell
# Create your new project
mkdir E:\code\cpp\my_intro_framework
cd E:\code\cpp\my_intro_framework

# Initialize git
git init
```

---

## Step 2: Copy PR Documentation

```powershell
# Copy the PR folder from HowIMetYourMod
Copy-Item -Recurse E:\code\cpp\mono\HowIMetYourMod\PR .\docs

# Your structure:
# my_intro_framework/
#   docs/                  ← All PR documentation
#   revision_libs/         ← To be created (libraries)
#   examples/              ← To be created (test apps)
#   CMakeLists.txt         ← To be created
```

---

## Step 3: Configure Copilot to Use the Docs

### Option A: Use .github/copilot-instructions.md

Create `.github/copilot-instructions.md`:

```markdown
# Project Context

This is a fresh C++ intro framework following the architecture from docs/FROM_SCRATCH_V2.md.

## Key References
- Implementation plan: docs/ROADMAP.md
- Library APIs: docs/LIBRARY_DESIGN.md
- Design rationale: docs/REFACTORING_NOTES.md
- Shader authoring: docs/guides/SHADER_GUIDE.md

## Current Phase
We are implementing **Phase 1: Foundation Libraries** from ROADMAP.md.

Currently working on: **Milestone 1.1 (rev_platform)**

## Guidelines
- Follow C++17 standards
- Keep libraries modular and independent
- No external dependencies except Win32 API and OpenGL
- Test each milestone before proceeding
- Target size: <30 KB compressed per intro

## Build System
- CMake 3.20+
- Visual Studio 2022 or Clang
- Windows-only (no cross-platform abstractions)
```

### Option B: Ask Copilot Directly

Open Copilot Chat and type:

```
@workspace I'm starting a new C++ intro framework. 

Read these docs to understand the architecture:
- docs/FROM_SCRATCH_V2.md (overall design)
- docs/LIBRARY_DESIGN.md (API specs)
- docs/ROADMAP.md (implementation plan)

We're implementing Milestone 1.1 (rev_platform) from ROADMAP.md.
Create the initial library structure and implement Win32 window creation.
```

---

## Step 4: Follow the ROADMAP

Open `docs/ROADMAP.md` and start with **Phase 1: Foundation Libraries**.

### Milestone 1.1: rev_platform

**Tell Copilot**:
```
Create the rev_platform library structure:

1. Create folders:
   - revision_libs/rev_platform/include/
   - revision_libs/rev_platform/src/

2. Implement include/rev_platform.h with these functions:
   - CreateWindow(width, height, fullscreen, title)
   - DestroyWindow(window)
   - PollEvents(window)
   - SwapBuffers(window)
   - GetTime()
   - IsKeyPressed(window, key)
   - LoadGLFunctions()

3. Implement src/platform_win32.cpp with Win32 + WGL setup

Reference: docs/LIBRARY_DESIGN.md section "rev_platform"
```

Copilot will have the LIBRARY_DESIGN.md context and can implement the API exactly as specified.

---

## Step 5: Iterate Through Milestones

After each milestone:

1. **Build**: `cmake --build build --config Release`
2. **Test**: Run the validation test from ROADMAP.md
3. **Verify**: Check output matches expected behavior
4. **Next**: Tell Copilot to proceed to next milestone

Example progression:
```
✅ Milestone 1.1 complete → "Now implement Milestone 1.2 (rev_shader)"
✅ Milestone 1.2 complete → "Now implement Milestone 1.3 (rev_xm)"
✅ Milestone 1.3 complete → "Now implement Milestone 1.4 (integration test)"
```

---

## Step 6: Keep Copilot Updated

As you progress, update `.github/copilot-instructions.md`:

```markdown
## Current Phase
Phase 2: Animation Libraries (Week 2)

Currently working on: **Milestone 2.1 (rev_curve)**

## Completed Milestones
- ✅ Milestone 1.1: rev_platform (Win32 + WGL)
- ✅ Milestone 1.2: rev_shader (GLSL compilation)
- ✅ Milestone 1.3: rev_xm (XM playback)
- ✅ Milestone 1.4: Integration test (minimal intro)
```

---

## Common Copilot Prompts

### Starting a Library
```
Create the rev_<name> library following the API in docs/LIBRARY_DESIGN.md.
Include folder structure, header file, implementation stubs, and CMakeLists.txt.
```

### Implementing a Feature
```
Implement the <function_name> function in rev_<library>.
Reference: docs/LIBRARY_DESIGN.md section "<library>".
Use Win32 API for platform code, keep it simple and size-optimized.
```

### Testing
```
Create a test app for rev_<library> that validates <specific_feature>.
Follow the validation criteria from docs/ROADMAP.md Milestone X.Y.
```

### Debugging
```
The test app crashes at <location>. Debug this:
<paste error or behavior>

Check against the API contract in docs/LIBRARY_DESIGN.md.
```

### Moving to Next Phase
```
Phase 1 complete! Summary:
- rev_platform: <size>
- rev_shader: <size>
- rev_xm: <size>
- Integration test: ✅ passes

Now start Phase 2: Animation Libraries.
Implement Milestone 2.1 (rev_curve) from docs/ROADMAP.md.
```

---

## Pro Tips

### 1. Reference Docs Explicitly
Instead of: "Create a window"
Better: "Create a window using the API from docs/LIBRARY_DESIGN.md rev_platform section"

### 2. One Milestone at a Time
Don't jump ahead. Complete validation test before proceeding.

### 3. Keep Size in Check
After each library, check size:
```powershell
ls -l revision_libs/*/lib/*.lib
```
If it's too big, ask Copilot to optimize.

### 4. Use the Shader Guide
When implementing shader system:
```
Read docs/guides/SHADER_GUIDE.md for shader pipeline architecture.
Implement scene dispatch exactly as described.
```

### 5. Commit After Each Milestone
```powershell
git add .
git commit -m "feat(rev_platform): implement Win32 window + WGL (Milestone 1.1)"
```

---

## Example First Session

```powershell
# Create project
mkdir E:\code\cpp\my_intro_framework
cd E:\code\cpp\my_intro_framework
git init

# Copy docs
Copy-Item -Recurse E:\code\cpp\mono\HowIMetYourMod\PR .\docs

# Open in VS Code
code .
```

In Copilot Chat:
```
I'm starting a new C++ intro framework based on docs/FROM_SCRATCH_V2.md.

Phase: Week 1, Milestone 1.1 (rev_platform)
Goal: Win32 window + WGL OpenGL 3.3 context + timing

Create:
1. revision_libs/rev_platform/include/rev_platform.h
2. revision_libs/rev_platform/src/platform_win32.cpp
3. revision_libs/rev_platform/src/platform_timing.cpp
4. revision_libs/rev_platform/src/platform_gl_loader.cpp
5. revision_libs/rev_platform/CMakeLists.txt

Reference API: docs/LIBRARY_DESIGN.md section "rev_platform"
```

Copilot will read the docs and generate the code following the exact API.

---

## Troubleshooting

### "Copilot doesn't seem to reference the docs"
Make sure docs are in workspace and explicitly mention the file path:
```
Read docs/ROADMAP.md and tell me what Milestone 1.1 requires.
```

### "Generated code doesn't match the API"
Be more specific:
```
The CreateWindow function signature is wrong.
Check docs/LIBRARY_DESIGN.md line 50 for correct signature:
Window* CreateWindow(int width, int height, bool fullscreen, const char* title);
```

### "Code is too complex"
Remind Copilot of constraints:
```
This is too complex. From docs/FROM_SCRATCH_V2.md:
- Keep it simple (no abstractions)
- Target <30 KB compressed
- Windows-only (no cross-platform)

Simplify this code.
```

---

## Success Checklist

After 6 weeks, you should have:

- ✅ 7 modular C++ libraries
- ✅ Minimal intro example (<100 KB)
- ✅ C++ editor with ImGui (<300 KB)
- ✅ All validation tests passing
- ✅ Documentation updated with your learnings

---

## Next Steps

1. **Create project folder** (Step 1)
2. **Copy PR docs** (Step 2)
3. **Configure Copilot** (Step 3)
4. **Open docs/ROADMAP.md** and start Milestone 1.1
5. **Iterate** through phases

Good luck! 🚀

---

**Last Updated**: May 30, 2026  
**Version**: 1.0  
**Status**: Workflow guide for using PR documentation with Copilot
