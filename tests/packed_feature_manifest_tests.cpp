#include "rev_pack.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

struct ExpectedFeatures {
    bool xm;
    bool pixel;
    bool particles;
    bool mesh;
    bool gltf;
    bool image;
    bool text;
    bool image_decoder;
    bool animated_sprite;
    bool scroll_text;
};

int failures = 0;

void Check(bool condition, const char* case_name, const char* message) {
    if (condition) return;
    std::fprintf(stderr, "[packed_feature_manifest_tests] %s: %s\n", case_name, message);
    ++failures;
}

bool WriteText(const std::filesystem::path& path, const std::string& content) {
    std::ofstream file(path, std::ios::binary);
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    return file.good();
}

std::string ReadText(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file),
                       std::istreambuf_iterator<char>());
}

void CheckFlag(const std::string& content,
               const char* case_name,
               const char* prefix,
               const char* name,
               bool expected,
               const char* enabled,
               const char* disabled) {
    const std::string expected_line = std::string(prefix) + name +
                                      (expected ? enabled : disabled);
    Check(content.find(expected_line) != std::string::npos,
          case_name, expected_line.c_str());
}

void RunCase(const std::filesystem::path& root,
             const char* case_name,
             const char* cues,
             const ExpectedFeatures& expected,
             bool needs_asset,
             int expected_shader_id = 0,
             int expected_post_effect_id = -1) {
    const std::filesystem::path case_dir = root / case_name;
    std::filesystem::create_directories(case_dir);
    const std::filesystem::path cues_path = case_dir / "cues.txt";
    const std::filesystem::path header_path = case_dir / "packed_assets.h";
    const std::filesystem::path cache_path = case_dir / "pack_cache.txt";
    Check(WriteText(cues_path, cues), case_name, "could not write cues.txt");
    if (needs_asset) {
        Check(WriteText(case_dir / "asset.bin", "test-asset"), case_name,
              "could not write test asset");
        if (std::string(case_name) == "transferred_absolute_path") {
            std::filesystem::create_directories(case_dir / "project_assets");
            Check(WriteText(case_dir / "project_assets" / "asset.bin", "test-asset"),
                  case_name, "could not write transferred test asset");
        }
    }

    const rev::pack::PackResult result = rev::pack::PackAssets(
        cues_path.string().c_str(), header_path.string().c_str(),
        cache_path.string().c_str(), case_dir.string().c_str());
    Check(result.ok, case_name, result.error[0] ? result.error : "PackAssets failed");
    if (!result.ok) return;

    const std::string header = ReadText(header_path);
    const std::string cmake = ReadText(case_dir / "packed_features.cmake");
    Check(!header.empty(), case_name, "packed_assets.h was not generated");
    Check(!cmake.empty(), case_name, "packed_features.cmake was not generated");
    const std::string expected_shader = "{ " + std::to_string(expected_shader_id) + ", \"";
    Check(header.find(expected_shader) != std::string::npos,
          case_name, "expected packed shader source is missing");
    if (expected_shader_id != 46) {
        Check(header.find("{ 46, \"") == std::string::npos,
              case_name, "unused shader 46 should not be packed");
    }
    if (expected_post_effect_id >= 0) {
        const std::string expected_effect = "if (u_enabled[" +
                                            std::to_string(expected_post_effect_id) + "]";
        Check(header.find(expected_effect) != std::string::npos,
              case_name, "expected packed post-effect branch is missing");
    }
    if (expected_post_effect_id != 22) {
        Check(header.find("if (u_enabled[22]") == std::string::npos,
              case_name, "unused post-effect branch 22 should not be packed");
    }

    const struct FeatureCheck {
        const char* name;
        bool expected;
    } features[] = {
        {"XM", expected.xm},
        {"PIXEL", expected.pixel},
        {"PARTICLES", expected.particles},
        {"MESH", expected.mesh},
        {"GLTF", expected.gltf},
        {"IMAGE", expected.image},
        {"TEXT", expected.text},
        {"IMAGE_DECODER", expected.image_decoder},
        {"ANIMATED_SPRITE", expected.animated_sprite},
        {"SCROLL_TEXT", expected.scroll_text},
    };
    for (const FeatureCheck& feature : features) {
        CheckFlag(header, case_name, "#define HIMYM_USE_", feature.name,
                  feature.expected, " 1", " 0");
        CheckFlag(cmake, case_name, "set(HIMYM_PACKED_USE_", feature.name,
                  feature.expected, " ON)", " OFF)");
    }
}

