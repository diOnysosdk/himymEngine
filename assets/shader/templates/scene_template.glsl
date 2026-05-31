// Shader Scene Template for revision2026 intro runtime
// Copy into assets/shader/scenes/NN_your_name.glsl and replace placeholders.
// Keep dyn neutral behavior around 0.5 and keep branch in else-if style.

// @shader_id 99
// @name your_scene_name
// @editor_dyn dyn0 | Direction / phase bias (neutral at 0.5).
// @editor_dyn dyn1 | Motion speed amount (neutral at 0.5).
// @editor_dyn dyn2 | Shape/detail amount (neutral at 0.5).
// @editor_dyn dyn3 | Energy/glow amount (neutral at 0.5).
else if (id == 99) {
float d0 = dynBiDZ(dyn.x, 0.06);
float d1 = dynBiDZ(dyn.y, 0.08);
float d2 = dynBiDZ(dyn.z, 0.06);
float d3 = dynBiDZ(dyn.w, 0.06);

float speed_mul  = mix(0.70, 1.60, 0.5 + 0.5 * d1);
float detail_mul = mix(0.70, 1.60, 0.5 + 0.5 * d2);
float energy     = max(0.0, d3);

vec2 q = uv;
q += vec2(
    fbm(uv * 2.0 + vec2(st * 0.07, 0.0)),
    fbm(uv.yx * 2.3 - vec2(st * 0.05, 0.0))
) * (0.08 + 0.10 * wrp);

float n0 = fbm(q * (3.0 * detail_mul) + st * (0.12 * speed_mul));
float n1 = fbm(q * (6.0 * detail_mul) - st * (0.18 * speed_mul));
float f  = n0 * 0.65 + n1 * 0.35;

vec3 base_col = mix(c_low, c_high, smoothstep(0.18, 0.92, f));

float hi = smoothstep(0.78, 1.0, f);
base_col += c_high * hi * (0.10 + 0.35 * energy);

col = base_col;
}

// Integration checklist:
// 1) Copy this into assets/shader/scenes/NN_name.glsl and set @shader_id / branch id.
// 2) Add a matching preset row in assets/shader/presets.txt.
// 3) Validate:
//    cmake -S . -B build
//    cmake --build build --config Release
