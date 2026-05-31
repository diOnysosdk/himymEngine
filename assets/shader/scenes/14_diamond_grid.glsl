// @shader_id 14
// @name diamond_grid
else if (id == 14) {
vec2 uv2 = uv * rot(0.785); // 45 grader
float d = max(abs(fract(uv2.x * 10.0)-0.5), abs(fract(uv2.y * 10.0)-0.5));
col = mix(c_low, c_high, smoothstep(0.45, 0.5, d));
}
