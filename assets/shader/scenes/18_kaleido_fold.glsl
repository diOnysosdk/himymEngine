// @shader_id 18
// @name kaleidoscope
// @editor_dyn dyn0 | Fold count modulation.
// @editor_dyn dyn1 | Spin and sweep speed.
// @editor_dyn dyn2 | Domain warp strength.
// @editor_dyn dyn3 | Highlight bloom intensity.
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
