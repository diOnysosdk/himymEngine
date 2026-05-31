// @shader_id 34
// @name flag_wave
// @editor_dyn dyn0 | Wave frequency (horizontal waves).
// @editor_dyn dyn1 | Wave amplitude (vertical displacement).
// @editor_dyn dyn2 | Wind speed (animation speed).
// @editor_dyn dyn3 | Ripple detail (secondary wave complexity).
else if (id == 34) {
    // Configurable parameters from dyn controls
    float wave_freq = mix(3.0, 12.0, 0.5 + 0.5 * dynBiDZ(dyn.x, 0.06));
    float wave_amp = mix(0.01, 0.08, 0.5 + 0.5 * dynBiDZ(dyn.y, 0.06));
    float wind_speed = mix(0.8, 3.0, 0.5 + 0.5 * dynBiDZ(dyn.z, 0.06)) * spd;
    float ripple_detail = mix(0.0, 0.5, 0.5 + 0.5 * dynBiDZ(dyn.w, 0.06));
    
    // UV coordinates for flag (flip Y to correct image orientation)
    // Apply scale around center point
    vec2 flag_uv = vec2(uv.x, 1.0 - uv.y);
    flag_uv = (flag_uv - 0.5) / u_flag_scale + 0.5;
    
    // Primary wave motion (horizontal waves traveling vertically)
    float wave_phase = st * wind_speed;
    float wave = sin(flag_uv.x * wave_freq + wave_phase) * wave_amp;
    
    // Secondary ripple (adds detail based on position)
    float ripple = sin(flag_uv.x * wave_freq * 2.5 + flag_uv.y * 8.0 + wave_phase * 1.3) * wave_amp * 0.3 * ripple_detail;
    
    // Apply wave displacement to V coordinate
    flag_uv.y += wave + ripple;
    
    // Add subtle horizontal flutter at the edges (flag edges move more)
    float edge_flutter = pow(flag_uv.x, 2.0) * sin(flag_uv.y * 6.0 + wave_phase * 2.0) * wave_amp * 0.5;
    flag_uv.x += edge_flutter;
    
    // Clamp UV to prevent over-distortion
    flag_uv = clamp(flag_uv, 0.0, 1.0);
    
    // Sample the flag texture
    vec3 tex_col = texture(u_flag_texture, flag_uv).rgb;
    
    // Apply palette tinting and grading
    vec3 tinted = tex_col * mix(c_low, c_high, tex_col.r * 0.5 + 0.5);
    
    // Lighting simulation - flag gets darker in the wave troughs
    float lighting = 1.0 - abs(wave + ripple) * 8.0 * ins;
    lighting = clamp(lighting, 0.7, 1.15);
    
    // Apply intensity modulation
    col = tinted * lighting * ins;
    
    // Add subtle edge glow where the flag curves most
    float curve_intensity = abs(wave + ripple) * 5.0;
    col += c_high * curve_intensity * 0.15 * ins;
    
    // Vignette for depth
    float vign = vignetteMask(uv);
    col *= mix(0.85, 1.0, vign);
}
