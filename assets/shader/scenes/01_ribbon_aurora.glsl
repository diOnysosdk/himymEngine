// @shader_id 1
// @name ribbon_aurora
// @editor_dyn dyn0 | Aurora curtain tilt.
// @editor_dyn dyn1 | Aurora sway amplitude.
// @editor_dyn dyn2 | Aurora band density.
// @editor_dyn dyn3 | Aurora horizon glow.
else if (id == 1) {
float d0 = dynBiDZ(dyn.x, 0.06);
float d1 = dynBiDZ(dyn.y, 0.08);
float d2 = dynBiDZ(dyn.z, 0.06);
float d3 = dynBiDZ(dyn.w, 0.06);
vec2 auv = (uv - 0.5) * rot(0.35 * d0) + 0.5;
float band_mul = mix(0.75, 1.35, 0.5 + 0.5 * d2);
float sway_mul = mix(0.70, 1.35, 0.5 + 0.5 * d1);
float drift0 = fbm(vec2(auv.x * 2.2 + st * 0.07, st * 0.09));
float drift1 = fbm(vec2(auv.x * 3.8 - st * 0.05, st * 0.06 + 4.0));
float curtain0 = sin(auv.x * ((4.5 + wrp * 2.3) * band_mul) + drift0 * (9.0 + 7.0 * wrp) + st * (0.80 + 0.30 * sway_mul));
float curtain1 = sin(auv.x * ((7.0 + wrp * 1.7) * band_mul) + drift1 * (6.0 + 5.0 * wrp) - st * (0.45 + 0.25 * sway_mul));
float wave = smoothstep(-0.15, 0.88, 0.62 * curtain0 + 0.38 * curtain1);
float sky_falloff = smoothstep(0.95, 0.15, auv.y);
float horizon = smoothstep(0.42, 0.02, abs(auv.y - 0.62));
col = mix(c_low * 0.10, c_high, wave * sky_falloff);
col += c_high * (0.10 + (0.18 + 0.16 * max(0.0, d3)) * wave) * horizon;
}