void RunMissingAfterDuplicateCase(const std::filesystem::path& root) {
    const char* case_name = "missing_after_duplicate";
    const std::filesystem::path case_dir = root / case_name;
    std::filesystem::create_directories(case_dir);
    Check(WriteText(case_dir / "asset.bin", "same-payload"), case_name,
          "could not write duplicate asset");
    Check(WriteText(case_dir / "cues.txt",
                    "[image_cues]\na|asset.bin\nb|asset.bin\nc|missing.bin\n"),
          case_name, "could not write cues.txt");
    const rev::pack::PackResult result = rev::pack::PackAssets(
        (case_dir / "cues.txt").string().c_str(),
        (case_dir / "packed_assets.h").string().c_str(),
        (case_dir / "pack_cache.txt").string().c_str(),
        case_dir.string().c_str());
    Check(!result.ok, case_name, "missing required asset should fail cleanly");
}

}  // namespace

int main() {
    const auto unique = std::chrono::high_resolution_clock::now()
                            .time_since_epoch().count();
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("himym_packed_feature_tests_" + std::to_string(unique));
    std::filesystem::create_directories(root);

    RunCase(root, "empty", "[metadata]\n", {}, false);
    RunCase(root, "shader", "[shader_cues]\n18|0|1\n", {}, false, 18);
    RunCase(root, "asset_shader",
            "[image_cues]\nasset|asset.bin|0|0|1|1|0|1|0|0|0|0|0|0|-1|-1|-1|-1|0|0|-1|0|1|5,1\n",
            {false, false, false, false, false, true, false, true}, true, 5);
    RunCase(root, "scene_post_effect",
            "[scene_layer_post_effects]\n0|10|1|12,1\n",
            {}, false, 0, 12);
    RunCase(root, "asset_post_effect",
            "[image_cues]\nasset|asset.bin|0|0|1|1|0|1|0|0|0|0|0|0|-1|-1|-1|-1|0|0|-1|1|3,1|0\n",
            {false, false, false, false, false, true, false, true}, true, 0, 3);
    RunCase(root, "animated_sprite", "[animated_sprite_cues]\nasset|asset.bin\n",
            {false, false, false, false, false, false, false, true, true, false}, true);
    RunCase(root, "scroll_text", "[scroll_text_cues]\nscroll row\n",
            {false, false, false, false, false, false, false, true, false, true}, false);
    RunCase(root, "text", "[text_cues]\ntext row\n",
            {false, false, false, false, false, false, true, true}, false);
    RunCase(root, "scene_menu", "[scene_menus]\nmenu row\n",
            {false, false, false, false, false, false, true, true}, false);
    RunCase(root, "shader_texture_channel",
            "[shader_pipeline_channels]\n0|0|0|1|asset.bin\n",
            {false, false, false, false, false, false, false, true}, true);
    RunCase(root, "xm", "[music_cues]\nmusic|asset.bin\n",
            {true, false, false, false, false}, true);
    RunCase(root, "pixel", "[pixel_cues]\npixels|asset.bin\n",
            {false, true, false, false, false}, true);
    RunCase(root, "particle", "[pixel_emitter_cues]\nnone|unused|1\n",
            {false, false, true, false, false}, false);
    RunCase(root, "particle_image", "[pixel_emitter_cues]\nimage|asset.bin|0\n",
            {false, false, true, false, false, false, false, true}, true);
    RunCase(root, "procedural_mesh", "[mesh_cues]\nmesh||0\n",
            {false, false, false, true, false}, false);
    RunCase(root, "gltf", "[mesh_cues]\nmodel|asset.bin|4\n",
            {false, false, false, true, true}, true);
    RunCase(root, "transferred_absolute_path",
            "[music_cues]\nmusic|Z:\\OldMachine\\project_assets\\asset.bin\n",
            {true, false, false, false, false}, true);
    RunMissingAfterDuplicateCase(root);

    std::error_code cleanup_error;
    std::filesystem::remove_all(root, cleanup_error);
    Check(!cleanup_error, "cleanup", "could not remove temporary test directory");

    if (failures != 0) return 1;
    std::printf("[packed_feature_manifest_tests] PASS\n");
    return 0;
}
