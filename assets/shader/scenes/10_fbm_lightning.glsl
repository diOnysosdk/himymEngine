// @shader_id 10
// @name fireball
// @editor_dyn dyn0 | Orbital drift direction.
// @editor_dyn dyn1 | Flame turbulence speed.
// @editor_dyn dyn2 | Flame tongue sharpness.
// @editor_dyn dyn3 | Ember ejection intensity.
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
