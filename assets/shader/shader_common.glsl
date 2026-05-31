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
