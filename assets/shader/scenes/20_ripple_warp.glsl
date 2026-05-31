// @shader_id 20
// @name ripple_warp
else if (id == 20) {
float rip = sin(r * 40.0 - st * 2.0 + fbm(uv * 10.0) * wrp);
col = mix(c_low, c_high, rip * 0.5 + 0.5);
}
