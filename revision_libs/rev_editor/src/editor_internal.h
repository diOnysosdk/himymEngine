#pragma once

#include "rev_editor.h"

// Internal declarations for editor UI modules
// These functions are split across editor_modals.cpp and editor_panels.cpp

namespace rev {
namespace editor {

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
void RenderTextModal(EditorContext* editor);
void RenderMeshModal(EditorContext* editor);

} // namespace editor
} // namespace rev
