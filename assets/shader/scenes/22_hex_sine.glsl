// @shader_id 22
// @name hexagonal_lattice
// @editor_dyn dyn0 | Lattice rotation drift.
// @editor_dyn dyn1 | Sweep speed.
// @editor_dyn dyn2 | Cell density.
// @editor_dyn dyn3 | Edge glow amount.
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
