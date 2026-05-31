// @shader_id 17
// @name starfield_cinematic
// @editor_dyn dyn0 | Flight direction angle.
// @editor_dyn dyn1 | Hyper-speed factor.
// @editor_dyn dyn2 | Star density and depth.
// @editor_dyn dyn3 | Twinkle and streak intensity.
else if (id == 17) {
float d0 = dynBiDZ(dyn.x, 0.06);
float d1 = dynBiDZ(dyn.y, 0.08);
float d2 = dynBiDZ(dyn.z, 0.08);
float d3 = dynBiDZ(dyn.w, 0.08);

float dir_angle = d0 * 3.14159265;
vec2 dir = vec2(cos(dir_angle), sin(dir_angle));
float speed = (0.25 + abs(spd)) * mix(0.45, 2.2, 0.5 + 0.5 * d1);
float density_gain = 1.0 + max(0.0, d2) * 0.55;
float sparkle = max(0.0, d3);

vec3 sky = mix(c_low * 0.10, c_high * 0.28, smoothstep(0.0, 1.0, uv.y));
float neb = fbm(uv * vec2(2.3, 1.4) + vec2(st * 0.03, -st * 0.02));
float neb2 = fbm(uv * vec2(4.2, 2.1) - vec2(st * 0.05, st * 0.03));
sky += mix(c_low, c_high, 0.55) * (0.10 * neb + 0.06 * neb2);

float stars = 0.0;
for (int layer = 0; layer < 3; ++layer) {
float lf = float(layer);
float depth = 0.45 + 0.55 * (lf / 2.0);
float density = (90.0 + 80.0 * lf) * density_gain;
vec2 g = uv * density + dir * st * speed * depth;
vec2 cell = floor(g);
vec2 f = fract(g) - 0.5;
float rnd = hash(cell + vec2(17.3 * lf, 43.9 * lf));
float gate = step(0.992 - 0.008 * lf - 0.004 * max(0.0, d2), rnd);
float rad = mix(110.0, 34.0, lf / 2.0);
float glow = exp(-dot(f, f) * rad);
float tw = 0.35 + 0.65 * sin(st * (0.7 + 0.8 * lf) + rnd * 9.7);
tw = mix(tw, pow(max(tw, 0.0), 1.7), sparkle);
stars += gate * glow * tw;
}

float warp = max(0.0, d1) * (0.45 + 0.55 * sparkle);
vec2 p2 = uv - 0.5;
float r2 = length(p2);
float a2 = atan(p2.y, p2.x);
float streak = pow(max(0.0, cos(a2 - dir_angle)), 18.0) * smoothstep(0.7, 0.0, r2);
stars += streak * warp * 0.8;

col = sky + stars * (c_high * 1.1 + vec3(0.12, 0.16, 0.22));
}
