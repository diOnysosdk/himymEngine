#include "rev_gltf.h"
#include "rev_mesh.h"
#include "rev_runtime.h"

#include <cmath>
#include <cstdio>
#include <cstring>

static int failures = 0;

static void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "[gltf_import_tests] FAILED: %s\n", message);
        ++failures;
    }
}

static bool Near(float actual, float expected, float epsilon = 0.001f) {
    return std::fabs(actual - expected) <= epsilon;
}

static void TransformPoint(const float matrix[16], const float point[3], float out[3]) {
    out[0] = matrix[0] * point[0] + matrix[4] * point[1] +
             matrix[8] * point[2] + matrix[12];
    out[1] = matrix[1] * point[0] + matrix[5] * point[1] +
             matrix[9] * point[2] + matrix[13];
    out[2] = matrix[2] * point[0] + matrix[6] * point[1] +
             matrix[10] * point[2] + matrix[14];
}

int main(int argc, char** argv) {
    if (argc >= 3 && std::strcmp(argv[1], "--smoke") == 0) {
        rev::gltf::ImportResult* smoke = rev::gltf::LoadMesh(argv[2]);
        Check(smoke != nullptr, "smoke LoadMesh returned null");
        if (smoke) {
            Check(smoke->ok, smoke->error);
            Check(smoke->mesh != nullptr && smoke->mesh->vertex_count > 0,
                  "smoke asset contains no mesh vertices");
            Check(smoke->has_camera, "smoke asset contains no camera");
        }
        rev::gltf::FreeImportResult(smoke);
        if (failures == 0) std::printf("[gltf_import_tests] SMOKE PASS\n");
        return failures == 0 ? 0 : 1;
    }
    const char* path = argc > 1 ? argv[1] : "tests/fixtures/blender_axis_material.gltf";
    rev::gltf::ImportResult* result = rev::gltf::LoadMesh(path);
    Check(result != nullptr, "LoadMesh returned null");
    if (!result) return 1;
    Check(result->ok, result->error);

    if (result->ok && result->mesh) {
        Check(result->mesh->vertex_count == 3, "fixture vertex count");
        const rev::mesh::Vertex& transformed = result->mesh->vertices[2];
        Check(Near(transformed.pos[0], 1.0f) &&
              Near(transformed.pos[1], 2.0f) &&
              Near(transformed.pos[2], 6.0f),
              "Blender-exported glTF axes/TRS must be consumed without another axis conversion");
        Check(Near(transformed.normal[0], 0.894427f) &&
              Near(transformed.normal[1], -0.447214f) &&
              Near(transformed.normal[2], 0.0f),
              "non-uniform node scale must use inverse-transpose normal transformation");

        Check(result->mesh->material_slot_count == 1, "fixture material slot count");
        if (result->mesh->material_slot_count == 1) {
            const rev::mesh::MaterialSlot& slot = result->mesh->material_slots[0];
            Check(slot.noise_target == 2, "material extras roughness target");
            Check(Near(slot.noise_scale, 6.5f) &&
                  Near(slot.noise_detail, 3.0f) &&
                  Near(slot.noise_roughness, 0.4f) &&
                  Near(slot.noise_distortion, 0.25f) &&
                  Near(slot.noise_strength, 0.75f),
                  "material extras noise parameters");
        }
    }

    Check(result->has_camera, "fixture camera import");
    Check(Near(result->camera_pos[0], 4.0f) &&
          Near(result->camera_pos[1], 5.0f) &&
          Near(result->camera_pos[2], 6.0f),
          "glTF camera position");
    Check(Near(result->camera_target[0], 4.0f) &&
          Near(result->camera_target[1], 5.0f) &&
          Near(result->camera_target[2], 5.0f),
          "glTF camera looks along local -Z");
    Check(result->camera_type == 0, "perspective camera type");
    Check(Near(result->camera_fov_deg, 45.0f) &&
          Near(result->camera_znear, 0.25f) &&
          Near(result->camera_zfar, 250.0f) &&
          Near(result->camera_aspect_ratio, 1.5f),
          "perspective camera projection fields");
    Check(Near(result->camera_shift_x, 0.125f) &&
          Near(result->camera_shift_y, -0.25f),
          "HiMYM camera shift extras");

    float perspective[16] = {};
    rev::runtime::Mat4PerspectiveShift(perspective, 0.78539816f, 1.5f,
                                       0.25f, 250.0f, 0.125f, -0.25f);
    Check(Near(perspective[8], 0.25f) && Near(perspective[9], -0.5f),
          "shifted perspective projection center");
    float infinite_perspective[16] = {};
    rev::runtime::Mat4PerspectiveShift(infinite_perspective, 0.78539816f, 1.5f,
                                       0.25f, 0.0f, 0.0f, 0.0f);
    Check(Near(infinite_perspective[10], -1.0f) &&
          Near(infinite_perspective[14], -0.5f),
          "infinite perspective far plane");
    float orthographic[16] = {};
    rev::runtime::Mat4Orthographic(orthographic, 4.0f, 2.0f,
                                   0.25f, 250.0f, 0.125f, -0.25f);
    Check(Near(orthographic[0], 0.25f) &&
          Near(orthographic[5], 0.5f) &&
          Near(orthographic[12], -0.25f) &&
          Near(orthographic[13], 0.5f),
          "orthographic magnitudes and shift");

    const float off_axis_eye[3] = {7.3589f, 4.9583f, 6.9258f};
    const float off_axis_center[3] = {0.0f, 0.0f, 0.0f};
    const float off_axis_up[3] = {0.0f, 1.0f, 0.0f};
    float view[16] = {};
    rev::runtime::Mat4LookAt(view, off_axis_eye, off_axis_center, off_axis_up);
    float eye_in_view[3] = {};
    float center_in_view[3] = {};
    TransformPoint(view, off_axis_eye, eye_in_view);
    TransformPoint(view, off_axis_center, center_in_view);
    Check(Near(eye_in_view[0], 0.0f) &&
          Near(eye_in_view[1], 0.0f) &&
          Near(eye_in_view[2], 0.0f),
          "off-axis camera eye must transform to the view origin");
    Check(Near(center_in_view[0], 0.0f) &&
          Near(center_in_view[1], 0.0f) &&
          center_in_view[2] < 0.0f,
          "off-axis camera target must lie on negative view Z");

    rev::gltf::FreeImportResult(result);

    rev::gltf::ImportResult* ortho_result =
        rev::gltf::LoadMesh("tests/fixtures/orthographic_camera.gltf");
    Check(ortho_result != nullptr && ortho_result->ok,
          ortho_result ? ortho_result->error : "orthographic LoadMesh returned null");
    if (ortho_result && ortho_result->ok) {
        Check(ortho_result->has_camera && ortho_result->camera_type == 1,
              "orthographic camera type import");
        Check(Near(ortho_result->camera_xmag, 4.0f) &&
              Near(ortho_result->camera_ymag, 2.0f) &&
              Near(ortho_result->camera_znear, 0.5f) &&
              Near(ortho_result->camera_zfar, 80.0f),
              "orthographic camera projection fields");
    }
    rev::gltf::FreeImportResult(ortho_result);

    if (failures == 0) std::printf("[gltf_import_tests] PASS\n");
    return failures == 0 ? 0 : 1;
}
