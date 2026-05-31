// @shader_id 21
// @name horizon_line
else if (id == 21) {
float d = abs(uv.y - 0.5 + sin(uv.x * 10.0 + st) * 0.2);
col = c_high * (0.02 / d);
}
