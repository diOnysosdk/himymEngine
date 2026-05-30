# Editor Application (Phase 3)

Basic editor application using the rev_editor library.

## Current Status

**INFRASTRUCTURE READY**: The editor framework is implemented but requires Dear ImGui to be fully functional.

### What Works Now
- Window creation and OpenGL context
- Editor context initialization
- Project data structures
- Frame lifecycle

### What Needs ImGui
- Menu bar
- Timeline window
- Curve editor panel
- Shader modal dialog
- All visual UI elements

## Adding ImGui

Follow instructions in [revision_libs/rev_editor/README.md](../../revision_libs/rev_editor/README.md) to:

1. Download Dear ImGui
2. Extract files to `revision_libs/rev_editor/third_party/imgui/`
3. Uncomment ImGui sections in CMakeLists.txt
4. Rebuild

## Building (Current State)

```powershell
# Build (will create empty editor window)
cmake --build build --config Release

# Run
.\build\bin\Release\editor_app.exe
```

Expected: Empty window with dark gray background. No UI yet (needs ImGui).

## Building (After ImGui)

Once ImGui is added, the editor will show:
- **Menu bar**: File, View, Build menus
- **Timeline panel**: Scene cue visualization
- **Curve editor**: Animation curve editing
- **Shader modal**: Parameter controls

## Features (Planned)

- Load/save JSON projects
- Visual timeline editing with drag & drop
- Curve editor with point manipulation
- Shader parameter editing
- Export to runtime format
- Build and run integration
- Live preview viewport

## Size Target

~200-300 KB executable (including ImGui)
