// @shader_id 19
// @name glitch_scan
// @editor_dyn dyn0 | Horizontal tear direction bias.
// @editor_dyn dyn1 | Glitch event rate.
// @editor_dyn dyn2 | Block corruption amount.
// @editor_dyn dyn3 | RGB split intensity.
else if (id == 19) {
float d0 = dynBiDZ(dyn.x, 0.06);
float d1 = dynBiDZ(dyn.y, 0.08);
float d2 = dynBiDZ(dyn.z, 0.06);
float d3 = dynBiDZ(dyn.w, 0.06);

float event_rate = mix(0.12, 1.9, 0.5 + 0.5 * d1);
float block_amt = max(0.0, d2);
float rgb_split = max(0.0, d3);

vec2 q = uv;
float line_id = floor(uv.y * 220.0);
float scan = 0.85 + 0.15 * sin(uv.y * 850.0 + st * 5.0);

float event = step(0.90 - 0.20 * event_rate, hash(vec2(floor(st * 8.0), 11.2)));
float tear_gate = step(0.85, hash(vec2(line_id, floor(st * 5.0))));
float tear = event * tear_gate * (hash(vec2(line_id, 73.1)) - 0.5) * (0.12 + 0.20 * abs(d0));
q.x += tear;

float by = floor(uv.y * (28.0 + 36.0 * block_amt));
float bx = floor((uv.x + st * 0.09) * (40.0 + 45.0 * block_amt));
float block_noise = hash(vec2(bx, by + floor(st * 12.0)));
float block_gate = step(0.88 - 0.18 * block_amt, block_noise);
float block_shift = (hash(vec2(by, floor(st * 9.0))) - 0.5) * 0.18 * block_gate;
q.x += block_shift;

float base_noise = fbm(vec2(q.x * 9.0 + st * 0.35, q.y * 38.0 - st * 0.18));
vec3 base = mix(c_low * 0.45, c_high * 0.95, base_noise);

float split = rgb_split * 0.012;
float nr = fbm(vec2((q.x + split) * 9.4 + st * 0.39, q.y * 39.0));
float ng = fbm(vec2(q.x * 9.1 + st * 0.35, q.y * 38.0));
float nb = fbm(vec2((q.x - split) * 8.8 + st * 0.31, q.y * 37.2));
vec3 ch = vec3(nr, ng, nb);

float bar = step(0.96, hash(vec2(floor(st * 14.0), floor(uv.y * 12.0))));
float bar_mask = smoothstep(0.45, 0.55, sin((uv.y + st * 0.7) * 18.0));
vec3 accent = c_high * bar * bar_mask * 0.45;

col = mix(base, ch, 0.45 + 0.35 * rgb_split);
col *= scan;
col += accent;
}
