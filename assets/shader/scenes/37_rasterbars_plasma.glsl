// @shader_id 37
// @name rasterbars_plasma
// @editor_dyn dyn0 | Plasma animation speed.
// @editor_dyn dyn1 | Bar count and density.
// @editor_dyn dyn2 | Plasma complexity and flow.
// @editor_dyn dyn3 | Color cycling speed.
else if (id == 37) {
    float d0 = dynBiDZ(dyn.x, 0.06);
    float d1 = dynBiDZ(dyn.y, 0.08);
    float d2 = dynBiDZ(dyn.z, 0.06);
    float d3 = dynBiDZ(dyn.w, 0.06);
    
    // Animation speeds
    float anim_speed = mix(0.6, 2.0, (d0 + 1.0) * 0.5);
    float color_speed = mix(0.3, 1.5, (d3 + 1.0) * 0.5);
    
    // Bar parameters
    float bar_freq = mix(6.0, 18.0, (d1 + 1.0) * 0.5);
    float plasma_complexity = mix(0.5, 2.0, (d2 + 1.0) * 0.5);
    
    // Plasma-like motion for bars
    float y_wave = uv.y + 
                   sin(uv.x * 8.0 + st * anim_speed) * 0.08 +
                   cos(uv.x * 5.0 - st * anim_speed * 0.7) * 0.06;
    
    float x_wave = uv.x + 
                   sin(uv.y * 6.0 + st * anim_speed * 1.2) * 0.06 +
                   cos(uv.y * 9.0 - st * anim_speed * 0.9) * 0.04;
    
    // Create plasma-infused bars
    float bar_phase = fract(y_wave * bar_freq);
    float bar = smoothstep(0.4, 0.3, bar_phase) * smoothstep(0.6, 0.7, bar_phase);
    
    // Plasma color gradient within bars
    float plasma1 = sin(x_wave * 12.0 * plasma_complexity + st * color_speed) * 0.5 + 0.5;
    float plasma2 = cos(y_wave * 10.0 * plasma_complexity - st * color_speed * 0.8) * 0.5 + 0.5;
    float plasma_mix = (plasma1 + plasma2) * 0.5;
    
    // Multi-color gradient across bars
    float color_offset = fract(y_wave * bar_freq + st * color_speed * 0.2);
    vec3 color1 = mix(c_low, c_mid, plasma_mix);
    vec3 color2 = mix(c_mid, c_high, plasma_mix);
    vec3 bar_color = mix(color1, color2, color_offset);
    
    // Enhanced glow for plasma feel
    float glow = bar * smoothstep(0.0, 0.3, bar_phase) * smoothstep(1.0, 0.7, bar_phase) * 0.5;
    
    col = bar_color * (bar + glow) * ins;
}
