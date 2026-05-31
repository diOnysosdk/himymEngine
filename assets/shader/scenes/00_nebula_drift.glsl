// @shader_id 0
// @name nebula_drift
// @editor_dyn dyn0 | Nebula swirl rotation.
// @editor_dyn dyn1 | Nebula advection drift.
// @editor_dyn dyn2 | Nebula ridge sharpness.
// @editor_dyn dyn3 | Nebula core glow amount.
else if (id == 0) {
float d0 = dynBiDZ(dyn.x, 0.06);
float d1 = dynBiDZ(dyn.y, 0.08);
float d2 = dynBiDZ(dyn.z, 0.06);
float d3 = dynBiDZ(dyn.w, 0.06);
vec2 nuv = (uv - 0.5) * rot(d0 * 0.9) + 0.5;
nuv += vec2(sin(st * 0.33), cos(st * 0.27)) * (0.035 * d1);
float ridge_sharp = mix(0.75, 1.55, 0.5 + 0.5 * d2);
vec2 q = vec2(fbm(nuv * 2.1 + st * 0.11), fbm(nuv * 2.1 + 1.7 - st * 0.04));
vec2 r2 = vec2(fbm(nuv * 4.0 + 3.5 * q + st * 0.21), fbm(nuv * 4.0 + 2.0 * q - st * 0.17));
float f = fbm(nuv * (3.2 + wrp * 0.8) + 4.6 * q + 1.8 * r2);
float ridged = 1.0 - abs(2.0 * f - 1.0);
float core = smoothstep(0.92, 0.12, r) * mix(0.70, 1.40, 0.5 + 0.5 * d3);
col = mix(c_low, c_high, f);
col += c_high * (0.16 * pow(ridged, 1.6 * ridge_sharp) + 0.18 * core);
}
