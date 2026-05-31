// @shader_id 26
// @name cathedral_rays
// @editor_dyn dyn0 | Rose-window spin bias.
// @editor_dyn dyn1 | Beam breathing amplitude.
// @editor_dyn dyn2 | Stained-glass segmentation.
// @editor_dyn dyn3 | Godray bloom strength.
else if (id == 26) {
float d0 = dynBiDZ(dyn.x, 0.06);
float d1 = dynBiDZ(dyn.y, 0.08);
float d2 = dynBiDZ(dyn.z, 0.06);
float d3 = dynBiDZ(dyn.w, 0.06);

float spin = st * (0.14 + 0.16 * d0);
vec2 q = p * rot(spin);
float aq = atan(q.y, q.x);
float rq = length(q);

float seg = mix(8.0, 22.0, 0.5 + 0.5 * d2);
float petals = abs(cos(aq * seg + fbm(q * 6.0 + st * 0.15) * 1.2));
float spokes = pow(max(0.0, cos(aq * (seg * 0.5) - st * 0.35)), 5.0);
float ring = exp(-pow((rq - 0.26) * 9.0, 2.0));

float breath = 1.0 + 0.25 * d1 * sin(st * 0.9 + rq * 14.0);
float glass = smoothstep(0.18, 0.95, petals * breath);
float radial_falloff = smoothstep(0.98, 0.05, rq);

vec3 window_col = mix(c_low, c_high, glass);
window_col += c_high * (0.25 * ring + 0.30 * spokes);

float beam_noise = fbm(vec2(aq * 2.6, rq * 5.0 - st * 0.25));
float beams = pow(max(0.0, 1.0 - rq) * (0.55 + 0.45 * beam_noise), 2.3);
float bloom = mix(0.6, 2.0, 0.5 + 0.5 * d3);

col = window_col * radial_falloff;
col += c_high * beams * bloom * 0.55;
}