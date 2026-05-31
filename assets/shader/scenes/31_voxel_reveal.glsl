// @shader_id 31
// @name voxel_reveal
// @editor_dyn dyn0 | Reveal radius and gate softness.
// @editor_dyn dyn1 | Voxel cell density and edge hardness.
// @editor_dyn dyn2 | Ambient plasma glow amount.
// @editor_dyn dyn3 | Reveal pulse/motion speed.
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