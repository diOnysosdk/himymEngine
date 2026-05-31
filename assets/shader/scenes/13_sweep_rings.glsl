// @shader_id 13
// @name sweep_rings
else if (id == 13) {
float sweep = fract(a / 6.28 + st * 0.2);
float rings = step(0.48, fract(r * 10.0));
col = c_high * (1.0 - sweep) * (rings + 0.2);
}
