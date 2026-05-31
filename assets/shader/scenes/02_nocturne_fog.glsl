// @shader_id 2
// @name nocturne_fog
// @editor_dyn dyn0 | Chrome flow direction.
// @editor_dyn dyn1 | Chrome ripple motion.
// @editor_dyn dyn2 | Chrome micro-distortion.
// @editor_dyn dyn3 | Chrome specular hardening.
else if (id == 2) {
float d0 = dynBiDZ(dyn.x, 0.06);
float d1 = dynBiDZ(dyn.y, 0.08);
float d2 = dynBiDZ(dyn.z, 0.06);
float d3 = dynBiDZ(dyn.w, 0.06);
vec2 muv = (uv - 0.5) * rot(d0 * 0.65) + 0.5;
float micro = mix(0.70, 1.45, 0.5 + 0.5 * d2);
float flow = mix(0.55, 1.45, 0.5 + 0.5 * d1);
float f1 = fbm(muv * (3.0 * micro) + st * (0.18 * flow));
float f2 = fbm(muv * (3.0 * micro) - st * (0.21 * flow) + 0.1);
float spec_pow = mix(10.0, 26.0, 0.5 + 0.5 * d3);
float spec = pow(clamp(1.0 - abs(f1 - f2), 0.0, 1.0), spec_pow);
float anis = 1.0 - abs(sin((muv.x - 0.5) * 22.0 + st * 0.55));
col = mix(c_low, c_high, f1);
col += c_high * (0.60 * spec + 0.24 * pow(anis, 4.0));
}
