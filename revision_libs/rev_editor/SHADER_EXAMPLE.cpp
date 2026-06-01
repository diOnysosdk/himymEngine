// Example: How to add a new shader to the system
//
// This file demonstrates the modular shader architecture.
// To add a new shader, simply append an entry to g_shader_presets[] in shader_presets.cpp

// Example entry structure:
/*
{
    11,  // Unique ID (next available number)
    "Fire Particles",
    "Rising fire particle effect with heat distortion",
    R"(
#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform float u_time;
uniform vec2 u_resolution;
uniform vec3 u_palette_low;
uniform vec3 u_palette_mid;
uniform vec3 u_palette_high;
uniform float u_speed;
uniform float u_intensity;

// Hash function for randomness
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void main() {
    vec2 p = uv * vec2(u_resolution.x / u_resolution.y, 1.0);
    float t = u_time * u_speed;
    
    vec3 col = vec3(0.0);
    
    // Generate fire particles
    for (int i = 0; i < 50; i++) {
        float fi = float(i);
        float h = hash(vec2(fi, 0.5));
        
        // Particle position
        vec2 particle_pos = vec2(
            h * u_resolution.x / u_resolution.y,
            fract(h * 7.13 - t * 0.5) * 1.2 - 0.1
        );
        
        float d = length(p - particle_pos);
        float particle = smoothstep(0.02, 0.0, d);
        
        // Color based on height (cooler at top)
        float heat = 1.0 - particle_pos.y;
        vec3 fire_col = mix(
            mix(u_palette_mid, u_palette_high, heat),
            u_palette_low,
            smoothstep(0.8, 1.0, particle_pos.y)
        );
        
        col += fire_col * particle * u_intensity;
    }
    
    fragColor = vec4(col, 1.0);
}
)"
}
*/

// That's it! Rebuild and the shader appears in the editor dropdown.
//
// For more details, see SHADER_SYSTEM.md
