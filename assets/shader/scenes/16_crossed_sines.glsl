// @shader_id 16
// @name crossed_sines
else if (id == 16) {
float line = abs(sin(uv.x * 20.0 + fbm(uv * 5.0) * 5.0)) * abs(sin(uv.y * 20.0 + st));
col = c_high * pow(1.0 - line, 10.0);
}
