// @shader_id 27
// @name desert_flyover
// @editor_dyn dyn0 | Camera bank and heat-wave drift.
// @editor_dyn dyn1 | Forward flyover speed.
// @editor_dyn dyn2 | Dune sharpness and ridge detail.
// @editor_dyn dyn3 | Cactus density and dust glow.
else if (id == 27) {
float d0 = dynBiDZ(dyn.x, 0.06);
float d1 = dynBiDZ(dyn.y, 0.08);
float d2 = dynBiDZ(dyn.z, 0.06);
float d3 = dynBiDZ(dyn.w, 0.06);

float fly_speed = mix(0.45, 1.90, 0.5 + 0.5 * d1);
float dune_detail = mix(0.70, 1.55, 0.5 + 0.5 * d2);
float cactus_boost = max(0.0, d3);

vec2 duv = uv;
duv.x += 0.02 * d0 * sin(st * 0.55 + uv.y * 7.0);
duv.y += 0.008 * sin(duv.x * 34.0 + st * (1.0 + abs(d0) * 0.7));

float horizon = 0.48 + 0.03 * sin(st * 0.16 + duv.x * 2.5);

vec3 sky_bottom = c_low * 0.35 + vec3(0.10, 0.09, 0.10);
vec3 sky_top = c_high * 1.15 + vec3(0.20, 0.10, 0.04);
float sky_t = smoothstep(0.0, 1.0, duv.y);
vec3 sky = mix(sky_bottom, sky_top, sky_t);

vec2 sun_pos = vec2(0.72, horizon - 0.03);
float sun = exp(-dot(duv - sun_pos, duv - sun_pos) * 85.0);
sky += c_high * sun * 0.85;

col = sky;

float dune0 = horizon + 0.07
	+ sin(duv.x * 2.6 + st * fly_speed * 0.26) * 0.018
	+ fbm(vec2(duv.x * 3.5 + st * 0.07, 0.5)) * 0.028 * dune_detail;
float dune1 = horizon + 0.17
	+ sin(duv.x * 4.3 + st * fly_speed * 0.44) * 0.028
	+ fbm(vec2(duv.x * 6.0 + st * 0.10, 1.8)) * 0.042 * dune_detail;
float dune2 = horizon + 0.29
	+ sin(duv.x * 6.1 + st * fly_speed * 0.74) * 0.036
	+ fbm(vec2(duv.x * 9.0 + st * 0.14, 3.2)) * 0.060 * dune_detail;

float mask0 = smoothstep(dune0 - 0.018, dune0 + 0.020, duv.y);
float mask1 = smoothstep(dune1 - 0.020, dune1 + 0.024, duv.y);
float mask2 = smoothstep(dune2 - 0.022, dune2 + 0.030, duv.y);

vec3 dune_col0 = mix(c_low * 0.45, c_high * 0.58, 0.35);
vec3 dune_col1 = mix(c_low * 0.58, c_high * 0.72, 0.48);
vec3 dune_col2 = mix(c_low * 0.70, c_high * 0.86, 0.58);

col = mix(col, dune_col0, mask0);
col = mix(col, dune_col1, mask1);
col = mix(col, dune_col2, mask2);

float haze = smoothstep(horizon - 0.01, horizon + 0.24, duv.y);
col = mix(col, c_high * 0.60 + c_low * 0.25, haze * 0.20);

float cactus = 0.0;
for (int layer = 0; layer < 2; ++layer) {
float lf = float(layer);
float density = 12.0 + 8.0 * lf + cactus_boost * 10.0;
float gx = duv.x * density + st * fly_speed * (0.40 + 0.45 * lf);
float cell = floor(gx);
float fx = fract(gx) - 0.5;
float rnd = hash(vec2(cell, 31.7 + lf * 23.0));
float spawn = step(0.83 - cactus_boost * 0.22, rnd);

float cactus_ground = dune2 + 0.010 + 0.025 * lf + (hash(vec2(cell, 8.3 + lf)) - 0.5) * 0.012;
float cactus_h = mix(0.08, 0.18, hash(vec2(cell, 5.2 + lf * 2.0)));
float y = (cactus_ground - duv.y) / max(0.001, cactus_h);

float trunk_w = 0.018 + 0.010 * hash(vec2(cell, 2.2 + lf));
float trunk = smoothstep(0.0, trunk_w, trunk_w - abs(fx))
* smoothstep(-0.03, 0.06, y)
* (1.0 - smoothstep(0.72, 1.03, y));

float arm_h = 0.40 + 0.17 * hash(vec2(cell, 6.1 + lf));
float arm_l = smoothstep(0.0, 0.016, 0.016 - abs(fx + 0.055))
* smoothstep(arm_h - 0.06, arm_h + 0.01, y)
* (1.0 - smoothstep(arm_h + 0.08, arm_h + 0.20, y));
float arm_r = smoothstep(0.0, 0.016, 0.016 - abs(fx - 0.050))
* smoothstep(arm_h - 0.02, arm_h + 0.04, y)
* (1.0 - smoothstep(arm_h + 0.12, arm_h + 0.24, y));

float shape = max(trunk, max(arm_l, arm_r));
cactus += spawn * shape * (0.75 - 0.20 * lf);
}

vec3 cactus_col = mix(c_low * 0.10, c_low * 0.30 + c_high * 0.04, 0.30);
col = mix(col, cactus_col, clamp(cactus, 0.0, 1.0));

if (cactus_boost > 0.0) {
float dust_field = fbm(vec2(duv.x * 10.0 + st * fly_speed * 0.7, duv.y * 3.8 - st * 0.2));
float dust_mask = smoothstep(horizon + 0.02, horizon + 0.28, duv.y);
col += c_high * dust_field * dust_mask * cactus_boost * 0.16;
}
}
