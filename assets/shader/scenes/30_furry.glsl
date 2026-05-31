// @shader_id 30
// @name furry
// @editor_dyn dyn0 | Shake heading and spin bias.
// @editor_dyn dyn1 | Shake amount and wobble rate.
// @editor_dyn dyn2 | Fur length and strand density.
// @editor_dyn dyn3 | Rim glow and sparkle energy.
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
