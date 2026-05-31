// @shader_id 4
// @name dune_waves
else if (id == 4) {
vec2 g = uv * (10.0 + wrp * 5.0);
vec2 id_g = floor(g);
float n = hash(id_g);
float b = pow(0.5 + 0.5 * sin(st + n * 6.28), 10.0);
float s = smoothstep(0.45, 0.4, max(abs(fract(g.x)-0.5), abs(fract(g.y)-0.5)));
col = mix(c_low * 0.2, c_high, s * (0.2 + b));
}
