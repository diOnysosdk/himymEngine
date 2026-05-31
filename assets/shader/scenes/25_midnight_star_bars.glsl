// @shader_id 25
// @name midnight_star_bars
// @editor_dyn dyn0 | Starfield direction angle.
// @editor_dyn dyn1 | Starfield scroll speed.
// @editor_dyn dyn2 | Red edge pulse strength.
// @editor_dyn dyn3 | Top/bottom bar height.
else if (id == 25) {
float dir_angle = dynBiDZ(dyn.x, 0.06) * 3.14159265;
vec2 dir = vec2(cos(dir_angle), sin(dir_angle));
float speed_bi = dynBiDZ(dyn.y, 0.08);
float speed_neg = max(0.0, -speed_bi);
float speed_pos = max(0.0, speed_bi);
float speed_mul = 1.0;
speed_mul = mix(speed_mul, 0.18, speed_neg);
speed_mul = mix(speed_mul, 2.20, pow(speed_pos, 1.60));
float star_speed = speed_mul * (0.25 + abs(spd));
float pulse_raw = abs(dynBi(dyn.z));
float pulse_deadzone = 0.10;
float pulse_t = clamp((pulse_raw - pulse_deadzone) / (1.0 - pulse_deadzone), 0.0, 1.0);
float pulse_amt = smoothstep(0.0, 1.0, pulse_t);
float bar_shape = dynBiDZ(dyn.w, 0.08);
float bar_height = (1.0 / 6.0) * (1.0 + 0.24 * bar_shape);
vec3 deep_blue = mix(vec3(0.005, 0.010, 0.020), c_low, 0.35);
vec3 bar_blue = mix(c_low * 1.10, c_high * 0.55, 0.55);
vec3 red_line_color = vec3(1.0, 0.07, 0.05);
col = mix(deep_blue * 0.7, deep_blue, smoothstep(0.0, 1.0, uv.y));
float stars = 0.0;
for (int layer = 0; layer < 3; ++layer) {
float lf = float(layer);
float density = 110.0 + lf * 70.0;
vec2 suv = uv * density;
vec2 scroll = dir * st * star_speed * (0.45 + lf * 0.55);
vec2 g = suv + scroll;
vec2 cell = floor(g);
vec2 f = fract(g) - 0.5;
float rnd = hash(cell + vec2(19.13 * lf, 7.71 * lf));
float gate = step(0.992 - lf * 0.006, rnd);
float radius = mix(120.0, 36.0, lf / 2.0);
float point = exp(-dot(f, f) * radius);
float phase01 = hash(cell + vec2(3.17 + 11.0 * lf, 9.43 + 5.0 * lf));
float rate = mix(0.06, 0.24, hash(cell + vec2(23.1 + lf * 2.0, 41.7 + lf * 3.0)));
float amp = mix(0.55, 1.00, hash(cell + vec2(61.2 + lf * 4.0, 17.6 + lf * 6.0)));
float tw = fract(st * rate + phase01);
float tri = 1.0 - abs(tw * 2.0 - 1.0);
float twinkle = 0.20 + 0.80 * smoothstep(0.08, 0.92, tri) * amp;
stars += gate * point * twinkle;
}
col += stars * vec3(0.60, 0.76, 1.0);
float top_mask = step(1.0 - bar_height, uv.y);
float bottom_mask = step(uv.y, bar_height);
col = mix(col, bar_blue, max(top_mask, bottom_mask));
float top_edge_dist = abs(uv.y - (1.0 - bar_height));
float bottom_edge_dist = abs(uv.y - bar_height);
float line_core_top = smoothstep(0.010, 0.0, top_edge_dist);
float line_core_bottom = smoothstep(0.010, 0.0, bottom_edge_dist);
float pulse_phase = st * (0.6 + pulse_amt * 4.0);
float wave = pow(max(0.0, sin(pulse_phase + uv.x * 28.0)), 5.0) * pulse_amt;
float emitters = 0.0;
for (int i = 0; i < 4; ++i) {
float fi = float(i);
float center_x = fract(0.19 * fi + pulse_phase * 0.09);
float spread = abs(uv.x - center_x);
emitters += exp(-spread * spread * (260.0 - pulse_amt * 120.0));
}
emitters *= pulse_amt;
float edge_energy = 0.25 + wave * (0.65 + pulse_amt * 0.7) + emitters * (0.35 + pulse_amt * 0.9);
vec3 edge_col = red_line_color * edge_energy;
col += edge_col * line_core_top;
col += edge_col * line_core_bottom;
}
