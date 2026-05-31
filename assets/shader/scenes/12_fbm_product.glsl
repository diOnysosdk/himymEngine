// @shader_id 12
// @name fbm_product
else if (id == 12) {
float n1 = fbm(uv * 3.0 + st * 0.2);
float n2 = fbm(uv * 6.0 - st * 0.1);
col = mix(c_low, c_high, pow(n1 * n2, 0.5) * 2.0);
}
