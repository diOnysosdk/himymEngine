// @shader_id 6
// @name storm_cells_2
// @editor_dyn dyn0 | Wind direction shear.
// @editor_dyn dyn1 | Storm motion speed.
// @editor_dyn dyn2 | Lightning strike frequency.
// @editor_dyn dyn3 | Rain and glow intensity.
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
