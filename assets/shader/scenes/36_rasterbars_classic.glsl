// @shader_id 36
// @name rasterbars_classic
// @editor_dyn dyn0 | Scroll speed and direction.
// @editor_dyn dyn1 | Bar count and spacing.
// @editor_dyn dyn2 | Sine wave amplitude (horizontal motion).
// @editor_dyn dyn3 | Bar width and sharpness.
else if (id == 36) {
    float d0 = dynBiDZ(dyn.x, 0.06);
    float d1 = dynBiDZ(dyn.y, 0.08);
    float d2 = dynBiDZ(dyn.z, 0.06);
    float d3 = dynBiDZ(dyn.w, 0.06);
    
    // Scroll speed control
    float scroll_speed = mix(0.5, 2.5, (d0 + 1.0) * 0.5);
    
    // Bar frequency/count
    float bar_freq = mix(8.0, 24.0, (d1 + 1.0) * 0.5);
    
    // Sine wave amplitude for horizontal motion
    float wave_amp = mix(0.0, 0.15, (d2 + 1.0) * 0.5);
    
    // Bar width/sharpness
    float bar_width = mix(0.3, 0.7, (d3 + 1.0) * 0.5);
    
    // Vertical position with scrolling
    float y_scroll = uv.y + st * scroll_speed * 0.15;
    
    // Horizontal sine wave offset
    float x_offset = sin(y_scroll * bar_freq + st * 1.5) * wave_amp;
    vec2 ruv = vec2(uv.x + x_offset, y_scroll);
    
    // Create repeating bars
    float bar_phase = fract(ruv.y * bar_freq * 0.5);
    float bar = smoothstep(bar_width, bar_width - 0.1, bar_phase) * 
                smoothstep(1.0 - bar_width, 1.0 - bar_width + 0.1, bar_phase);
    
    // Color gradient per bar
    float color_phase = fract(ruv.y * bar_freq * 0.5);
    vec3 bar_color = mix(c_low, c_high, color_phase);
    
    // Edge glow for classic look
    float edge = abs(bar_phase - 0.5) * 2.0;
    float glow = (1.0 - edge) * bar * 0.3;
    
    col = bar_color * (bar + glow) * ins;
}
