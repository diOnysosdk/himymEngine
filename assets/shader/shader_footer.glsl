col = applyGlobalLook(col * ins, uv, st, dyn);
return col;
}
void main() {
vec2 uv = gl_TexCoord[0].xy;
vec4 dyn_a = vec4(a_dyn0, a_dyn1, a_dyn2, a_dyn3);
vec4 dyn_b = vec4(b_dyn0, b_dyn1, b_dyn2, b_dyn3);
vec3 col_a = renderScene(scene_a_id, uv, time, a_st, a_palette_low, a_palette_mid, a_palette_high, a_speed, a_intensity, a_warp, dyn_a);
vec3 col_b = renderScene(scene_b_id, uv, time, b_st, b_palette_low, b_palette_mid, b_palette_high, b_speed, b_intensity, b_warp, dyn_b);
vec3 finalCol = mix(col_a, col_b, clamp(scene_blend, 0.0, 1.0));
finalCol *= exposure;
finalCol = mix(finalCol, vec3(0.0), fade);
finalCol = finalCol / (vec3(1.0) + finalCol * 0.1);
gl_FragColor = vec4(finalCol, 1.0);
}
