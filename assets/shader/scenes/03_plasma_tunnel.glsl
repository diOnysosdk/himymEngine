// @shader_id 3
// @name plasma_tunnel
else if (id == 3) {
float v = sin(10.0 * r - st + sin(a * 5.0) * wrp);
col = mix(c_low, c_high, v * 0.5 + 0.5) + (0.05 / r) * c_high;
}
