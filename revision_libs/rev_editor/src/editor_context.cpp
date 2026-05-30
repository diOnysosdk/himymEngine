#include "rev_editor.h"
#include <cstring>

// NOTE: This file requires Dear ImGui to be fully functional
// See revision_libs/rev_editor/README.md for setup instructions

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_opengl3.h"

namespace rev {
namespace editor {

EditorContext* CreateEditor(rev::platform::Window* window) {
    EditorContext* editor = new EditorContext();
    editor->window = window;
    editor->imgui_context = nullptr;
    editor->project = nullptr;
    editor->show_timeline = true;
    editor->show_curve_editor = true;
    editor->show_shader_modal = false;
    editor->show_demo = false;
    editor->timeline_zoom = 1.0f;
    editor->timeline_scroll = 0.0f;
    
    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    
    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(window->hwnd);
    ImGui_ImplOpenGL3_Init("#version 330");
    
    // Style
    ImGui::StyleColorsDark();
    
    // Create empty project
    editor->project = new ProjectData();
    editor->project->timeline = rev::sequence::CreateTimeline(64);
    editor->project->curves = nullptr;
    editor->project->curve_count = 0;
    editor->project->modified = false;
    memset(editor->project->project_path, 0, sizeof(editor->project->project_path));
    
    return editor;
}

void DestroyEditor(EditorContext* editor) {
    if (!editor) return;
    
    // Cleanup project
    if (editor->project) {
        rev::sequence::DestroyTimeline(editor->project->timeline);
        
        for (int i = 0; i < editor->project->curve_count; ++i) {
            rev::curve::DestroyCurve(editor->project->curves[i]);
        }
        delete[] editor->project->curves;
        
        delete editor->project;
    }
    
    // Cleanup ImGui
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    
    delete editor;
}

bool LoadProject(EditorContext* editor, const char* path) {
    if (!editor || !path) return false;
    
    // TODO: Implement JSON loading
    // For now, just store the path
    strncpy_s(editor->project->project_path, path, sizeof(editor->project->project_path) - 1);
    editor->project->modified = false;
    
    return true;
}

bool SaveProject(EditorContext* editor, const char* path) {
    if (!editor || !path) return false;
    
    // TODO: Implement JSON saving
    strncpy_s(editor->project->project_path, path, sizeof(editor->project->project_path) - 1);
    editor->project->modified = false;
    
    return true;
}

bool NewProject(EditorContext* editor) {
    if (!editor) return false;
    
    // Clear existing project
    rev::sequence::DestroyTimeline(editor->project->timeline);
    editor->project->timeline = rev::sequence::CreateTimeline(64);
    
    for (int i = 0; i < editor->project->curve_count; ++i) {
        rev::curve::DestroyCurve(editor->project->curves[i]);
    }
    delete[] editor->project->curves;
    editor->project->curves = nullptr;
    editor->project->curve_count = 0;
    
    memset(editor->project->project_path, 0, sizeof(editor->project->project_path));
    editor->project->modified = false;
    
    return true;
}

void BeginFrame(EditorContext* editor) {
    if (!editor) return;
    
    // ImGui frame start
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void RenderUI(EditorContext* editor) {
    if (!editor) return;
    
    // For now, render a placeholder message
    // Once ImGui is added, this will show the full editor UI
    
    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
    
    RenderMenuBar(editor);
    
    if (editor->show_timeline) {
        RenderTimeline(editor);
    }
    
    if (editor->show_curve_editor) {
        RenderCurveEditor(editor);
    }
    
    if (editor->show_shader_modal) {
        RenderShaderModal(editor);
    }
    
    if (editor->show_demo) {
        // ImGui::ShowDemoWindow(&editor->show_demo);  // Requires imgui_demo.cpp
    }
}

void EndFrame(EditorContext* editor) {
    if (!editor) return;
    
    // ImGui frame end and render
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void RenderMenuBar(EditorContext* editor) {
    if (!editor) return;
    
    // ImGui menu bar
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New", "Ctrl+N")) { NewProject(editor); }
            if (ImGui::MenuItem("Open", "Ctrl+O")) { /* TODO: File dialog */ }
            if (ImGui::MenuItem("Save", "Ctrl+S")) { SaveProject(editor, editor->project->project_path); }
            if (ImGui::MenuItem("Save As")) { /* TODO: File dialog */ }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) { editor->window->should_close = true; }
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Timeline", nullptr, &editor->show_timeline);
            ImGui::MenuItem("Curve Editor", nullptr, &editor->show_curve_editor);
            ImGui::MenuItem("ImGui Demo", nullptr, &editor->show_demo);
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Build")) {
            if (ImGui::MenuItem("Export Project")) { ExportProject(editor, "export.txt"); }
            if (ImGui::MenuItem("Build and Run", "F5")) { BuildAndRun(editor); }
            ImGui::EndMenu();
        }
        
        ImGui::EndMainMenuBar();
    }
}

void RenderTimeline(EditorContext* editor) {
    if (!editor) return;
    
    // ImGui timeline window
    if (ImGui::Begin("Timeline", &editor->show_timeline)) {
        ImGui::Text("Timeline: %d cues", editor->project->timeline.cue_count);
        ImGui::SliderFloat("Zoom", &editor->timeline_zoom, 0.1f, 10.0f);
        
        // TODO: Render timeline cues
        
        ImGui::End();
    }
}

void RenderCurveEditor(EditorContext* editor) {
    if (!editor) return;
    
    // ImGui curve editor window
    if (ImGui::Begin("Curve Editor", &editor->show_curve_editor)) {
        ImGui::Text("Curves: %d", editor->project->curve_count);
        
        // TODO: Render curve canvas
        
        ImGui::End();
    }
}

void RenderShaderModal(EditorContext* editor) {
    if (!editor) return;
    
    // ImGui shader modal
    if (ImGui::BeginPopupModal("Shader Parameters", &editor->show_shader_modal)) {
        // TODO: Shader parameter controls
        
        if (ImGui::Button("Apply")) {
            editor->show_shader_modal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            editor->show_shader_modal = false;
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::EndPopup();
    }
}

bool ExportProject(EditorContext* editor, const char* output_path) {
    if (!editor || !output_path) return false;
    
    // TODO: Implement export to cues.txt format
    return false;
}

bool BuildAndRun(EditorContext* editor) {
    if (!editor) return false;
    
    // TODO: Run cmake --build and launch intro
    return false;
}

}  // namespace editor
}  // namespace rev
