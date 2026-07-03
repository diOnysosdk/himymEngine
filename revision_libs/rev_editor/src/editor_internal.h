#pragma once

#include "rev_editor.h"
#include "shader_presets.h"

// Internal declarations for editor UI modules
// These functions are split across editor_modals.cpp and editor_panels.cpp

namespace rev {
namespace editor {

// Helper functions (editor_context.cpp)
uint64_t GetFileModificationTime(const char* path);

// UI Panel Functions (editor_panels.cpp)
void RenderTimeline(EditorContext* editor);
void RenderProperties(EditorContext* editor);
void RenderAssetBrowser(EditorContext* editor);
void RenderCurveEditor(EditorContext* editor);

// Modal Dialog Functions (editor_modals.cpp)
void RenderCurveEditorModal(EditorContext* editor);
void RenderShaderModal(EditorContext* editor);
void RenderMusicModal(EditorContext* editor);
void RenderImageModal(EditorContext* editor);
void RenderAnimatedSpriteModal(EditorContext* editor);
void RenderTextModal(EditorContext* editor);
void RenderScrollTextModal(EditorContext* editor);
void RenderMeshModal(EditorContext* editor);

// Curve Management Functions (editor_modals.cpp)
void BuildCurveUsageMap(ProjectData* project, bool* used);

} // namespace editor
} // namespace rev
