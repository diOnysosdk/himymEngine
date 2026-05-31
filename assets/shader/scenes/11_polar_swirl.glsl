// @shader_id 11
// @name polar_swirl
else if (id == 11) {
float swirl = sin(a * 5.0 + r * 20.0 - st);
col = mix(c_low, c_high, swirl * 0.5 + 0.5);
}
