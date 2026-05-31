// @shader_id 24
// @name cinematic_wormhole_enhanced
// @editor_dyn dyn0 | Spiral spin bias.
// @editor_dyn dyn1 | Forward tunnel velocity.
// @editor_dyn dyn2 | Chromatic dispersion.
// @editor_dyn dyn3 | Event-horizon bloom.
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
