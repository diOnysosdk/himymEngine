#version 330 compatibility
uniform vec2 resolution;
uniform float time;
uniform float exposure;
uniform float fade;
uniform int scene_a_id;
uniform int scene_b_id;
uniform float scene_blend;
uniform vec3 a_palette_low, a_palette_mid, a_palette_high;
uniform float a_speed, a_intensity, a_warp;
uniform float a_dyn0, a_dyn1, a_dyn2, a_dyn3;
uniform vec3 b_palette_low, b_palette_mid, b_palette_high;
uniform float b_speed, b_intensity, b_warp;
uniform float b_dyn0, b_dyn1, b_dyn2, b_dyn3;
uniform float a_st;
uniform float b_st;
uniform sampler2D u_flag_texture;
uniform float u_flag_scale;
// @editor_dyn_default dyn0 | Global dyn speed multiplier for scene time progression.
// @editor_dyn_default dyn1 | Global drift/motion amount around neutral center at 0.5.
// @editor_dyn_default dyn2 | Global warp strength multiplier.
// @editor_dyn_default dyn3 | Global energy/intensity multiplier.
float hash(vec2 p) {
return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}
float noise(vec2 p) {
vec2 i = floor(p);
vec2 f = fract(p);
vec2 u = f * f * (3.0 - 2.0 * f);
return mix(mix(hash(i + vec2(0,0)), hash(i + vec2(1,0)), u.x),
mix(hash(i + vec2(0,1)), hash(i + vec2(1,1)), u.x), u.y);
}
float fbm(vec2 p) {
float v = 0.0;
float amp = 0.5;
for(int i = 0; i < 4; i++) {
v += amp * noise(p);
p *= 2.15;
amp *= 0.5;
}
return v;
}
mat2 rot(float a) {
float s = sin(a), c = cos(a);
return mat2(c, -s, s, c);
}
float dynBi(float v) {
    return clamp(v, -1.0, 1.0);
}
float dynBiDZ(float v, float deadzone) {
float b = dynBi(v);
float m = abs(b);
if (m <= deadzone) {
return 0.0;
}
float t = (m - deadzone) / (1.0 - deadzone);
return sign(b) * clamp(t, 0.0, 1.0);
}
float dynMul(float v, float minMul, float maxMul) {
float b = dynBi(v);
if (b < 0.0) {
return mix(1.0, minMul, -b);
}
return mix(1.0, maxMul, b);
}
float vignetteMask(vec2 uv) {
vec2 q = uv * (1.0 - uv);
float v = q.x * q.y * 16.0;
return clamp(pow(v, 0.24), 0.0, 1.0);
}
vec3 applyGlobalLook(vec3 col, vec2 uv, float t, vec4 dyn) {
float motion = abs(dynBiDZ(dyn.y, 0.08));
float energy = abs(dynBiDZ(dyn.w, 0.06));
float sat_boost = 1.0 + 0.18 * energy;
float luma = dot(col, vec3(0.299, 0.587, 0.114));
col = mix(vec3(luma), col, sat_boost);
float vign = vignetteMask(uv);
col *= mix(0.78, 1.0, vign);
float grain = hash(gl_FragCoord.xy + vec2(t * 17.0, t * 31.0)) - 0.5;
col += grain * (0.010 + 0.012 * motion);
return max(col, vec3(0.0));
}
// st_in is the CPU-integrated scene time (speed + dyn0-modulated); avoids multiplying
// a changing dyn_speed against a large absolute t which causes visible phase jumps.
vec3 renderScene(int id, vec2 uv, float t, float st_in, vec3 c_low, vec3 c_mid, vec3 c_high, float spd, float ins, float wrp, vec4 dyn) {
float dyn_motion = abs(dynBiDZ(dyn.y, 0.08));
float dyn_warp = dynMul((dynBiDZ(dyn.z, 0.05) + 1.0) * 0.5, 0.40, 2.00);
float dyn_energy = dynMul((dynBiDZ(dyn.w, 0.05) + 1.0) * 0.5, 0.45, 1.45);
float st = st_in;
vec2 drift = vec2(
sin(st * 0.73 + uv.y * 6.2831),
cos(st * 0.57 + uv.x * 6.2831)
) * (0.04 * dyn_motion);
uv += drift;
wrp *= dyn_warp;
ins *= dyn_energy;
vec2 p = uv - 0.5;
float r = length(p);
float a = atan(p.y, p.x);
vec3 col = vec3(0.0);
if (id == -1) {
float palette_energy =
	max(max(c_low.r, c_low.g), c_low.b) +
	max(max(c_mid.r, c_mid.g), c_mid.b) +
	max(max(c_high.r, c_high.g), c_high.b);
if (palette_energy <= 0.0001) {
	// True black preset: no authored color means full black output.
	col = vec3(0.0);
} else {
	// If palette colors are authored, force visible fade even when preset intensity is 0.
	ins = max(ins, 1.0);
	float v = smoothstep(0.0, 1.0, uv.y);
	vec3 grad = mix(c_low, c_mid, smoothstep(0.0, 0.5, v));
	grad = mix(grad, c_high, smoothstep(0.5, 1.0, v));
	col = grad;
}
}
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

else if (id == 1) {
float d0 = dynBiDZ(dyn.x, 0.06);
float d1 = dynBiDZ(dyn.y, 0.08);
float d2 = dynBiDZ(dyn.z, 0.06);
float d3 = dynBiDZ(dyn.w, 0.06);
vec2 auv = (uv - 0.5) * rot(0.35 * d0) + 0.5;
float band_mul = mix(0.75, 1.35, 0.5 + 0.5 * d2);
float sway_mul = mix(0.70, 1.35, 0.5 + 0.5 * d1);
float drift0 = fbm(vec2(auv.x * 2.2 + st * 0.07, st * 0.09));
float drift1 = fbm(vec2(auv.x * 3.8 - st * 0.05, st * 0.06 + 4.0));
float curtain0 = sin(auv.x * ((4.5 + wrp * 2.3) * band_mul) + drift0 * (9.0 + 7.0 * wrp) + st * (0.80 + 0.30 * sway_mul));
float curtain1 = sin(auv.x * ((7.0 + wrp * 1.7) * band_mul) + drift1 * (6.0 + 5.0 * wrp) - st * (0.45 + 0.25 * sway_mul));
float wave = smoothstep(-0.15, 0.88, 0.62 * curtain0 + 0.38 * curtain1);
float sky_falloff = smoothstep(0.95, 0.15, auv.y);
float horizon = smoothstep(0.42, 0.02, abs(auv.y - 0.62));
col = mix(c_low * 0.10, c_high, wave * sky_falloff);
col += c_high * (0.10 + (0.18 + 0.16 * max(0.0, d3)) * wave) * horizon;
}

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

else if (id == 3) {
float v = sin(10.0 * r - st + sin(a * 5.0) * wrp);
col = mix(c_low, c_high, v * 0.5 + 0.5) + (0.05 / r) * c_high;
}

else if (id == 4) {
vec2 g = uv * (10.0 + wrp * 5.0);
vec2 id_g = floor(g);
float n = hash(id_g);
float b = pow(0.5 + 0.5 * sin(st + n * 6.28), 10.0);
float s = smoothstep(0.45, 0.4, max(abs(fract(g.x)-0.5), abs(fract(g.y)-0.5)));
col = mix(c_low * 0.2, c_high, s * (0.2 + b));
}

else if (id == 5) {
float rays = sin(a * 20.0 + st + fbm(p * 10.0) * 5.0);
col = mix(c_low, c_high, rays * 0.5 + 0.5) * smoothstep(0.8, 0.1, r) + (0.02 / r) * c_high;
}

else if (id == 6) {
float d0 = dynBiDZ(dyn.x, 0.06);
float d1 = dynBiDZ(dyn.y, 0.08);
float d2 = dynBiDZ(dyn.z, 0.06);
float d3 = dynBiDZ(dyn.w, 0.06);

float wind = mix(-0.8, 0.8, 0.5 + 0.5 * d0);
float motion = mix(0.35, 1.9, 0.5 + 0.5 * d1);
float strike = max(0.0, d2);
float rain = max(0.0, d3);

vec2 q = uv;
q.x += q.y * 0.20 * wind;
q += vec2(st * 0.03 * wind, -st * 0.06 * motion);

float c0 = fbm(q * vec2(2.2, 1.0) + vec2(st * 0.03, 0.0));
float c1 = fbm(q * vec2(4.6, 1.8) - vec2(st * 0.06, st * 0.03));
float cloud = smoothstep(0.28, 0.92, c0 * 0.62 + c1 * 0.38);

vec3 sky = mix(c_low * 0.16, c_high * 0.36, smoothstep(0.0, 1.0, uv.y));
vec3 storm = mix(c_low * 0.45, c_high * 0.85, cloud);
col = mix(sky, storm, smoothstep(0.22, 0.95, uv.y));

float bolt = 0.0;
for (int i = 0; i < 2; ++i) {
float fi = float(i);
float tcell = floor(st * (3.0 + fi * 1.5));
float xseed = hash(vec2(tcell, 17.0 + fi * 13.0));
float trig = step(0.95 - strike * 0.18, hash(vec2(tcell, 33.0 + fi * 9.0)));
float x0 = xseed;
float path = x0 + sin(uv.y * (10.0 + fi * 4.0) + st * (2.5 + fi)) * (0.03 + 0.05 * strike);
float width = 0.004 + 0.008 * strike;
float line = smoothstep(width, 0.0, abs(uv.x - path));
float fade = smoothstep(0.05, 0.95, uv.y);
bolt += trig * line * fade;
}

float rain_col = 0.0;
if (rain > 0.0) {
vec2 rp = vec2(uv.x * 160.0 + wind * 20.0, uv.y * 60.0 - st * (8.0 + 8.0 * motion));
vec2 rc = floor(rp);
float rr = hash(rc);
float gate = step(0.90 - 0.20 * rain, rr);
float fx = abs(fract(rp.x) - 0.5);
rain_col = gate * smoothstep(0.08, 0.0, fx) * smoothstep(0.1, 1.0, uv.y);
}

col += c_high * bolt * (0.75 + 0.85 * strike);
col += c_high * rain_col * (0.08 + 0.16 * rain);
}

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

else if (id == 8) {
float h = fbm(vec2(uv.x * 2.0 + st * 0.5, uv.y * 0.2));
col = mix(c_low, c_high, smoothstep(0.4, 0.6, sin(uv.x * 5.0 + h * 5.0 + uv.y * 2.0)));
}

else if (id == 9) {
float f = sin(uv.x * 50.0 + st) * sin(uv.y * 50.0 + st * 1.2);
col = mix(c_low, c_high, f * 0.5 + 0.5);
}

else if (id == 10) {
float d0 = dynBiDZ(dyn.x, 0.06);
float d1 = dynBiDZ(dyn.y, 0.08);
float d2 = dynBiDZ(dyn.z, 0.06);
float d3 = dynBiDZ(dyn.w, 0.06);

vec2 center = vec2(0.5 + 0.16 * sin(st * 0.45 + d0 * 2.0), 0.52 + 0.10 * cos(st * 0.33));
vec2 q = uv - center;
float rr = length(q);
float ang = atan(q.y, q.x);

float turb_speed = mix(0.45, 2.2, 0.5 + 0.5 * d1);
float sharp = mix(0.75, 2.10, 0.5 + 0.5 * d2);
float emb = max(0.0, d3);

float n0 = fbm(vec2(ang * 2.4, rr * 7.2 - st * 0.9 * turb_speed));
float n1 = fbm(vec2(ang * 5.8 + st * 0.5, rr * 11.0 - st * 1.6 * turb_speed));
float tongue = pow(max(0.0, 1.0 - rr * (2.1 + 0.6 * n0) + n1 * 0.32), sharp);

float core = smoothstep(0.30, 0.00, rr);
float shell = smoothstep(0.58, 0.12, rr + n0 * 0.12);

vec3 deep = mix(c_low * 0.18 + vec3(0.05, 0.01, 0.00), c_low * 0.55 + vec3(0.20, 0.07, 0.01), shell);
vec3 hot = mix(vec3(1.0, 0.38, 0.08), vec3(1.0, 0.86, 0.42), core);

col = mix(deep, hot, core);
col += c_high * tongue * 0.40;

if (emb > 0.0) {
vec2 ep = q * 120.0 + vec2(st * 22.0, -st * 13.0);
vec2 ec = floor(ep);
vec2 ef = fract(ep) - 0.5;
float rnd = hash(ec + vec2(17.0, 5.0));
float gate = step(0.994 - 0.012 * emb, rnd);
float spark = exp(-dot(ef, ef) * 70.0);
float outside = smoothstep(0.10, 0.65, rr);
col += vec3(1.0, 0.78, 0.35) * gate * spark * outside * (0.20 + 0.90 * emb);
}
}

else if (id == 11) {
float swirl = sin(a * 5.0 + r * 20.0 - st);
col = mix(c_low, c_high, swirl * 0.5 + 0.5);
}

else if (id == 12) {
float n1 = fbm(uv * 3.0 + st * 0.2);
float n2 = fbm(uv * 6.0 - st * 0.1);
col = mix(c_low, c_high, pow(n1 * n2, 0.5) * 2.0);
}

else if (id == 13) {
float sweep = fract(a / 6.28 + st * 0.2);
float rings = step(0.48, fract(r * 10.0));
col = c_high * (1.0 - sweep) * (rings + 0.2);
}

else if (id == 14) {
vec2 uv2 = uv * rot(0.785); // 45 grader
float d = max(abs(fract(uv2.x * 10.0)-0.5), abs(fract(uv2.y * 10.0)-0.5));
col = mix(c_low, c_high, smoothstep(0.45, 0.5, d));
}

else if (id == 15) {
float z = sin(1.0 / (r + 0.01) * 5.0 + st);
col = mix(c_low, c_high, step(0.0, z));
}

else if (id == 16) {
float line = abs(sin(uv.x * 20.0 + fbm(uv * 5.0) * 5.0)) * abs(sin(uv.y * 20.0 + st));
col = c_high * pow(1.0 - line, 10.0);
}

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

else if (id == 18) {
float d0 = dynBiDZ(dyn.x, 0.06);
float d1 = dynBiDZ(dyn.y, 0.08);
float d2 = dynBiDZ(dyn.z, 0.06);
float d3 = dynBiDZ(dyn.w, 0.06);

vec2 q = p;
float spin = st * mix(0.08, 0.62, 0.5 + 0.5 * d1);
q *= rot(spin);

float seg = mix(4.0, 14.0, 0.5 + 0.5 * d0);
float ang = atan(q.y, q.x);
float rad = length(q) + 0.0001;

float sector = 6.2831853 / max(2.0, seg);
ang = abs(mod(ang + sector * 0.5, sector) - sector * 0.5);

vec2 k = vec2(cos(ang), sin(ang)) * rad;
float warp = mix(0.10, 0.75, 0.5 + 0.5 * d2);
k += vec2(
	fbm(k * 3.0 + vec2(st * 0.18, -st * 0.12)),
	fbm(k.yx * 3.4 + vec2(-st * 0.14, st * 0.20))
) * warp * wrp * 0.45;

float f0 = fbm(k * (4.0 + wrp * 1.5));
float f1 = fbm(k * (7.0 + wrp * 2.1) + vec2(st * 0.23, -st * 0.17));
float rings = 0.5 + 0.5 * sin(rad * (26.0 + 10.0 * wrp) - st * (1.4 + abs(d1)) + f1 * 2.7);
float blend = smoothstep(0.25, 0.95, f0 * 0.65 + f1 * 0.35);

vec3 base = mix(c_low * 0.30, c_high * 0.95, blend);
base += c_high * rings * 0.20;

float glow = smoothstep(0.82, 1.0, blend) * mix(0.15, 0.70, 0.5 + 0.5 * d3);
base += c_high * glow;

col = base;
}

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

else if (id == 20) {
float rip = sin(r * 40.0 - st * 2.0 + fbm(uv * 10.0) * wrp);
col = mix(c_low, c_high, rip * 0.5 + 0.5);
}

else if (id == 21) {
float d = abs(uv.y - 0.5 + sin(uv.x * 10.0 + st) * 0.2);
col = c_high * (0.02 / d);
}

else if (id == 22) {
float d0 = dynBiDZ(dyn.x, 0.06);
float d1 = dynBiDZ(dyn.y, 0.08);
float d2 = dynBiDZ(dyn.z, 0.06);
float d3 = dynBiDZ(dyn.w, 0.06);

vec2 q = (uv - 0.5) * rot(d0 * 0.8) + 0.5;
float dens = mix(8.0, 24.0, 0.5 + 0.5 * d2);
float tm = st * mix(0.35, 1.85, 0.5 + 0.5 * d1);

vec2 hp = q * dens;
hp.x += hp.y * 0.57735026919;
vec2 cell = floor(hp);
vec2 f = fract(hp) - 0.5;

float idn = hash(cell);
float pulse = 0.5 + 0.5 * sin(tm * (0.8 + idn * 1.2) + idn * 10.0);

float edge = 1.0 - max(abs(f.x), abs(f.y + f.x * 0.5));
edge = smoothstep(0.14, 0.34, edge);

float sweep = sin((q.x + q.y) * 20.0 - tm * 2.2 + idn * 6.28);
float sweep_mask = smoothstep(0.35, 0.95, 0.5 + 0.5 * sweep);

vec3 bg = mix(c_low * 0.20, c_low * 0.45 + c_high * 0.08, 0.5 + 0.5 * q.y);
vec3 grid = mix(c_low * 0.55, c_high, edge * (0.45 + 0.55 * pulse));
vec3 hi = c_high * sweep_mask * edge * (0.16 + 0.30 * max(0.0, d3));

col = mix(bg, grid, edge);
col += hi;
}

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

else if (id == 24) {
float d0 = dynBiDZ(dyn.x, 0.06);
float d1 = dynBiDZ(dyn.y, 0.08);
float d2 = dynBiDZ(dyn.z, 0.06);
float d3 = dynBiDZ(dyn.w, 0.06);

float rr = max(r, 0.003);
float z = 1.0 / rr;
float flow = st * mix(0.65, 1.85, 0.5 + 0.5 * d1);
float spin = (0.11 + 0.05 * d0) * mix(0.75, 1.55, wrp);
float spiral = a + z * spin;

float band0 = 0.5 + 0.5 * sin(spiral * 6.0 - flow * 1.5 + fbm(vec2(spiral * 1.7, z * 0.10)) * 2.0);
float band1 = 0.5 + 0.5 * sin(spiral * 10.0 - flow * 2.1 + fbm(vec2(spiral * 2.4, z * 0.16)) * 2.6);
float band2 = 0.5 + 0.5 * sin(spiral * 16.0 - flow * 2.9 + fbm(vec2(spiral * 3.1, z * 0.22)) * 3.1);
float bands = band0 * 0.45 + band1 * 0.35 + band2 * 0.20;

float split = mix(1.0, 1.24, 0.5 + 0.5 * d2);
float texR = fbm(vec2(spiral * 3.0, z - flow * split));
float texG = fbm(vec2(spiral * 3.0, z - flow * (1.05 * split)));
float texB = fbm(vec2(spiral * 3.0, z - flow * (1.10 * split)));

vec3 vortex = vec3(texR, texG, texB);
vec3 tunnel_col = mix(c_low * 0.06, c_high, vortex * 0.72 + bands * 0.28);

float ring = smoothstep(0.22, 0.06, abs(rr - 0.10));
float horizon = smoothstep(0.0, 1.0, 0.018 / rr);
float bloom = mix(0.85, 2.20, 0.5 + 0.5 * d3);

tunnel_col += c_high * ring * 0.22;
tunnel_col += c_high * horizon * (1.25 * bloom);
tunnel_col *= smoothstep(0.0, 0.62, rr);

col = tunnel_col;
}

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

else if (id == 28) {
float d0 = dynBiDZ(dyn.x, 0.06);
float d1 = dynBiDZ(dyn.y, 0.08);
float d2 = dynBiDZ(dyn.z, 0.06);
float d3 = dynBiDZ(dyn.w, 0.06);

float fly_speed     = mix(0.55, 2.20, 0.5 + 0.5 * d1);
float dune_detail   = mix(0.85, 1.90, 0.5 + 0.5 * d2);
float cactus_boost  = max(0.0, d3);
float bank          = d0 * 0.22;

vec2 p = uv * 2.0 - 1.0;
p.x *= 1.25;
p.y += 0.03 * sin(st * 0.45 + p.x * 2.0);

vec3 ro = vec3(bank * 2.2, 0.78 + 0.06 * sin(st * 0.30), st * fly_speed * 5.2);
vec3 ta = ro + vec3(bank * 1.2, -0.13 - 0.03 * abs(d0), 1.9);

vec3 ww = normalize(ta - ro);
vec3 uu = normalize(vec3(ww.z, 0.0, -ww.x));
vec3 vv = normalize(cross(uu, ww));

float lens = 1.45;
vec3 rd = normalize(uu * p.x + vv * (p.y + bank * 0.10) + ww * lens);

vec3 sky_bottom = c_low * 0.30 + vec3(0.11, 0.09, 0.10);
vec3 sky_top    = c_high * 1.12 + vec3(0.22, 0.11, 0.03);
float sky_t     = clamp(0.5 + 0.5 * rd.y, 0.0, 1.0);
vec3 sky        = mix(sky_bottom, sky_top, sky_t);

vec3 sun_dir = normalize(vec3(0.55, 0.22, 0.75));
float sun_a = max(0.0, dot(rd, sun_dir));
sky += c_high * pow(sun_a, 28.0) * 0.32;
sky += c_high * pow(sun_a, 220.0) * 0.85;

float t = 0.0;
float hit = 0.0;
vec3 pos = ro;
float terr = 0.0;

for (int i = 0; i < 28; ++i) {
    pos = ro + rd * t;

    vec2 q = pos.xz;
    vec2 q0 = q * vec2(0.030, 0.045);
    vec2 q1 = q * vec2(0.070, 0.090);
    vec2 q2 = q * vec2(0.180, 0.130);

    float n0 = fbm(q0 + vec2(0.0, st * 0.020));
    float n1 = fbm(q1 + vec2(1.7, st * 0.045));
    float n2 = fbm(q2 + vec2(3.3, st * 0.080));

    float ridge = sin(q.x * 0.060 + n1 * 2.2 + q.y * 0.018);
    ridge += 0.5 * sin(q.x * 0.115 - q.y * 0.030 + n2 * 3.0);
    ridge *= 0.5;

    terr =
          0.04
        + n0 * 0.16
        + n1 * 0.10 * dune_detail
        + abs(ridge) * (0.18 + 0.22 * dune_detail);

    float h = pos.y - terr;
    if (h < 0.015) {
        hit = 1.0;
        break;
    }

    t += clamp(h * 0.65, 0.035, 0.38);
    if (t > 18.0) break;
}

if (hit < 0.5) {
    float far_haze = exp(-max(0.0, rd.y + 0.02) * 16.0);
    col = sky + (c_high * 0.18 + c_low * 0.05) * far_haze * 0.18;
} else {
    vec2 q = pos.xz;

    vec2 ex = vec2(0.12, 0.0);
    vec2 ez = vec2(0.0, 0.12);

    vec2 qx0 = (q + ex) * vec2(0.030, 0.045);
    vec2 qx1 = (q + ex) * vec2(0.070, 0.090);
    vec2 qx2 = (q + ex) * vec2(0.180, 0.130);
    float nx0 = fbm(qx0 + vec2(0.0, st * 0.020));
    float nx1 = fbm(qx1 + vec2(1.7, st * 0.045));
    float nx2 = fbm(qx2 + vec2(3.3, st * 0.080));
    float rx = sin((q.x + ex.x) * 0.060 + nx1 * 2.2 + (q.y + ex.y) * 0.018);
    rx += 0.5 * sin((q.x + ex.x) * 0.115 - (q.y + ex.y) * 0.030 + nx2 * 3.0);
    rx *= 0.5;
    float hx = 0.04 + nx0 * 0.16 + nx1 * 0.10 * dune_detail + abs(rx) * (0.18 + 0.22 * dune_detail);

    vec2 qz0 = (q + ez) * vec2(0.030, 0.045);
    vec2 qz1 = (q + ez) * vec2(0.070, 0.090);
    vec2 qz2 = (q + ez) * vec2(0.180, 0.130);
    float nz0 = fbm(qz0 + vec2(0.0, st * 0.020));
    float nz1 = fbm(qz1 + vec2(1.7, st * 0.045));
    float nz2 = fbm(qz2 + vec2(3.3, st * 0.080));
    float rz = sin((q.x + ez.x) * 0.060 + nz1 * 2.2 + (q.y + ez.y) * 0.018);
    rz += 0.5 * sin((q.x + ez.x) * 0.115 - (q.y + ez.y) * 0.030 + nz2 * 3.0);
    rz *= 0.5;
    float hz = 0.04 + nz0 * 0.16 + nz1 * 0.10 * dune_detail + abs(rz) * (0.18 + 0.22 * dune_detail);

    vec3 n = normalize(vec3(terr - hx, 0.12, terr - hz));

    float diff = clamp(dot(n, sun_dir), 0.0, 1.0);
    float back = clamp(0.35 + 0.65 * dot(n, normalize(vec3(-sun_dir.x, 0.0, -sun_dir.z))), 0.0, 1.0);

    vec3 sand_dark  = mix(c_low * 0.40, c_high * 0.36, 0.30);
    vec3 sand_light = mix(c_low * 0.72, c_high * 0.92, 0.55);
    vec3 terrain_col = mix(sand_dark, sand_light, diff * 0.82 + 0.18);
    terrain_col *= 0.78 + 0.22 * back;

    float ridge_mask = smoothstep(0.28, 0.82, abs(
        sin(q.x * 0.090 + fbm(q * 0.10) * 2.2 + q.y * 0.035)
    ));
    terrain_col *= 1.0 + ridge_mask * 0.08;

    float dist = t;
    float haze = 1.0 - exp(-dist * 0.10);
    vec3 dust_col = c_high * 0.58 + c_low * 0.18;
    terrain_col = mix(terrain_col, dust_col, haze * 0.45);

    float cactus = 0.0;
    vec2 cell = floor(q * vec2(0.095, 0.060));
    vec2 gv = fract(q * vec2(0.095, 0.060)) - 0.5;

    for (int k = 0; k < 3; ++k) {
        vec2 cid = cell + vec2(float(k) - 1.0, 0.0);
        float rnd = hash(cid + vec2(11.3, 7.1));
        float spawn = step(0.90 - cactus_boost * 0.22, rnd);

        vec2 off = vec2(hash(cid + 2.7), hash(cid + 5.9)) - 0.5;
        vec2 local = gv - vec2(float(k) - 1.0, 0.0) - off * vec2(0.55, 0.18);

        float hgt = mix(0.22, 0.52, hash(cid + 8.2));
        float wid = mix(0.030, 0.055, hash(cid + 4.4));

        float trunk = smoothstep(wid, 0.0, abs(local.x)) *
                      smoothstep(-0.02, 0.05, local.y + hgt * 0.06) *
                      (1.0 - smoothstep(hgt * 0.72, hgt, local.y + hgt * 0.06));

        float arm_y = hgt * mix(0.38, 0.60, hash(cid + 9.7));
        float arm_l = smoothstep(0.028, 0.0, length(vec2(local.x + 0.07, local.y - arm_y) * vec2(1.8, 0.85)));
        float arm_r = smoothstep(0.028, 0.0, length(vec2(local.x - 0.07, local.y - arm_y * 0.92) * vec2(1.8, 0.85)));

        cactus += spawn * max(trunk, max(arm_l, arm_r));
    }

    cactus *= smoothstep(7.5, 1.8, dist);
    vec3 cactus_col = c_low * 0.16 + c_high * 0.05;
    terrain_col = mix(terrain_col, cactus_col, clamp(cactus, 0.0, 1.0));

    float shimmer_band = smoothstep(0.0, 0.18, pos.y) * (1.0 - smoothstep(0.18, 0.65, pos.y));
    float shimmer = fbm(vec2(q.x * 0.20 + st * 1.8, q.y * 0.06 - st * 0.4));
    terrain_col += c_high * shimmer * shimmer_band * 0.10;

    float dust = fbm(vec2(q.x * 0.08 + st * fly_speed * 1.1, q.y * 0.03 - st * 0.25));
    terrain_col += dust_col * dust * haze * (0.08 + cactus_boost * 0.10);

    float fres = pow(clamp(1.0 + dot(rd, n), 0.0, 1.0), 3.0);
    terrain_col += c_high * fres * 0.05;

    col = terrain_col;
}
}

else if (id == 29) {
float d0 = dynBiDZ(dyn.x, 0.06);
float d1 = dynBiDZ(dyn.y, 0.08);
float d2 = dynBiDZ(dyn.z, 0.06);
float d3 = dynBiDZ(dyn.w, 0.06);

vec2 guv = uv;
guv.x += d0 * 0.12 * (uv.y - 0.5);

float middle_pos = clamp(0.50 + d1 * 0.18, 0.18, 0.82);
float middle_softness = mix(0.12, 0.38, 0.5 + 0.5 * d2);

vec3 bottom_col = c_low * 0.95 + vec3(0.02, 0.01, 0.00);
vec3 middle_col = c_mid;
middle_col += vec3(0.08, 0.04, 0.02) * max(0.0, (ins - 1.0) + d3);
vec3 top_col = c_high * 1.05;

float to_middle = smoothstep(middle_pos - middle_softness, middle_pos, guv.y);
float from_middle = smoothstep(middle_pos, middle_pos + middle_softness, guv.y);

vec3 band_mix = mix(bottom_col, middle_col, to_middle);
col = mix(band_mix, top_col, from_middle);

float vign = uv.x * uv.y * (1.0 - uv.x) * (1.0 - uv.y);
col *= 0.88 + 0.12 * pow(clamp(vign * 18.0, 0.0, 1.0), 0.35);
}

else if (id == 30) {
float d0 = dynBiDZ(dyn.x, 0.06);
float d1 = dynBiDZ(dyn.y, 0.08);
float d2 = dynBiDZ(dyn.z, 0.06);
float d3 = dynBiDZ(dyn.w, 0.06);

float shake_rate = mix(0.70, 2.20, 0.5 + 0.5 * d1);
float shake_amt = mix(0.010, 0.060, 0.5 + 0.5 * abs(d1));
float fluff = mix(0.82, 1.38, 0.5 + 0.5 * d2);
float glow = max(0.0, d3);

vec2 q = uv - 0.5;
float ph = st * shake_rate + d0 * 2.7;
q -= vec2(cos(ph), sin(ph * 1.31 + d0)) * shake_amt;
q = rot(0.18 * sin(ph * 0.73) + d0 * 0.28) * q;

vec2 warp_q = q;
q += vec2(
    fbm(warp_q * 3.3 + vec2(st * 0.10, -st * 0.06)),
    fbm(warp_q.yx * 3.7 - vec2(st * 0.08, st * 0.05))
) * (0.015 + 0.050 * wrp);

float rr = length(q);
float aa = atan(q.y, q.x);
float n0 = fbm(vec2(aa * (4.0 + 2.4 * fluff), rr * 7.0 - st * 0.35));
float n1 = fbm(vec2(aa * (10.0 + 4.5 * fluff) - st * 0.22, rr * 13.0));
float fur = n0 * 0.65 + n1 * 0.35;

float radius = 0.22 + 0.10 * fur * fluff;
float shell = 1.0 - smoothstep(radius, radius + 0.10 + 0.03 * fluff, rr);
float inner = 1.0 - smoothstep(radius - 0.06, radius + 0.01, rr);
float rim = max(shell - inner, 0.0);
float core = 1.0 - smoothstep(0.00, 0.22, rr);

float pulse = 0.5 + 0.5 * sin(st * 6.0 + aa * 10.0 + fur * 4.0);
float strand = smoothstep(0.50, 0.92, fur + 0.10 * pulse) * shell;
float sparkle = step(
    0.988 - 0.010 * glow,
    hash(floor((q + 0.5) * (18.0 + 8.0 * fluff) + vec2(7.0, 13.0)))
);
float aura = (1.0 - smoothstep(radius + 0.05, radius + 0.26, rr)) * (0.12 + 0.22 * glow);

vec3 bg = mix(c_low * 0.08, c_low * 0.15 + c_mid * 0.05, smoothstep(0.0, 1.0, uv.y));
vec3 fur_col = mix(c_low * 0.30, mix(c_mid, c_high, 0.35), smoothstep(0.12, 0.92, fur + core * 0.25));

col = bg;
col += fur_col * shell * (0.65 + 0.35 * ins);
col += c_high * strand * (0.10 + 0.18 * fluff);
col += mix(c_mid, c_high, 0.60) * rim * (0.18 + 0.30 * pulse + 0.35 * glow);
col += c_high * sparkle * strand * (0.12 + 0.50 * glow);
col += mix(c_low, c_mid, 0.50) * aura;
}

else if (id == 31) {
float d0 = dynBiDZ(dyn.x, 0.06);
float d1 = dynBiDZ(dyn.y, 0.06);
float d2 = dynBiDZ(dyn.z, 0.06);
float d3 = dynBiDZ(dyn.w, 0.06);

vec2 q = uv - 0.5;
float rr = length(q);

float pulse_speed = mix(0.65, 1.90, 0.5 + 0.5 * d3);
float reveal_radius = 0.34 + 0.22 * sin(st * pulse_speed + rr * 2.2);
reveal_radius += d0 * 0.12;
float gate_soft = mix(0.06, 0.18, 0.5 + 0.5 * d0);
float reveal_gate = 1.0 - smoothstep(reveal_radius, reveal_radius + gate_soft, rr);

float cell_scale = mix(8.0, 28.0, 0.5 + 0.5 * d1);
vec2 gv = q * cell_scale;
vec2 cell_id = floor(gv);
vec2 cell_uv = fract(gv) - 0.5;

float n = hash(cell_id);
float edge_hard = mix(0.20, 0.05, 0.5 + 0.5 * d1);
float cell_edge = smoothstep(0.48, 0.48 - edge_hard, max(abs(cell_uv.x), abs(cell_uv.y)));

float layer = smoothstep(0.26, 0.78, n + 0.22 * sin(st * (1.1 + 0.5 * d3) + n * 6.2831));
float voxel_mask = cell_edge * layer * reveal_gate;

float plasma = fbm(q * (3.5 + 2.5 * wrp) + vec2(st * 0.30 * pulse_speed, -st * 0.22 * pulse_speed));
float glow_amount = mix(0.03, 0.24, 0.5 + 0.5 * d2);
float glow = glow_amount * plasma * (0.35 + 0.65 * reveal_gate);

vec3 bg = mix(c_low * 0.12, c_mid * 0.22, smoothstep(0.0, 1.0, uv.y));
vec3 voxel_col = mix(c_mid, c_high, 0.45 + 0.45 * n);

col = bg;
col = mix(col, voxel_col, voxel_mask * ins);
col += mix(c_low, c_high, 0.6) * glow;
}
else if (id == 32) {
float d0 = dynBiDZ(dyn.x, 0.06);
float d1 = dynBiDZ(dyn.y, 0.06);
float d2 = dynBiDZ(dyn.z, 0.05);
float d3 = dynBiDZ(dyn.w, 0.05);

vec2 q = uv - 0.5;
q += vec2(d0 * 0.035, d2 * 0.022);
float rr = max(length(q), 0.0015);
float aa = atan(q.y, q.x);

float spin = st * (0.55 + 1.65 * abs(d0) + 0.35 * abs(spd)) + d0 * 5.2;
float horizon = mix(0.085, 0.165, 0.5 + 0.5 * d1);
float lens = mix(0.06, 0.36, 0.5 + 0.5 * abs(d1));
float turb = mix(0.15, 1.15, 0.5 + 0.5 * d2) * (0.40 + 0.60 * wrp);
float jet_energy = max(0.0, d3);

float grav_pull = lens / rr;
vec2 lens_uv = q + normalize(q + vec2(0.0001, 0.0)) * grav_pull * 0.22;

float bg_stars_a = fbm(lens_uv * 12.0 + vec2(st * 0.02, -st * 0.03));
float bg_stars_b = fbm(lens_uv * 21.0 - vec2(st * 0.03, st * 0.01));
float star_gate = smoothstep(0.88, 1.0, bg_stars_a * 0.65 + bg_stars_b * 0.35);

float spiral = aa + spin + grav_pull * (1.8 + 0.7 * sign(d0));
float disk_band = 0.5 + 0.5 * sin(spiral * 8.0 + rr * 44.0 - spin * 1.7);
float disk_noise = fbm(vec2(spiral * 1.8 + d2 * 2.3, rr * 24.0 - spin * 0.8));
float filament = smoothstep(0.38, 0.92, disk_band * 0.58 + disk_noise * 0.42 + turb * 0.08);

float disk_mask_outer = smoothstep(0.52, 0.18, rr);
float disk_mask_inner = smoothstep(horizon + 0.018, horizon + 0.090, rr);
float disk_mask = disk_mask_outer * disk_mask_inner;

float jet_col = abs(q.x) * (0.7 + 0.3 * sin(spin * 0.6));
float jet_profile = exp(-jet_col * (28.0 - 10.0 * jet_energy));
float jet_length = smoothstep(0.16, 0.92, abs(q.y));
float jets = jet_profile * jet_length * (0.12 + 1.05 * jet_energy);

float horizon_core = 1.0 - smoothstep(horizon, horizon + 0.020, rr);
float photon_ring = smoothstep(horizon + 0.018, horizon + 0.002, abs(rr - (horizon + 0.012)));
photon_ring *= 1.0 + 0.35 * max(0.0, d1);

vec3 sky = mix(c_low * 0.06, c_mid * 0.16, smoothstep(0.0, 1.0, uv.y));
sky += c_high * star_gate * 0.45;

vec3 disk_col = mix(c_low * 0.35 + c_mid * 0.25, c_high, filament);
disk_col = mix(disk_col, c_high, max(0.0, d3) * 0.18);
vec3 jet_col_rgb = mix(c_mid, c_high, 0.7);

col = sky;
col += disk_col * disk_mask * (0.55 + 0.55 * ins);
col += jet_col_rgb * jets;
col += c_high * photon_ring * (0.22 + 0.42 * jet_energy);
col = mix(col, vec3(0.0), horizon_core);
}
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
else if (id == 34) {
    // Configurable parameters from dyn controls
    float wave_freq = mix(3.0, 12.0, 0.5 + 0.5 * dynBiDZ(dyn.x, 0.06));
    float wave_amp = mix(0.01, 0.08, 0.5 + 0.5 * dynBiDZ(dyn.y, 0.06));
    float wind_speed = mix(0.8, 3.0, 0.5 + 0.5 * dynBiDZ(dyn.z, 0.06)) * spd;
    float ripple_detail = mix(0.0, 0.5, 0.5 + 0.5 * dynBiDZ(dyn.w, 0.06));
    
    // UV coordinates for flag (flip Y to correct image orientation)
    // Apply scale around center point
    vec2 flag_uv = vec2(uv.x, 1.0 - uv.y);
    flag_uv = (flag_uv - 0.5) / u_flag_scale + 0.5;
    
    // Primary wave motion (horizontal waves traveling vertically)
    float wave_phase = st * wind_speed;
    float wave = sin(flag_uv.x * wave_freq + wave_phase) * wave_amp;
    
    // Secondary ripple (adds detail based on position)
    float ripple = sin(flag_uv.x * wave_freq * 2.5 + flag_uv.y * 8.0 + wave_phase * 1.3) * wave_amp * 0.3 * ripple_detail;
    
    // Apply wave displacement to V coordinate
    flag_uv.y += wave + ripple;
    
    // Add subtle horizontal flutter at the edges (flag edges move more)
    float edge_flutter = pow(flag_uv.x, 2.0) * sin(flag_uv.y * 6.0 + wave_phase * 2.0) * wave_amp * 0.5;
    flag_uv.x += edge_flutter;
    
    // Clamp UV to prevent over-distortion
    flag_uv = clamp(flag_uv, 0.0, 1.0);
    
    // Sample the flag texture
    vec3 tex_col = texture(u_flag_texture, flag_uv).rgb;
    
    // Apply palette tinting and grading
    vec3 tinted = tex_col * mix(c_low, c_high, tex_col.r * 0.5 + 0.5);
    
    // Lighting simulation - flag gets darker in the wave troughs
    float lighting = 1.0 - abs(wave + ripple) * 8.0 * ins;
    lighting = clamp(lighting, 0.7, 1.15);
    
    // Apply intensity modulation
    col = tinted * lighting * ins;
    
    // Add subtle edge glow where the flag curves most
    float curve_intensity = abs(wave + ripple) * 5.0;
    col += c_high * curve_intensity * 0.15 * ins;
    
    // Vignette for depth
    float vign = vignetteMask(uv);
    col *= mix(0.85, 1.0, vign);
}

else if (id == 35) {
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

else if (id == 36) {
    float d0 = dynBiDZ(dyn.x, 0.06);
    float d1 = dynBiDZ(dyn.y, 0.08);
    float d2 = dynBiDZ(dyn.z, 0.06);
    float d3 = dynBiDZ(dyn.w, 0.06);
    
    // Scroll speed control
    float scroll_speed = mix(0.5, 2.5, (d0 + 1.0) * 0.5);
    
    // Bar frequency/count
    float bar_freq = mix(8.0, 24.0, (d1 + 1.0) * 0.5);
    
    // Sine wave amplitude for horizontal motion
    float wave_amp = mix(0.0, 0.15, (d2 + 1.0) * 0.5);
    
    // Bar width/sharpness
    float bar_width = mix(0.3, 0.7, (d3 + 1.0) * 0.5);
    
    // Vertical position with scrolling
    float y_scroll = uv.y + st * scroll_speed * 0.15;
    
    // Horizontal sine wave offset
    float x_offset = sin(y_scroll * bar_freq + st * 1.5) * wave_amp;
    vec2 ruv = vec2(uv.x + x_offset, y_scroll);
    
    // Create repeating bars
    float bar_phase = fract(ruv.y * bar_freq * 0.5);
    float bar = smoothstep(bar_width, bar_width - 0.1, bar_phase) * 
                smoothstep(1.0 - bar_width, 1.0 - bar_width + 0.1, bar_phase);
    
    // Color gradient per bar
    float color_phase = fract(ruv.y * bar_freq * 0.5);
    vec3 bar_color = mix(c_low, c_high, color_phase);
    
    // Edge glow for classic look
    float edge = abs(bar_phase - 0.5) * 2.0;
    float glow = (1.0 - edge) * bar * 0.3;
    
    col = bar_color * (bar + glow) * ins;
}

else if (id == 37) {
    float d0 = dynBiDZ(dyn.x, 0.06);
    float d1 = dynBiDZ(dyn.y, 0.08);
    float d2 = dynBiDZ(dyn.z, 0.06);
    float d3 = dynBiDZ(dyn.w, 0.06);
    
    // Animation speeds
    float anim_speed = mix(0.6, 2.0, (d0 + 1.0) * 0.5);
    float color_speed = mix(0.3, 1.5, (d3 + 1.0) * 0.5);
    
    // Bar parameters
    float bar_freq = mix(6.0, 18.0, (d1 + 1.0) * 0.5);
    float plasma_complexity = mix(0.5, 2.0, (d2 + 1.0) * 0.5);
    
    // Plasma-like motion for bars
    float y_wave = uv.y + 
                   sin(uv.x * 8.0 + st * anim_speed) * 0.08 +
                   cos(uv.x * 5.0 - st * anim_speed * 0.7) * 0.06;
    
    float x_wave = uv.x + 
                   sin(uv.y * 6.0 + st * anim_speed * 1.2) * 0.06 +
                   cos(uv.y * 9.0 - st * anim_speed * 0.9) * 0.04;
    
    // Create plasma-infused bars
    float bar_phase = fract(y_wave * bar_freq);
    float bar = smoothstep(0.4, 0.3, bar_phase) * smoothstep(0.6, 0.7, bar_phase);
    
    // Plasma color gradient within bars
    float plasma1 = sin(x_wave * 12.0 * plasma_complexity + st * color_speed) * 0.5 + 0.5;
    float plasma2 = cos(y_wave * 10.0 * plasma_complexity - st * color_speed * 0.8) * 0.5 + 0.5;
    float plasma_mix = (plasma1 + plasma2) * 0.5;
    
    // Multi-color gradient across bars
    float color_offset = fract(y_wave * bar_freq + st * color_speed * 0.2);
    vec3 color1 = mix(c_low, c_mid, plasma_mix);
    vec3 color2 = mix(c_mid, c_high, plasma_mix);
    vec3 bar_color = mix(color1, color2, color_offset);
    
    // Enhanced glow for plasma feel
    float glow = bar * smoothstep(0.0, 0.3, bar_phase) * smoothstep(1.0, 0.7, bar_phase) * 0.5;
    
    col = bar_color * (bar + glow) * ins;
}

else if (id == 38) {
    float d0 = dynBiDZ(dyn.x, 0.06);
    float d1 = dynBiDZ(dyn.y, 0.08);
    float d2 = dynBiDZ(dyn.z, 0.06);
    float d3 = dynBiDZ(dyn.w, 0.06);
    
    // Animation parameters
    float anim_speed = mix(0.4, 1.8, (d0 + 1.0) * 0.5);
    
    // Blob parameters
    float blob_count = mix(5.0, 12.0, (d1 + 1.0) * 0.5);
    float blob_size = mix(0.08, 0.20, (d2 + 1.0) * 0.5);
    float glow_intensity = mix(0.3, 1.2, (d3 + 1.0) * 0.5);
    
    // Metaball field accumulator
    float metaball_field = 0.0;
    float y_scroll = st * anim_speed * 0.12;
    
    // Create multiple metaball "bars"
    for (float i = 0.0; i < 12.0; i += 1.0) {
        if (i >= blob_count) break;
        
        // Bar position with different sine wave patterns
        float bar_y = fract(i / blob_count + y_scroll);
        float bar_x = 0.5 + sin(st * anim_speed + i * 2.0) * 0.15 +
                           cos(st * anim_speed * 0.7 + i * 1.5) * 0.10;
        
        vec2 blob_pos = vec2(bar_x, bar_y);
        float dist = length(uv - blob_pos);
        
        // Metaball contribution (inverse distance falloff)
        metaball_field += blob_size / (dist + 0.01);
        
        // Add horizontal elongation for bar effect
        float bar_dist = abs(uv.y - bar_y);
        metaball_field += blob_size * 0.5 / (bar_dist * bar_dist + 0.02);
    }
    
    // Threshold to create solid bars with soft edges
    float bar_mask = smoothstep(1.5, 2.5, metaball_field);
    float glow = smoothstep(0.8, 1.5, metaball_field) * (1.0 - bar_mask) * glow_intensity;
    
    // Color based on metaball field density
    float color_mix = fract(metaball_field * 0.3 + st * anim_speed * 0.1);
    vec3 bar_color = mix(c_low, c_high, color_mix);
    vec3 glow_color = mix(c_mid, c_high, color_mix);
    
    // Combine bar and glow
    col = bar_color * bar_mask + glow_color * glow;
    
    // Extra core highlights where blobs merge
    float highlight = smoothstep(3.5, 5.0, metaball_field);
    col += c_high * highlight * 0.6;
    
    col *= ins;
}

col = applyGlobalLook(col * ins, uv, st, dyn);
return col;
}
void main() {
vec2 uv = gl_TexCoord[0].xy;
vec4 dyn_a = vec4(a_dyn0, a_dyn1, a_dyn2, a_dyn3);
vec4 dyn_b = vec4(b_dyn0, b_dyn1, b_dyn2, b_dyn3);
vec3 col_a = renderScene(scene_a_id, uv, time, a_st, a_palette_low, a_palette_mid, a_palette_high, a_speed, a_intensity, a_warp, dyn_a);
vec3 col_b = renderScene(scene_b_id, uv, time, b_st, b_palette_low, b_palette_mid, b_palette_high, b_speed, b_intensity, b_warp, dyn_b);
vec3 finalCol = mix(col_a, col_b, clamp(scene_blend, 0.0, 1.0));
finalCol *= exposure;
finalCol = mix(finalCol, vec3(0.0), fade);
finalCol = finalCol / (vec3(1.0) + finalCol * 0.1);
gl_FragColor = vec4(finalCol, 1.0);
}
