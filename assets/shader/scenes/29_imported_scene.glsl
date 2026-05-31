// @shader_id 29
// @name imported_scene
// @editor_dyn dyn0 | Gradient tilt/shear.
// @editor_dyn dyn1 | Middle band vertical position.
// @editor_dyn dyn2 | Middle band softness.
// @editor_dyn dyn3 | Middle color warmth boost (additional).
else if (id == 29) {
float d0 = dynBiDZ(dyn.x, 0.06);
float d1 = dynBiDZ(dyn.y, 0.08);
float d2 = dynBiDZ(dyn.z, 0.06);
float d3 = dynBiDZ(dyn.w, 0.06);

vec2 guv = uv;
guv.x += d0 * 0.12 * (uv.y - 0.5);

float middle_pos = clamp(0.50 + d1 * 0.18, 0.18, 0.82);
float middle_softness = mix(0.12, 0.38, 0.5 + 0.5 * d2);

vec3 bottom_col = c_low * 0.95 + vec3(0.02, 0.01, 0.00);
vec3 middle_col = c_mid;
middle_col += vec3(0.08, 0.04, 0.02) * max(0.0, (ins - 1.0) + d3);
vec3 top_col = c_high * 1.05;

float to_middle = smoothstep(middle_pos - middle_softness, middle_pos, guv.y);
float from_middle = smoothstep(middle_pos, middle_pos + middle_softness, guv.y);

vec3 band_mix = mix(bottom_col, middle_col, to_middle);
col = mix(band_mix, top_col, from_middle);

float vign = uv.x * uv.y * (1.0 - uv.x) * (1.0 - uv.y);
col *= 0.88 + 0.12 * pow(clamp(vign * 18.0, 0.0, 1.0), 0.35);
}
