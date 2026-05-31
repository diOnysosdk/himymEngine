// @shader_id 23
// @name neon_web
// @editor_dyn dyn0 | Net rotation and drift.
// @editor_dyn dyn1 | Current flow speed.
// @editor_dyn dyn2 | Web density.
// @editor_dyn dyn3 | Node pulse intensity.
else if (id == 23) {
float d0 = dynBiDZ(dyn.x, 0.06);
float d1 = dynBiDZ(dyn.y, 0.08);
float d2 = dynBiDZ(dyn.z, 0.06);
float d3 = dynBiDZ(dyn.w, 0.06);

vec2 q = (uv - 0.5) * rot(d0 * 0.9) + 0.5;
float flow = st * mix(0.25, 1.8, 0.5 + 0.5 * d1);
float dens = mix(5.5, 15.0, 0.5 + 0.5 * d2);
vec2 gv = q * dens + vec2(flow * 0.28, -flow * 0.19);

vec2 id = floor(gv);
vec2 f = fract(gv) - 0.5;

float min_d = 10.0;
float second_d = 10.0;
for (int j = -1; j <= 1; ++j) {
for (int i = -1; i <= 1; ++i) {
vec2 o = vec2(float(i), float(j));
vec2 h = id + o;
vec2 pnt = o + vec2(hash(h + vec2(3.1, 7.7)), hash(h + vec2(11.3, 2.9))) - 0.5;
float d = length(f - pnt);
if (d < min_d) {
second_d = min_d;
min_d = d;
} else if (d < second_d) {
second_d = d;
}
}
}

float edge = second_d - min_d;
float web = smoothstep(0.10, 0.0, edge);

float node = smoothstep(0.22, 0.0, min_d);
float pulse = 0.5 + 0.5 * sin(flow * 4.5 + id.x * 0.7 + id.y * 1.1);
float node_boost = max(0.0, d3);

vec3 bg = mix(c_low * 0.14, c_low * 0.26 + c_high * 0.06, smoothstep(0.0, 1.0, uv.y));
vec3 web_col = mix(c_low * 0.30, c_high, web);
vec3 node_col = c_high * (0.25 + 1.05 * node_boost) * node * pulse;

col = bg;
col += web_col * web * 0.95;
col += node_col;
}
