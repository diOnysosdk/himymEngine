// @shader_id 32
// @name singularity_accretion
// @editor_dyn dyn0 | Disk spin direction and swirl rate.
// @editor_dyn dyn1 | Event-horizon size and lensing strength.
// @editor_dyn dyn2 | Gas turbulence and filament detail.
// @editor_dyn dyn3 | Polar jet energy and bloom.
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