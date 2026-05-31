// @shader_id 28
// @name desert_flyover_cinematic
// @editor_dyn dyn0 | Camera bank and heat-wave drift.
// @editor_dyn dyn1 | Forward flyover speed.
// @editor_dyn dyn2 | Dune sharpness and ridge detail.
// @editor_dyn dyn3 | Cactus density and dust glow.
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
