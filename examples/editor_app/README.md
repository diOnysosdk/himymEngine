# HiMYM Editor Application

This is the current native C++17/ImGui authoring application. It supports
project JSON, the scene timeline, cue modals, curves, preview rendering,
`cues.txt` export/import, asset packing, standalone builds, and screen-saver
output.

## Build and run

```powershell
cmake -S . -B build
cmake --build build --config Release --target editor_app
.\build\bin\Release\editor_app.exe
```

Keep the executable under `build\bin\Release` when using integrated Build
commands. The editor resolves the workspace by walking three directories up
from its executable.

## Packed workflow

`Build > Pack, Build and Run` performs:

1. Save and export the current project.
2. Generate `build/packed_assets.h` with referenced asset bytes and C++ feature flags.
3. Generate `build/packed_features.cmake` with matching optional dependencies.
4. Reconfigure CMake.
5. Build and launch `minimal_intro_packed`.

The editor is fully integrated; no Python, tkinter, or separate ImGui download
is required. See `PR/guides/EDITOR_GUIDE.md` for authoring instructions.
