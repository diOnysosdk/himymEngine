#include "rev_platform.h"
#include "rev_editor.h"
#include <cstdio>
#include <cstring>

int main() {
    printf("=== Export Test ===\n");
    
    // Create minimal window for editor context
    rev::platform::WindowConfig config;
    config.width = 800;
    config.height = 600;
    config.title = "Export Test";
    config.fullscreen = false;
    
    rev::platform::Window* window = rev::platform::CreateIntroWindow(config);
    if (!window) {
        printf("Failed to create window\n");
        return 1;
    }
    
    // Load OpenGL functions
    rev::platform::LoadGLFunctions();
    
    // Create editor context
    rev::editor::EditorContext* editor = rev::editor::CreateEditor(window);
    if (!editor) {
        printf("Failed to create editor\n");
        rev::platform::DestroyIntroWindow(window);
        return 1;
    }
    
    // Load existing project
    printf("Loading project.json...\n");
    bool loaded = rev::editor::LoadProject(editor, "project.json");
    
    if (loaded) {
        printf("Loaded %d scenes with total duration %.2fs\n", 
            editor->project->scene_count,
            editor->project->total_duration);
    } else {
        printf("Could not load project.json, creating sample project\n");
        
        // Create sample project
        rev::editor::NewProject(editor);
        int scene1 = rev::editor::AddScene(editor, "Opening", 10.0f);
        int scene2 = rev::editor::AddScene(editor, "Climax", 15.0f);
        
        // Add a shader to first scene
        rev::editor::SceneBlock* scene = rev::editor::GetScene(editor, scene1);
        if (scene) {
            rev::editor::ShaderCue cue = {};
            rev::editor::ResetShaderValues(&cue);
            strncpy_s(cue.shader_name, "Plasma Vibrant", sizeof(cue.shader_name) - 1);
            cue.shader_scene_id = 0;
            rev::editor::AddShaderCue(scene, cue);
        }
    }
    
    // Export to cues.txt
    printf("Exporting to assets/cues.txt...\n");
    if (rev::editor::ExportProject(editor, "assets/cues.txt")) {
        printf("SUCCESS: Exported to assets/cues.txt\n");
    } else {
        printf("ERROR: Failed to export\n");
    }
    
    // Cleanup
    rev::editor::DestroyEditor(editor);
    rev::platform::DestroyIntroWindow(window);
    
    printf("=== Export Test Complete ===\n");
    printf("Check assets/cues.txt to verify output\n");
    
    return 0;
}
