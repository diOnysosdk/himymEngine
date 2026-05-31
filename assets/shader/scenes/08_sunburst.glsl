// @shader_id 8
// @name sunburst
else if (id == 8) {
float h = fbm(vec2(uv.x * 2.0 + st * 0.5, uv.y * 0.2));
col = mix(c_low, c_high, smoothstep(0.4, 0.6, sin(uv.x * 5.0 + h * 5.0 + uv.y * 2.0)));
}
