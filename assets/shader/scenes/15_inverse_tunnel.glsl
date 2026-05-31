// @shader_id 15
// @name inverse_tunnel
else if (id == 15) {
float z = sin(1.0 / (r + 0.01) * 5.0 + st);
col = mix(c_low, c_high, step(0.0, z));
}
