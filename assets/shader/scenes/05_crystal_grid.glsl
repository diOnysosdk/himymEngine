// @shader_id 5
// @name crystal_grid
else if (id == 5) {
float rays = sin(a * 20.0 + st + fbm(p * 10.0) * 5.0);
col = mix(c_low, c_high, rays * 0.5 + 0.5) * smoothstep(0.8, 0.1, r) + (0.02 / r) * c_high;
}
