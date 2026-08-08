#pragma once

namespace rev {
namespace editor {

struct ShaderPreset {
    int id;
    const char* name;
    const char* description;
    const char* fragment_source;
};

enum ShaderCategory {
    ShaderCategoryFoundation,
    ShaderCategorySpaceAndTunnels,
    ShaderCategoryOrganicAndAtmospheric,
    ShaderCategoryGeometricAndFractal,
    ShaderCategoryRetroAndGlitch,
    ShaderCategoryNoiseAndMaterials,
    ShaderCategoryCount
};

extern const ShaderPreset g_shader_presets[];
extern const int g_shader_preset_count;

const char* GetShaderSourceById(int shader_id);
const char* GetPostEffectFragmentSource();
const ShaderPreset* GetPresetById(int shader_id);
ShaderCategory GetShaderCategory(int shader_id);
const char* GetShaderCategoryName(ShaderCategory category);

} // namespace editor
} // namespace rev

