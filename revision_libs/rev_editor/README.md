# rev_editor - ImGui-based Editor

Editor UI framework using Dear ImGui for scene authoring and parameter editing.

## Dependencies

This library requires **Dear ImGui** to function. To set it up:

### 1. Download Dear ImGui

Download from: https://github.com/ocornut/imgui

Latest tested version: v1.90.4 (or newer)

### 2. Extract Files

Create the directory structure:
```
revision_libs/rev_editor/third_party/imgui/
```

Copy these files from the ImGui download:
```
imgui/
├── imgui.h
├── imgui.cpp
├── imgui_draw.cpp
├── imgui_tables.cpp
├── imgui_widgets.cpp
├── imgui_internal.h
├── imstb_rectpack.h
├── imstb_textedit.h
├── imstb_truetype.h
└── backends/
    ├── imgui_impl_win32.h
    ├── imgui_impl_win32.cpp
    ├── imgui_impl_opengl3.h
    └── imgui_impl_opengl3.cpp
```

### 3. Update CMakeLists.txt

Uncomment the ImGui library section in `revision_libs/rev_editor/CMakeLists.txt`

### 4. Build

Once ImGui is added, the full editor will compile.

## Current Status

**PARTIAL IMPLEMENTATION**: The library contains editor infrastructure but requires ImGui to be fully functional. The backend integration code is ready.

## Features (when ImGui is added)

- **EditorContext**: Main editor state management
- **Project management**: Load/save JSON projects
- **Timeline editor**: Visual scene sequencing
- **Curve editor**: Animation curve editing
- **Shader modal**: Parameter editing UI
- **Preview viewport**: Real-time intro preview

## Usage Example

```cpp
#include "rev_editor.h"

// Create editor
auto* window = rev::platform::CreateIntroWindow(config);
auto* editor = rev::editor::CreateEditor(window);

// Load project
rev::editor::LoadProject(editor, "project.json");

// Frame loop
while (!window->should_close) {
    rev::platform::PollEvents(window);
    
    rev::editor::BeginFrame(editor);
    rev::editor::RenderUI(editor);
    rev::editor::EndFrame(editor);
    
    rev::platform::SwapBuffers(window);
}

// Cleanup
rev::editor::SaveProject(editor, "project.json");
rev::editor::DestroyEditor(editor);
```

## Size Target

~150-250 KB executable (including Dear ImGui)
