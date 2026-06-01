#pragma once

namespace rev {
namespace editor {

// Shader preset definition with embedded GLSL source code
struct ShaderPreset {
    int id;
    const char* name;
    const char* description;
    const char* fragment_source;  // GLSL fragment shader source code
};

// Central shader registry - single source of truth for editor and runtime
extern const ShaderPreset g_shader_presets[];
extern const int g_shader_preset_count;

// Helper to get shader source by ID (returns nullptr if not found)
const char* GetShaderSourceById(int shader_id);

// Helper to get preset by ID (returns nullptr if not found)
const ShaderPreset* GetPresetById(int shader_id);

} // namespace editor
} // namespace rev
