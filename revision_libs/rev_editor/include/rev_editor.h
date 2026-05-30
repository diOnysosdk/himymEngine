#pragma once

#include "rev_platform.h"
#include "rev_sequence.h"
#include "rev_curve.h"
#include <cstddef>

namespace rev {
namespace editor {

// Forward declarations
struct EditorContext;
struct ProjectData;

// Project data structure
struct ProjectData {
    rev::sequence::Timeline timeline;
    rev::curve::Curve* curves;
    int curve_count;
    char project_path[512];
    bool modified;
};

// Editor context (opaque handle)
struct EditorContext {
    void* imgui_context;
    rev::platform::Window* window;
    ProjectData* project;
    bool show_timeline;
    bool show_curve_editor;
    bool show_shader_modal;
    bool show_demo;
    float timeline_zoom;
    float timeline_scroll;
};

// Lifecycle
EditorContext* CreateEditor(rev::platform::Window* window);
void DestroyEditor(EditorContext* editor);

// Project management
bool LoadProject(EditorContext* editor, const char* path);
bool SaveProject(EditorContext* editor, const char* path);
bool NewProject(EditorContext* editor);

// Frame lifecycle
void BeginFrame(EditorContext* editor);
void RenderUI(EditorContext* editor);
void EndFrame(EditorContext* editor);

// UI panels
void RenderMenuBar(EditorContext* editor);
void RenderTimeline(EditorContext* editor);
void RenderCurveEditor(EditorContext* editor);
void RenderShaderModal(EditorContext* editor);

// Build integration
bool ExportProject(EditorContext* editor, const char* output_path);
bool BuildAndRun(EditorContext* editor);

}  // namespace editor
}  // namespace rev
