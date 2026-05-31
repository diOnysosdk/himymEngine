// @shader_id 7
// @name lava_flow
// @editor_dyn dyn0 | Flow direction drift.
// @editor_dyn dyn1 | Magma advection speed.
// @editor_dyn dyn2 | Crack width and ridge sharpness.
// @editor_dyn dyn3 | Ember intensity.
else if (id == 7) {
float d0 = dynBiDZ(dyn.x, 0.06);
float d1 = dynBiDZ(dyn.y, 0.08);
float d2 = dynBiDZ(dyn.z, 0.06);
float d3 = dynBiDZ(dyn.w, 0.06);

vec2 flow_dir = normalize(vec2(cos(d0 * 2.5), sin(d0 * 2.5) + 0.2));
float flow_speed = mix(0.45, 1.9, 0.5 + 0.5 * d1);
float crack_ctrl = mix(0.16, 0.40, 0.5 + 0.5 * d2);
float ember = max(0.0, d3);

vec2 q = uv;
q += flow_dir * st * 0.08 * flow_speed;
q += vec2(
	fbm(uv * 2.5 + st * 0.08),
	fbm(uv.yx * 2.8 - st * 0.07)
) * (0.10 + 0.14 * wrp);

float n0 = fbm(q * (3.2 + 1.2 * wrp) + vec2(st * 0.10, -st * 0.05));
float n1 = fbm(q * (6.5 + 1.6 * wrp) + vec2(-st * 0.16, st * 0.08));
float n = n0 * 0.65 + n1 * 0.35;

float cell = abs(fract(n * 5.2) - 0.5);
float cracks = smoothstep(crack_ctrl, 0.02, cell);
float heat = smoothstep(0.28, 0.92, n + cracks * 0.40);

vec3 rock_col = mix(c_low * 0.14, c_low * 0.38, n0);
vec3 magma_col = mix(c_low * 0.30 + vec3(0.08, 0.03, 0.00), c_high * 1.15, heat);
col = mix(rock_col, magma_col, heat);
col += c_high * cracks * (0.38 + 0.28 * heat);

if (ember > 0.0) {
	vec2 gp = q * 130.0 + vec2(st * 21.0, -st * 15.0);
	vec2 gi = floor(gp);
	vec2 gf = fract(gp) - 0.5;
	float rnd = hash(gi);
	float gate = step(0.995 - 0.012 * ember, rnd);
	float spark = exp(-dot(gf, gf) * 75.0);
	col += c_high * gate * spark * (0.45 + 1.15 * ember);
}
}
