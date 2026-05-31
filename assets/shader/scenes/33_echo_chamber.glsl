// @shader_id 33
// @name nebula_corridor
// @editor_dyn dyn0 | Forward drift direction and glide speed.
// @editor_dyn dyn1 | Dust column spacing and depth layering.
// @editor_dyn dyn2 | Volumetric swirl amount.
// @editor_dyn dyn3 | Starlight flares and highlight strength.
else if (id == 33) {
float d0 = dynBiDZ(dyn.x, 0.06);
float d1 = dynBiDZ(dyn.y, 0.06);
float d2 = dynBiDZ(dyn.z, 0.05);
float d3 = dynBiDZ(dyn.w, 0.05);

vec2 q = uv - 0.5;
q += vec2(d0 * 0.030, d2 * 0.016);
float rr = length(q);

float dir_angle = d0 * 3.14159265;
vec2 dir = vec2(cos(dir_angle), sin(dir_angle));
float glide = (0.18 + 0.32 * abs(spd)) * mix(0.45, 2.10, 0.5 + 0.5 * abs(d0));

float column_scale = mix(1.8, 5.8, 0.5 + 0.5 * d1);
float layer_depth = mix(0.45, 1.55, 0.5 + 0.5 * d1);
float swirl = mix(0.10, 1.00, 0.5 + 0.5 * d2) * (0.35 + 0.65 * wrp);
float flare_gain = max(0.0, d3);

vec2 flow = q + dir * st * glide + vec2(d2 * 0.42, -d1 * 0.28);
float dust_a = fbm(flow * vec2(column_scale, column_scale * 0.65) + vec2(st * 0.06, -st * 0.03));
float dust_b = fbm((flow + vec2(0.8, -0.5)) * vec2(column_scale * 1.7, column_scale * 0.9) - vec2(st * 0.09, st * 0.05));
float dust_c = fbm((flow + vec2(-0.9, 0.4)) * vec2(column_scale * 2.4, column_scale * 1.4) + vec2(st * 0.13, -st * 0.08));

float lane_shape = 1.0 - smoothstep(0.10, 0.92, abs(q.x) * (0.9 + 0.6 * layer_depth));
float billow = smoothstep(0.34, 0.88, dust_a * 0.40 + dust_b * 0.35 + dust_c * 0.25 + lane_shape * 0.25);

float vortex = fbm((q + vec2(0.0, st * 0.09)) * (3.0 + 2.0 * swirl) + vec2(sin(st * 0.33), cos(st * 0.27)));
float shear = 0.5 + 0.5 * sin((q.y + vortex * 0.6 + d0 * 0.22) * (12.0 + 8.0 * swirl) + st * (1.4 + swirl * 1.6));
float filaments = smoothstep(0.62, 0.98, shear) * billow;

float spark_field = fbm((q + dir * st * glide * 0.8) * (18.0 + 10.0 * layer_depth));
float stars = smoothstep(0.93 - 0.04 * flare_gain - 0.02 * max(0.0, d1), 1.0, spark_field);
float edge_flare = (1.0 - smoothstep(0.0, 0.55, rr)) * (0.06 + 0.24 * flare_gain);

vec3 deep_bg = mix(c_low * 0.08, c_mid * 0.16, smoothstep(0.0, 1.0, uv.y));
vec3 cloud_col = mix(c_low * 0.35 + c_mid * 0.25, c_high, billow * 0.75 + filaments * 0.25);
cloud_col = mix(cloud_col, c_high, max(0.0, d3) * 0.16);

col = deep_bg;
col += cloud_col * billow * (0.55 + 0.55 * ins);
col += mix(c_mid, c_high, 0.70) * filaments * (0.15 + 0.65 * swirl);
col += c_high * stars * (0.10 + 0.60 * flare_gain);
col += mix(c_low, c_high, 0.55) * edge_flare;
}