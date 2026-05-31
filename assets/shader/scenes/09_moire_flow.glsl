// @shader_id 9
// @name moire_flow
else if (id == 9) {
float f = sin(uv.x * 50.0 + st) * sin(uv.y * 50.0 + st * 1.2);
col = mix(c_low, c_high, f * 0.5 + 0.5);
}
