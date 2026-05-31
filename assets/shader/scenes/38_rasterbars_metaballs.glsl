// @shader_id 38
// @name rasterbars_metaballs
// @editor_dyn dyn0 | Metaball animation speed.
// @editor_dyn dyn1 | Bar blob count.
// @editor_dyn dyn2 | Blob size and merge radius.
// @editor_dyn dyn3 | Glow intensity and color shift.
else if (id == 38) {
    float d0 = dynBiDZ(dyn.x, 0.06);
    float d1 = dynBiDZ(dyn.y, 0.08);
    float d2 = dynBiDZ(dyn.z, 0.06);
    float d3 = dynBiDZ(dyn.w, 0.06);
    
    // Animation parameters
    float anim_speed = mix(0.4, 1.8, (d0 + 1.0) * 0.5);
    
    // Blob parameters
    float blob_count = mix(5.0, 12.0, (d1 + 1.0) * 0.5);
    float blob_size = mix(0.08, 0.20, (d2 + 1.0) * 0.5);
    float glow_intensity = mix(0.3, 1.2, (d3 + 1.0) * 0.5);
    
    // Metaball field accumulator
    float metaball_field = 0.0;
    float y_scroll = st * anim_speed * 0.12;
    
    // Create multiple metaball "bars"
    for (float i = 0.0; i < 12.0; i += 1.0) {
        if (i >= blob_count) break;
        
        // Bar position with different sine wave patterns
        float bar_y = fract(i / blob_count + y_scroll);
        float bar_x = 0.5 + sin(st * anim_speed + i * 2.0) * 0.15 +
                           cos(st * anim_speed * 0.7 + i * 1.5) * 0.10;
        
        vec2 blob_pos = vec2(bar_x, bar_y);
        float dist = length(uv - blob_pos);
        
        // Metaball contribution (inverse distance falloff)
        metaball_field += blob_size / (dist + 0.01);
        
        // Add horizontal elongation for bar effect
        float bar_dist = abs(uv.y - bar_y);
        metaball_field += blob_size * 0.5 / (bar_dist * bar_dist + 0.02);
    }
    
    // Threshold to create solid bars with soft edges
    float bar_mask = smoothstep(1.5, 2.5, metaball_field);
    float glow = smoothstep(0.8, 1.5, metaball_field) * (1.0 - bar_mask) * glow_intensity;
    
    // Color based on metaball field density
    float color_mix = fract(metaball_field * 0.3 + st * anim_speed * 0.1);
    vec3 bar_color = mix(c_low, c_high, color_mix);
    vec3 glow_color = mix(c_mid, c_high, color_mix);
    
    // Combine bar and glow
    col = bar_color * bar_mask + glow_color * glow;
    
    // Extra core highlights where blobs merge
    float highlight = smoothstep(3.5, 5.0, metaball_field);
    col += c_high * highlight * 0.6;
    
    col *= ins;
}
