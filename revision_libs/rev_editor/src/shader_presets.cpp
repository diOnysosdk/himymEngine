#include "shader_presets.h"
#include <cstring>

namespace rev {
namespace editor {

// Central shader registry with embedded GLSL source code
// To add a new shader: add an entry here with id, name, description, and fragment source
const ShaderPreset g_shader_presets[] = {
    {
        0,
        "Horizontal Gradient Bands",
        "Three horizontal color bands - default black, set colors for fades",
        R"(
#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform vec3 u_palette_low;
uniform vec3 u_palette_mid;
uniform vec3 u_palette_high;

void main() {
    // Smooth vertical blend across low -> mid -> high (no hard band edges).
    float y = uv.y;
    float low_to_mid = smoothstep(0.20, 0.55, y);
    float mid_to_high = smoothstep(0.45, 0.80, y);

    vec3 low_mid = mix(u_palette_low, u_palette_mid, low_to_mid);
    vec3 col = mix(low_mid, u_palette_high, mid_to_high);
    
    fragColor = vec4(col, 1.0);
}
)"
    },
    
    {
        1,
        "Plasma Vibrant",
        "Multi-layer plasma with FBM noise",
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

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float fbm(vec2 p) {
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 5; i++) {
        v += noise(p) * a;
        p *= 2.07;
        a *= 0.5;
    }
    return v;
}

void main() {
    vec2 p = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0);
    float t = u_time * u_speed;
    
    // Multiple plasma layers with different frequencies
    float v1 = sin(p.x * 5.0 + t) + cos(p.y * 5.0 - t * 0.7);
    float v2 = sin(length(p) * 8.0 - t * 1.5);
    float v3 = fbm(p * 3.0 + vec2(t * 0.3, -t * 0.2));
    
    // Combine layers with FBM noise
    float plasma = (v1 + v2 + v3 * 2.0) / 4.0;
    plasma = plasma * 0.5 + 0.5; // Normalize to 0-1
    
    // Add swirling distortion
    vec2 distort = vec2(
        fbm(p * 2.0 + vec2(t * 0.2, 0.0)),
        fbm(p * 2.0 + vec2(0.0, t * 0.15))
    ) * 0.3;
    plasma += fbm(p + distort) * 0.4;
    
    // Complex color mixing
    vec3 col = u_palette_low;
    col = mix(col, u_palette_mid, smoothstep(0.2, 0.5, plasma));
    col = mix(col, u_palette_high, smoothstep(0.5, 0.9, plasma));
    col += u_palette_high * pow(plasma, 4.0) * 0.5; // Bright highlights
    
    col *= u_intensity;
    
    fragColor = vec4(col, 1.0);
}
)"
    },
    
    {
        2,
        "Tunnel Neon",
        "Hexagonal tunnel with chromatic aberration",
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
uniform float u_warp;

void main() {
    vec2 p = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0);
    float t = u_time * u_speed;
    
    float r = length(p);
    float a = atan(p.y, p.x);
    
    // Hexagonal tunnel coordinates
    float angle = a + sin(r * 3.0 - t) * u_warp * 0.3;
    float hexAngle = mod(angle, 3.14159 / 3.0) - 3.14159 / 6.0;
    
    // Tunnel depth with perspective
    float d = 1.0 / (r + 0.05);
    float tunnel = d - t * 0.5;
    
    // Hexagonal pattern
    float hex = abs(sin(hexAngle * 6.0)) * cos(tunnel * 15.0);
    float rings = abs(sin(tunnel * 25.0));
    
    // Chromatic aberration effect
    vec2 offset_r = p * (1.0 + 0.02 * sin(t));
    vec2 offset_b = p * (1.0 - 0.02 * sin(t));
    
    float r_r = length(offset_r);
    float a_r = atan(offset_r.y, offset_r.x);
    float d_r = 1.0 / (r_r + 0.05);
    float tunnel_r = d_r - t * 0.5;
    
    float r_b = length(offset_b);
    float a_b = atan(offset_b.y, offset_b.x);
    float d_b = 1.0 / (r_b + 0.05);
    float tunnel_b = d_b - t * 0.5;
    
    float rings_r = abs(sin(tunnel_r * 25.0 + hex * 5.0));
    float rings_b = abs(sin(tunnel_b * 25.0 + hex * 5.0));
    
    // Build color with chromatic aberration
    vec3 col;
    col.r = mix(u_palette_low.r, u_palette_high.r, rings_r) * (1.0 + hex * 0.5);
    col.g = mix(u_palette_mid.g, u_palette_high.g, rings) * (1.0 + hex * 0.5);
    col.b = mix(u_palette_low.b, u_palette_mid.b, rings_b) * (1.0 + hex * 0.5);
    
    // Add depth fog
    float fog = exp(-r * 2.0);
    col = mix(u_palette_low * 0.1, col, fog);
    
    // Glow effect at center
    col += u_palette_high * exp(-r * 8.0) * 0.8;
    
    fragColor = vec4(col, 1.0);
}
)"
    },
    
    {
        3,
        "Raymarcher SDF",
        "Multiple SDF shapes with lighting and shadows",
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

float sdSphere(vec3 p, float r) { return length(p) - r; }
float sdBox(vec3 p, vec3 b) { vec3 d = abs(p) - b; return length(max(d, 0.0)) + min(max(d.x, max(d.y, d.z)), 0.0); }
float sdTorus(vec3 p, vec2 t) { vec2 q = vec2(length(p.xz) - t.x, p.y); return length(q) - t.y; }

vec3 rotateY(vec3 p, float a) {
    float s = sin(a), c = cos(a);
    return vec3(c * p.x + s * p.z, p.y, -s * p.x + c * p.z);
}

float scene(vec3 p) {
    vec3 rp = rotateY(p, u_time * u_speed * 0.5);
    
    float sphere1 = sdSphere(rp - vec3(sin(u_time * u_speed) * 1.5, 0, 0), 0.5);
    float box1 = sdBox(rp - vec3(0, sin(u_time * u_speed * 0.7) * 1.2, 0), vec3(0.4));
    float torus1 = sdTorus(rp - vec3(0, 0, 0), vec2(1.0, 0.2));
    
    return min(sphere1, min(box1, torus1));
}

vec3 getNormal(vec3 p) {
    vec2 e = vec2(0.001, 0.0);
    return normalize(vec3(
        scene(p + e.xyy) - scene(p - e.xyy),
        scene(p + e.yxy) - scene(p - e.yxy),
        scene(p + e.yyx) - scene(p - e.yyx)
    ));
}

void main() {
    vec2 p = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0);
    float t = u_time * u_speed;
    
    vec3 ro = vec3(0, 0, -4);
    vec3 rd = normalize(vec3(p, 1.5));
    
    float dist = 0.0;
    vec3 pos;
    bool hit = false;
    
    for (int i = 0; i < 64; i++) {
        pos = ro + rd * dist;
        float d = scene(pos);
        if (d < 0.001) { hit = true; break; }
        if (dist > 20.0) break;
        dist += d;
    }
    
    vec3 col;
    if (hit) {
        vec3 normal = getNormal(pos);
        
        // Lighting
        vec3 lightDir = normalize(vec3(1.0, 1.0, -1.0));
        float diff = max(dot(normal, lightDir), 0.0);
        float spec = pow(max(dot(reflect(-lightDir, normal), -rd), 0.0), 32.0);
        
        // Ambient occlusion approximation
        float ao = 1.0 - dist / 20.0;
        
        col = u_palette_low * 0.2; // ambient
        col += u_palette_mid * diff * 0.8;
        col += u_palette_high * spec;
        col *= ao;
    } else {
        // Background gradient
        col = mix(u_palette_low * 0.1, u_palette_mid * 0.3, length(p) * 0.5);
    }
    
    fragColor = vec4(col, 1.0);
}
)"
    },
    
    {
        4,
        "Fractal Mandelbrot",
        "Classic Mandelbrot fractal zoom",
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

void main() {
    vec2 p = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0) * 2.0;
    float t = u_time * u_speed * 0.2;
    p += vec2(sin(t) * 0.3, cos(t * 0.7) * 0.3);
    
    vec2 c = p;
    vec2 z = vec2(0.0);
    float iter = 0.0;
    
    for (int i = 0; i < 64; i++) {
        z = vec2(z.x * z.x - z.y * z.y, 2.0 * z.x * z.y) + c;
        if (length(z) > 4.0) break;
        iter += 1.0;
    }
    
    float v = iter / 64.0;
    vec3 col = mix(mix(u_palette_low, u_palette_mid, v), u_palette_high, smoothstep(0.7, 1.0, v));
    
    fragColor = vec4(col, 1.0);
}
)"
    },
    
    {
        5,
        "Voronoi Cells",
        "Cellular voronoi patterns",
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

vec2 hash2(vec2 p) {
    p = vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)));
    return fract(sin(p) * 43758.5453);
}

void main() {
    vec2 p = uv * 8.0 * vec2(u_resolution.x / u_resolution.y, 1.0);
    float t = u_time * u_speed;
    
    vec2 ip = floor(p);
    vec2 fp = fract(p);
    
    float minDist = 1.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 offset = vec2(x, y);
            vec2 h = hash2(ip + offset);
            vec2 pt = offset + sin(h * 6.28 + t) * 0.5 + 0.5;
            float d = length(pt - fp);
            minDist = min(minDist, d);
        }
    }
    
    vec3 col = mix(mix(u_palette_low, u_palette_mid, minDist), u_palette_high, smoothstep(0.5, 1.0, minDist));
    
    fragColor = vec4(col, 1.0);
}
)"
    },
    
    {
        6,
        "Wave Distortion",
        "Complex feedback distortion with chromatic splits",
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
uniform float u_warp;

void main() {
    vec2 p = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0);
    float t = u_time * u_speed;
    
    // Multi-layer wave distortion
    vec2 distort = p;
    distort.x += sin(p.y * 4.0 + t) * u_warp * 0.3;
    distort.y += cos(p.x * 4.0 + t * 0.7) * u_warp * 0.3;
    
    distort.x += sin(distort.y * 7.0 - t * 1.3) * u_warp * 0.2;
    distort.y += cos(distort.x * 7.0 + t * 0.9) * u_warp * 0.2;
    
    distort.x += sin(distort.y * 12.0 + t * 1.5) * u_warp * 0.1;
    distort.y += cos(distort.x * 12.0 - t * 1.1) * u_warp * 0.1;
    
    float d = length(distort);
    
    // Chromatic color splits
    float wave_r = sin(d * 8.0 - t * 2.5) * 0.5 + 0.5;
    float wave_g = sin(d * 8.0 + sin(t) * 2.0 - t * 2.0) * 0.5 + 0.5;
    float wave_b = sin(d * 8.0 - sin(t * 1.3) * 2.0 - t * 1.5) * 0.5 + 0.5;
    
    vec3 col;
    col.r = mix(u_palette_low.r, u_palette_high.r, wave_r);
    col.g = mix(u_palette_mid.g, u_palette_high.g, wave_g);
    col.b = mix(u_palette_low.b, u_palette_mid.b, wave_b);
    
    // Add radial glow based on feedback
    float feedback = sin(d * 15.0 + sin(distort.x * 10.0) + cos(distort.y * 10.0) - t * 3.0);
    feedback = feedback * 0.5 + 0.5;
    col += u_palette_high * pow(feedback, 4.0) * 0.4;
    
    // Vignette
    col *= 1.0 - length(p) * 0.3;
    
    fragColor = vec4(col, 1.0);
}
)"
    },
    
    {
        7,
        "Particle System",
        "GPU particles with trails and physics",
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

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

vec2 hash2(vec2 p) {
    p = vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)));
    return fract(sin(p) * 43758.5453) * 2.0 - 1.0;
}

void main() {
    vec2 p = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0);
    float t = u_time * u_speed;
    
    vec3 col = vec3(0.0);
    
    // Multiple particle systems with different behaviors
    for (int i = 0; i < 64; i++) {
        float fi = float(i);
        vec2 seed = vec2(fi * 0.1, fi * 0.7);
        
        float h = hash(seed);
        vec2 vel = hash2(seed + 13.7) * 0.5;
        
        // Orbital motion
        float angle = h * 6.28 + t * (0.3 + h * 0.4);
        float radius = 0.3 + h * 0.8 + sin(t * 0.5 + h * 6.28) * 0.2;
        
        vec2 pos = vec2(cos(angle), sin(angle)) * radius;
        pos += vel * sin(t * (0.5 + h * 0.5)) * 0.3;
        
        float d = length(p - pos);
        
        // Main particle
        float particle = smoothstep(0.04, 0.0, d) * (0.8 + sin(t * 10.0 + h * 100.0) * 0.2);
        
        // Particle trails
        for (int j = 1; j <= 5; j++) {
            float tj = t - float(j) * 0.05;
            float anglej = h * 6.28 + tj * (0.3 + h * 0.4);
            float radiusj = 0.3 + h * 0.8 + sin(tj * 0.5 + h * 6.28) * 0.2;
            vec2 posj = vec2(cos(anglej), sin(anglej)) * radiusj;
            posj += vel * sin(tj * (0.5 + h * 0.5)) * 0.3;
            
            float dj = length(p - posj);
            float trail = smoothstep(0.03, 0.0, dj) * (1.0 - float(j) / 6.0);
            particle += trail * 0.3;
        }
        
        // Color variety
        vec3 pcol = mix(u_palette_low, u_palette_mid, h);
        pcol = mix(pcol, u_palette_high, smoothstep(0.5, 1.0, h));
        
        col += pcol * particle;
    }
    
    // Add glow
    col += col * 0.5;
    
    fragColor = vec4(col, 1.0);
}
)"
    },
    
    {
        8,
        "Starfield",
        "3D starfield with motion blur",
        R"(
#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform float u_time;
uniform vec2 u_resolution;
uniform vec3 u_palette_high;
uniform float u_speed;

float hash(vec3 p) {
    return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453);
}

void main() {
    vec2 p = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0);
    float t = u_time * u_speed;
    
    vec3 rd = normalize(vec3(p, 1.0));
    vec3 col = vec3(0.0);
    
    for (int i = 0; i < 64; i++) {
        float fi = float(i);
        vec3 star_pos = vec3(hash(vec3(fi, fi * 0.1, 0)) * 2.0 - 1.0,
                             hash(vec3(fi * 0.5, fi, 0)) * 2.0 - 1.0,
                             hash(vec3(fi, 0, fi * 0.7)) * 5.0 + 1.0);
        
        star_pos.z = fract(star_pos.z - t * 0.5) * 10.0;
        vec3 proj = star_pos / star_pos.z;
        
        float d = length(proj.xy - p);
        float star = smoothstep(0.02, 0.0, d) / star_pos.z;
        col += u_palette_high * star;
    }
    
    fragColor = vec4(col, 1.0);
}
)"
    },
    
    {
        9,
        "Glow Orbs",
        "Glowing orb metaballs",
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

void main() {
    vec2 p = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0);
    float t = u_time * u_speed;
    
    vec3 col = vec3(0.0);
    
    for (int i = 0; i < 5; i++) {
        float fi = float(i);
        vec2 orb_pos = vec2(sin(t * 0.5 + fi * 1.2) * 0.6, cos(t * 0.7 + fi * 0.8) * 0.6);
        float d = length(p - orb_pos);
        float glow = 0.02 / d;
        
        vec3 orb_col = mix(mix(u_palette_low, u_palette_mid, fi / 5.0), u_palette_high, smoothstep(0.3, 0.8, fi / 5.0));
        col += orb_col * glow;
    }
    
    fragColor = vec4(col, 1.0);
}
)"
    },
    
    {
        10,
        "Matrix Rain",
        "Matrix-style digital rain",
        R"(
#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform float u_time;
uniform vec2 u_resolution;
uniform vec3 u_palette_high;
uniform float u_speed;

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void main() {
    vec2 p = uv * vec2(40.0, 30.0);
    vec2 ip = floor(p);
    vec2 fp = fract(p);
    float t = u_time * u_speed;
    
    float h = hash(ip);
    float drop = fract(h * 7.13 - t * 0.5);
    
    float char_y = fract((ip.y + drop * 30.0) / 30.0);
    float char_brightness = smoothstep(0.0, 0.05, drop) * smoothstep(1.0, 0.8, drop);
    
    float char = step(0.3, hash(ip + floor(t * 10.0)));
    float glyph = char * char_brightness;
    
    vec3 col = u_palette_high * glyph;
    
    fragColor = vec4(col, 1.0);
}
)"
    },
    
    {
        11,
        "Spiral Galaxy",
        "Rotating nebula with stars and spiral arms",
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
uniform float u_warp;

uniform float u_exposure_base;
uniform float u_fade_base;

float hash(vec2 p)
{
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float noise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);

    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));

    vec2 u = f * f * (3.0 - 2.0 * f);

    return mix(a, b, u.x)
         + (c - a) * u.y * (1.0 - u.x)
         + (d - b) * u.x * u.y;
}

float fbm(vec2 p)
{
    float v = 0.0;
    float a = 0.5;

    for (int i = 0; i < 6; i++)
    {
        v += noise(p) * a;
        p *= 2.05;
        p += vec2(7.13, 3.71);
        a *= 0.5;
    }

    return v;
}

vec2 rotate(vec2 p, float a)
{
    float s = sin(a);
    float c = cos(a);
    return mat2(c, -s, s, c) * p;
}

float starLayer(vec2 p, float scale, float speed, float size)
{
    p *= scale;

    vec2 id = floor(p);
    vec2 gv = fract(p) - 0.5;

    float rnd = hash(id);
    vec2 offset = vec2(
        hash(id + 13.1),
        hash(id + 91.7)
    ) - 0.5;

    gv -= offset * 0.55;

    float d = length(gv);
    float star = smoothstep(size, 0.0, d);

    float twinkle = 0.55 + 0.45 * sin(u_time * speed + rnd * 40.0);
    star *= step(0.965, rnd) * twinkle;

    return star;
}

void main()
{
    vec2 aspect = vec2(u_resolution.x / u_resolution.y, 1.0);
    vec2 p = (uv - 0.5) * aspect;

    float t = u_time * u_speed;

    float r = length(p);
    float a = atan(p.y, p.x);

    vec2 drift = vec2(
        sin(t * 0.11),
        cos(t * 0.09)
    ) * 0.35 * u_warp;

    vec2 nebulaUV = p;
    nebulaUV = rotate(nebulaUV, t * 0.035);
    nebulaUV += drift;

    float n1 = fbm(nebulaUV * 2.1 + vec2(t * 0.035, -t * 0.025));
    float n2 = fbm(nebulaUV * 4.3 - vec2(t * 0.02, t * 0.015));
    float n3 = fbm(nebulaUV * 8.0 + n1 * 2.0);

    float nebula = n1 * 0.55 + n2 * 0.32 + n3 * 0.13;
    nebula = smoothstep(0.24, 1.0, nebula);

    float spiral = sin(a * 3.0 + r * 11.0 - t * 0.45);
    spiral = smoothstep(0.15, 1.0, spiral);

    float core = exp(-r * 4.2);
    float halo = exp(-r * 1.35);

    vec3 col = u_palette_low * 0.08;

    vec3 nebulaCol = mix(u_palette_low, u_palette_mid, nebula);
    nebulaCol = mix(nebulaCol, u_palette_high, pow(nebula, 3.0));

    col += nebulaCol * nebula * (0.55 + u_warp * 1.2);
    col += u_palette_mid * spiral * halo * 0.55;
    col += u_palette_high * core * 1.8;

    float stars = 0.0;
    stars += starLayer(p + vec2(t * 0.015, -t * 0.010), 55.0, 4.0, 0.045);
    stars += starLayer(p + vec2(-t * 0.010, t * 0.020), 95.0, 7.0, 0.035);
    stars += starLayer(p + vec2(t * 0.025, t * 0.015), 150.0, 10.0, 0.025);

    col += vec3(stars) * mix(u_palette_mid, u_palette_high, stars) * 2.2;

    float dust = fbm(p * 18.0 + vec2(t * 0.03, -t * 0.02));
    dust = smoothstep(0.57, 0.95, dust);
    col += u_palette_mid * dust * nebula * 0.18;

    float vignette = smoothstep(1.15, 0.15, r);
    col *= vignette;

    col *= u_intensity;
    col *= u_exposure_base;

    col = 1.0 - exp(-col);

    fragColor = vec4(col, u_fade_base);
}
)"
    },
    
    {
        12,
        "Wormhole",
        "Extreme chromatic aberration tunnel",
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
uniform float u_warp;

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void main() {
    vec2 p = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0);
    float t = u_time * u_speed;
    
    float r = length(p);
    float a = atan(p.y, p.x);
    
    // Extreme chromatic aberration offsets
    vec3 col = vec3(0.0);
    for (int i = 0; i < 3; i++) {
        float fi = float(i);
        float aberration = (fi - 1.0) * 0.08 * u_warp;
        vec2 offset_p = p * (1.0 + aberration);
        
        float r_ch = length(offset_p);
        float a_ch = atan(offset_p.y, offset_p.x);
        
        // Multi-layer tunnel
        float d1 = 1.0 / (r_ch + 0.02);
        float d2 = 1.0 / (r_ch + 0.05);
        float d3 = 1.0 / (r_ch + 0.10);
        
        float tunnel1 = fract(d1 - t * 0.8);
        float tunnel2 = fract(d2 - t * 0.6);
        float tunnel3 = fract(d3 - t * 0.4);
        
        // Spiral pattern
        float spiral = sin(a_ch * 8.0 + d1 * 15.0 - t * 4.0);
        
        // Combined pattern
        float pattern = abs(sin(tunnel1 * 40.0 + spiral * 5.0)) * tunnel2;
        pattern += abs(sin(tunnel2 * 30.0)) * tunnel3 * 0.5;
        pattern += abs(sin(tunnel3 * 20.0)) * 0.25;
        
        // Assign to color channel
        if (i == 0) col.r = pattern;
        else if (i == 1) col.g = pattern;
        else col.b = pattern;
    }
    
    // Apply palette colors
    col *= mix(u_palette_low, mix(u_palette_mid, u_palette_high, col.g), col.r);
    
    // Center glow
    col += u_palette_high * exp(-r * 10.0) * 2.0;
    
    // Vignette
    col *= smoothstep(1.5, 0.0, r);
    
    fragColor = vec4(col, 1.0);
}
)"
    },
    
    {
        13,
        "DNA Helix",
        "Double helix with glowing particles",
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

void main() {
    vec2 p = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0);
    float t = u_time * u_speed;
    
    vec3 col = vec3(0.0);
    
    // Camera movement
    float cam_z = t * 2.0;
    
    // Draw multiple segments of DNA
    for (float z = -5.0; z < 10.0; z += 0.2) {
        float depth = z - cam_z;
        float depth_fract = fract(depth);
        depth = floor(depth) + depth_fract;
        
        if (depth < -2.0 || depth > 8.0) continue;
        
        // Perspective projection
        float scale = 3.0 / (depth + 3.0);
        
        // Helix positions
        float helix_angle = depth * 0.8;
        vec2 pos1 = vec2(cos(helix_angle), sin(helix_angle)) * 0.4 * scale;
        vec2 pos2 = vec2(cos(helix_angle + 3.14159), sin(helix_angle + 3.14159)) * 0.4 * scale;
        
        // Draw helices
        float d1 = length(p - pos1);
        float d2 = length(p - pos2);
        
        float helix1 = smoothstep(0.04 * scale, 0.0, d1);
        float helix2 = smoothstep(0.04 * scale, 0.0, d2);
        
        // Connecting bars
        float bar_phase = fract(depth * 1.5);
        if (bar_phase < 0.1) {
            vec2 bar_dir = normalize(pos2 - pos1);
            float bar_proj = dot(p - pos1, bar_dir);
            float bar_dist = length(p - pos1 - bar_dir * clamp(bar_proj, 0.0, length(pos2 - pos1)));
            float bar = smoothstep(0.02 * scale, 0.0, bar_dist);
            col += u_palette_mid * bar * (1.0 - bar_phase * 10.0);
        }
        
        // Color based on depth
        float depth_color = smoothstep(8.0, -2.0, depth);
        vec3 color1 = mix(u_palette_low, u_palette_high, depth_color);
        vec3 color2 = mix(u_palette_mid, u_palette_high, depth_color);
        
        col += color1 * helix1 * 2.0;
        col += color2 * helix2 * 2.0;
        
        // Glow
        col += color1 * exp(-d1 * 15.0 / scale) * 0.3;
        col += color2 * exp(-d2 * 15.0 / scale) * 0.3;
    }
    
    fragColor = vec4(col, 1.0);
}
)"
    },
    
    {
        14,
        "Liquid Chrome",
        "Metallic fluid simulation",
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

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float fbm(vec2 p) {
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 6; i++) {
        v += noise(p) * a;
        p *= 2.17;
        a *= 0.5;
    }
    return v;
}

void main() {
    vec2 p = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0);
    float t = u_time * u_speed;
    
    // Fluid distortion
    vec2 fluid = p;
    fluid.x += fbm(p * 2.0 + vec2(t * 0.3, 0)) * 0.5;
    fluid.y += fbm(p * 2.0 + vec2(0, t * 0.2)) * 0.5;
    
    // Multiple noise layers for metallic look
    float n1 = fbm(fluid * 3.0 + vec2(t * 0.1, -t * 0.15));
    float n2 = fbm(fluid * 5.0 - vec2(t * 0.15, t * 0.1));
    float n3 = fbm(fluid * 8.0 + vec2(sin(t * 0.3), cos(t * 0.25)) * 2.0);
    
    // Combine layers
    float metal = n1 * 0.5 + n2 * 0.3 + n3 * 0.2;
    
    // Sharp metallic reflections
    float reflection = abs(sin(metal * 20.0));
    reflection = pow(reflection, 3.0);
    
    // Flowing highlights
    float highlight = sin(fluid.x * 5.0 - fluid.y * 3.0 + t * 2.0);
    highlight = pow(max(highlight, 0.0), 8.0);
    
    // Metallic gradient
    vec3 base = mix(u_palette_low, u_palette_mid, smoothstep(0.3, 0.7, metal));
    vec3 col = mix(base, u_palette_high, reflection * 0.6);
    
    // Add flowing highlights
    col += u_palette_high * highlight * 0.8;
    
    // Specular shine
    float spec = pow(max(n3, 0.0), 4.0);
    col += vec3(1.0) * spec * 0.5 * u_intensity;
    
    // Edge darkening for depth
    float edge = 1.0 - pow(length(p) * 0.7, 2.0);
    col *= edge;
    
    fragColor = vec4(col, 1.0);
}
)"
    },
    
    {
        15,
        "Kaleidoscope",
        "Multi-mirror symmetry fractals",
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

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1,0)), u.x),
               mix(hash(i + vec2(0,1)), hash(i + vec2(1,1)), u.x), u.y);
}

vec2 kaleidoscope(vec2 p, float segments) {
    float a = atan(p.y, p.x);
    float r = length(p);
    float segment_angle = 6.28318 / segments;
    a = mod(a, segment_angle);
    a = abs(a - segment_angle * 0.5);
    return vec2(cos(a), sin(a)) * r;
}

void main() {
    vec2 p = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0);
    float t = u_time * u_speed;
    
    // Rotate entire view
    float rot = t * 0.2;
    float s = sin(rot), c = cos(rot);
    p = vec2(c * p.x - s * p.y, s * p.x + c * p.y);
    
    // Apply kaleidoscope effect with varying segments
    float segments = 8.0 + sin(t * 0.3) * 3.0;
    vec2 kp = kaleidoscope(p, segments);
    
    // Zoom effect
    float zoom = 1.0 + sin(t * 0.5) * 0.5;
    kp *= zoom;
    
    // Layer multiple kaleidoscope patterns
    vec2 kp2 = kaleidoscope(kp * 1.5, 6.0);
    vec2 kp3 = kaleidoscope(kp * 2.5, 12.0);
    
    // Generate fractal pattern
    float n1 = noise(kp * 5.0 + vec2(t * 0.1, -t * 0.15));
    float n2 = noise(kp2 * 8.0 - vec2(t * 0.15, t * 0.1));
    float n3 = noise(kp3 * 12.0 + vec2(sin(t * 0.2), cos(t * 0.25)));
    
    float pattern = n1 * 0.5 + n2 * 0.3 + n3 * 0.2;
    
    // Concentric rings
    float rings = abs(sin(length(kp) * 15.0 - t * 2.0));
    pattern += rings * 0.3;
    
    // Color mapping
    vec3 col = u_palette_low;
    col = mix(col, u_palette_mid, smoothstep(0.3, 0.5, pattern));
    col = mix(col, u_palette_high, smoothstep(0.6, 0.9, pattern));
    
    // Add bright highlights
    col += u_palette_high * pow(pattern, 4.0) * u_intensity;
    
    // Edge glow
    float edge_glow = pow(rings, 8.0);
    col += u_palette_mid * edge_glow * 0.5;
    
    fragColor = vec4(col, 1.0);
}
)"
    },
    
    {
        16,
        "Fire Shader",
        "Realistic fire with smoke and embers",
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

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1,0)), u.x),
               mix(hash(i + vec2(0,1)), hash(i + vec2(1,1)), u.x), u.y);
}

float fbm(vec2 p) {
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 6; i++) {
        v += noise(p) * a;
        p *= 2.13;
        p.y += u_time * u_speed * 0.3;
        a *= 0.5;
    }
    return v;
}

void main() {
    vec2 p = uv;
    p.x = (p.x * 2.0 - 1.0) * (u_resolution.x / u_resolution.y);
    float t = u_time * u_speed;
    
    // Rising flames
    vec2 fire_uv = p;
    fire_uv.y -= t * 0.5;
    fire_uv.y += sin(p.x * 5.0 + t) * 0.1;
    
    // Turbulent noise for flame shape
    float turb1 = fbm(fire_uv * vec2(3.0, 5.0));
    float turb2 = fbm(fire_uv * vec2(5.0, 8.0) + vec2(23.7, 11.3));
    
    // Flame shape
    float flame = turb1 * 0.7 + turb2 * 0.3;
    flame = pow(flame, 2.0);
    
    // Height falloff
    float height = smoothstep(1.2, 0.0, p.y);
    flame *= height;
    
    // Bottom intensity
    float bottom_intensity = smoothstep(0.4, -0.2, p.y);
    flame += bottom_intensity * 0.3;
    
    // Color gradient: white (hottest) -> yellow -> orange -> red -> black
    vec3 col = vec3(0.0);
    
    if (flame > 0.8) {
        // White hot core
        col = mix(u_palette_high, vec3(1.0), (flame - 0.8) * 5.0);
    } else if (flame > 0.5) {
        // Yellow-orange
        col = mix(u_palette_mid, u_palette_high, (flame - 0.5) * 3.33);
    } else if (flame > 0.2) {
        // Orange-red
        col = mix(u_palette_low, u_palette_mid, (flame - 0.2) * 3.33);
    } else {
        // Dark red to black
        col = u_palette_low * (flame * 5.0);
    }
    
    col *= u_intensity;
    
    // Add dancing embers
    for (int i = 0; i < 16; i++) {
        float fi = float(i);
        vec2 ember_seed = vec2(fi * 7.13, fi * 3.71);
        float ember_x = hash(ember_seed) * 2.0 - 1.0;
        float ember_phase = fract(hash(ember_seed + 5.5) + t * 0.3);
        float ember_y = ember_phase * 1.5 - 0.3;
        
        vec2 ember_pos = vec2(ember_x * 0.8, ember_y);
        ember_pos.x += sin(ember_y * 10.0 + t * 2.0) * 0.1;
        
        float ember_d = length(p - ember_pos);
        float ember = smoothstep(0.015, 0.0, ember_d);
        ember *= smoothstep(0.0, 0.2, ember_phase) * smoothstep(1.0, 0.7, ember_phase);
        
        col += u_palette_high * ember * 2.0;
    }
    
    fragColor = vec4(col, 1.0);
}
)"
    },
    
    {
        17,
        "Cosmic Nebula Volumetric",
        "Volumetric galaxy clouds with stars and cinematic grading",
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
uniform float u_warp;

uniform float u_exposure_base;
uniform float u_fade_base;

#define VOL_STEPS 20
#define STAR_LAYERS 3
#define PI 3.14159265359

float hash11(float p)
{
    p = fract(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return fract(p);
}

float hash31(vec3 p)
{
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

float hash21(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

mat2 rot(float a)
{
    float s = sin(a);
    float c = cos(a);
    return mat2(c, -s, s, c);
}

float noise3(vec3 p)
{
    vec3 i = floor(p);
    vec3 f = fract(p);

    f = f * f * (3.0 - 2.0 * f);

    float n000 = hash31(i + vec3(0.0, 0.0, 0.0));
    float n100 = hash31(i + vec3(1.0, 0.0, 0.0));
    float n010 = hash31(i + vec3(0.0, 1.0, 0.0));
    float n110 = hash31(i + vec3(1.0, 1.0, 0.0));
    float n001 = hash31(i + vec3(0.0, 0.0, 1.0));
    float n101 = hash31(i + vec3(1.0, 0.0, 1.0));
    float n011 = hash31(i + vec3(0.0, 1.0, 1.0));
    float n111 = hash31(i + vec3(1.0, 1.0, 1.0));

    float nx00 = mix(n000, n100, f.x);
    float nx10 = mix(n010, n110, f.x);
    float nx01 = mix(n001, n101, f.x);
    float nx11 = mix(n011, n111, f.x);

    float nxy0 = mix(nx00, nx10, f.y);
    float nxy1 = mix(nx01, nx11, f.y);

    return mix(nxy0, nxy1, f.z);
}

float fbm3(vec3 p)
{
    float v = 0.0;
    float a = 0.5;

    for (int i = 0; i < 4; i++)
    {
        v += noise3(p) * a;
        p = p * 2.03 + vec3(17.1, 9.2, 11.7);
        p.xy = rot(0.47) * p.xy;
        p.yz = rot(0.31) * p.yz;
        a *= 0.52;
    }

    return v;
}

float warpedFbm(vec3 p, float t)
{
    vec3 q = p;

    q.x += noise3(p * 1.6 + vec3(0.0, t * 0.10, 0.0)) * 2.0 - 1.0;
    q.y += noise3(p * 1.8 + vec3(t * 0.08, 4.0, 0.0)) * 2.0 - 1.0;
    q.z += noise3(p * 1.3 + vec3(0.0, 0.0, t * 0.06)) * 2.0 - 1.0;

    return fbm3(mix(p, q, 0.22 + u_warp * 0.55));
}

float nebulaDensity(vec3 p, float t)
{
    p.xy = rot(t * 0.035 + p.z * 0.18) * p.xy;

    float r = length(p.xy);
    float a = atan(p.y, p.x);

    float disk = exp(-abs(p.z) * 2.8) * exp(-r * 0.42);
    float core = exp(-r * 2.8);

    float arms = sin(a * 4.0 + r * 7.5 - t * 0.55);
    arms = smoothstep(-0.35, 0.95, arms);

    float clouds = warpedFbm(p * 1.15 + vec3(0.0, 0.0, t * 0.08), t);
    float d = smoothstep(0.40, 0.88, clouds);

    d *= disk * (0.55 + arms * 1.55);
    d += core * 0.18;

    return clamp(d, 0.0, 1.0);
}

vec3 palette(float x)
{
    vec3 c = mix(u_palette_low, u_palette_mid, smoothstep(0.0, 0.65, x));
    c = mix(c, u_palette_high, smoothstep(0.55, 1.0, x));
    return c;
}

float starLayer(vec2 p, float scale, float threshold, float t)
{
    p *= scale;

    vec2 id = floor(p);
    vec2 gv = fract(p) - 0.5;

    float rnd = hash21(id);
    vec2 jitter = vec2(hash21(id + 12.7), hash21(id + 78.3)) - 0.5;

    gv -= jitter * 0.75;

    float d = length(gv);
    float star = smoothstep(0.045, 0.0, d);
    float flare = smoothstep(0.025, 0.0, abs(gv.x)) * smoothstep(0.30, 0.0, abs(gv.y));
    flare += smoothstep(0.025, 0.0, abs(gv.y)) * smoothstep(0.30, 0.0, abs(gv.x));

    float twinkle = 0.65 + 0.35 * sin(t * (3.0 + rnd * 7.0) + rnd * 40.0);

    return (star + flare * 0.18) * step(threshold, rnd) * twinkle;
}

float starField(vec2 p, float t)
{
    float s = 0.0;

    s += starLayer(p + vec2(t * 0.003, -t * 0.002), 45.0, 0.965, t);
    s += starLayer(p + vec2(-t * 0.004, t * 0.003), 85.0, 0.975, t);
    s += starLayer(p + vec2(t * 0.006, t * 0.004), 135.0, 0.983, t);

    return s;
}

void main()
{
    vec2 aspect = vec2(u_resolution.x / u_resolution.y, 1.0);
    vec2 p = (uv - 0.5) * aspect;

    float t = u_time * u_speed;
    float intensity = max(u_intensity, 0.001);

    vec2 pp = p;
    pp += vec2(
        noise3(vec3(p * 2.0, t * 0.06)),
        noise3(vec3(p.yx * 2.0, t * 0.05 + 4.0))
    ) * 0.06 * u_warp;

    vec3 ro = vec3(0.0, 0.0, -4.2);
    vec3 rd = normalize(vec3(pp * 1.25, 1.75));

    ro.xz = rot(sin(t * 0.08) * 0.18) * ro.xz;
    rd.xz = rot(sin(t * 0.08) * 0.18) * rd.xz;
    rd.xy = rot(sin(t * 0.05) * 0.08) * rd.xy;

    vec4 acc = vec4(0.0);

    float travel = 0.0;
    float stepSize = 0.18;

    for (int i = 0; i < VOL_STEPS; i++)
    {
        vec3 pos = ro + rd * travel;

        float d = nebulaDensity(pos, t);
        float r = length(pos.xy);

        float glowCore = exp(-r * 3.4 - abs(pos.z) * 1.7);
        float rim = exp(-abs(r - 1.25) * 3.2) * exp(-abs(pos.z) * 2.1);

        vec3 c = palette(d);
        c += u_palette_high * glowCore * 2.4;
        c += u_palette_mid * rim * 0.35;

        float alpha = clamp(d * 0.105, 0.0, 0.22);
        alpha *= 1.0 - acc.a;

        acc.rgb += c * alpha;
        acc.a += alpha;

        travel += stepSize;

        if (acc.a > 0.93)
            break;
    }

    float stars = starField(p, t);
    vec3 starCol = mix(u_palette_mid, u_palette_high, clamp(stars, 0.0, 1.0));

    vec3 col = u_palette_low * 0.045;
    col += acc.rgb * 2.15;
    col += starCol * stars * 2.4;

    float centerGlow = exp(-length(p) * 3.2);
    col += u_palette_high * centerGlow * 0.26;

    float dust = noise3(vec3(p * 14.0, t * 0.02));
    dust = smoothstep(0.68, 0.95, dust);
    col += u_palette_mid * dust * acc.a * 0.16;

    float vignette = smoothstep(1.25, 0.08, length(p));
    col *= vignette;

    col *= intensity;
    col *= max(u_exposure_base, 0.0);

    col = 1.0 - exp(-col);
    col = pow(col, vec3(0.92));

    fragColor = vec4(col, u_fade_base);
}
)"
    },

    {
        18,
        "Star Scroller 360",
        "Directional starfield scroller (Warp controls 0-360 direction)",
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
uniform float u_warp;
uniform float u_exposure_base;
uniform float u_fade_base;

#define PI 3.14159265359

float hash21(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float starLayer(vec2 p, vec2 dir, vec2 perp, float t_scroll, float t_twinkle, float scale, float threshold, float base_size) {
    vec2 q = (p + dir * t_scroll * (0.35 + scale * 0.0015)) * scale;
    vec2 id = floor(q);
    vec2 gv = fract(q) - 0.5;

    float rnd = hash21(id);
    vec2 jitter = vec2(hash21(id + 12.7), hash21(id + 78.3)) - 0.5;
    gv -= jitter * 0.8;

    float along = dot(gv, dir);
    float across = dot(gv, perp);

    float core = smoothstep(base_size, 0.0, length(gv));
    float streak = smoothstep(base_size * 1.8, 0.0, abs(across)) *
                   smoothstep(base_size * 10.0, 0.0, abs(along));

    float phase_a = hash21(id + 31.7);
    float phase_b = hash21(id + 83.9);
    float rate_a = mix(0.10, 0.30, hash21(id + 11.3));
    float rate_b = mix(0.03, 0.09, hash21(id + 57.1));

    float shimmer = 0.86 + 0.14 * sin(t_twinkle * (6.2831853 * rate_a) + phase_a * 6.2831853);
    float glint_cycle = fract(t_twinkle * rate_b + phase_b);
    float glint = smoothstep(0.00, 0.38, glint_cycle) * (1.0 - smoothstep(0.86, 1.00, glint_cycle));
    glint = mix(0.62, 1.00, glint);

    float twinkle = shimmer * glint;
    return max(core, streak * 0.75) * step(threshold, rnd) * twinkle;
}

void main() {
    vec2 aspect = vec2(u_resolution.x / u_resolution.y, 1.0);
    vec2 p = (uv - 0.5) * aspect;

    float t = u_time * max(u_speed, 0.001);
    float angle = fract(u_warp) * (2.0 * PI);
    vec2 dir = vec2(cos(angle), sin(angle));
    vec2 perp = vec2(-dir.y, dir.x);

    float tw_t = u_time;
    float layer_far = starLayer(p, dir, perp, t, tw_t, 42.0, 0.970, 0.050);
    float layer_mid = starLayer(p + perp * 0.13, dir, perp, t * 1.35, tw_t, 75.0, 0.978, 0.040);
    float layer_near = starLayer(p - perp * 0.18, dir, perp, t * 1.95, tw_t, 120.0, 0.985, 0.033);

    float stars = layer_far + layer_mid * 1.25 + layer_near * 1.6;

    float lane = 0.5 + 0.5 * sin(dot(p, perp) * 3.5 + t * 0.6);
    vec3 bg = mix(u_palette_low * 0.10, u_palette_mid * 0.14, lane);

    vec3 star_col = mix(u_palette_mid, u_palette_high, clamp(stars, 0.0, 1.0));
    vec3 col = bg + star_col * stars * (1.1 + 0.9 * u_intensity);

    float vignette = smoothstep(1.25, 0.10, length(p));
    col *= vignette;
    col *= max(u_exposure_base, 0.0);
    col = 1.0 - exp(-col);

    fragColor = vec4(col, u_fade_base);
}
)"
    }

    ,
    {
        19,
        "Kaleido Fracture",
        "Recursive mirrored shards with chromatic separation and hard pulses",
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
uniform float u_warp;

#define PI 3.14159265359

void main() {
    vec2 p = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0);
    float t = u_time * u_speed;
    float pulse = 0.5 + 0.5 * sin(t * 1.7);
    float angle_offset = t * 0.18 + u_warp * 2.4;
    float shard = 0.0;
    float energy = 0.0;

    for (int i = 0; i < 6; ++i) {
        float fi = float(i);
        float radius = length(p) + 0.001;
        float angle = atan(p.y, p.x) + angle_offset + fi * PI / 3.0;
        angle = mod(angle, 2.0 * PI / 3.0) - PI / 3.0;
        vec2 q = vec2(cos(angle), sin(angle)) * radius;
        q.x -= 0.18 + pulse * 0.08;
        q.y += sin(t * 1.2 + fi) * 0.06 * u_warp;

        float crack = abs(sin(q.x * (18.0 + fi * 5.0) + q.y * 7.0 - t * (2.0 + fi * 0.3)));
        float shard_edge = smoothstep(0.94, 1.0, crack);
        float ring = smoothstep(0.16, 0.0, abs(fract(radius * (3.0 + fi * 0.45) - t * 0.08) - 0.5));
        shard += shard_edge * (0.25 + ring * 0.9) / (1.0 + fi * 0.35);
        energy += ring * (0.35 + shard_edge);
    }

    float split = 0.5 + 0.5 * sin(atan(p.y, p.x) * 9.0 - t * 2.0);
    vec3 col = mix(u_palette_low * 0.08, u_palette_mid, smoothstep(0.1, 0.8, energy));
    col = mix(col, u_palette_high, clamp(shard * 0.8 + split * energy * 0.35, 0.0, 1.0));
    col.r += shard * (0.25 + 0.25 * split) * u_intensity;
    col.b += energy * (0.16 + 0.22 * (1.0 - split)) * u_intensity;
    col *= 0.82 + 0.18 * sin(t * 8.0 + length(p) * 20.0);
    col *= smoothstep(1.45, 0.15, length(p));

    fragColor = vec4(max(col, vec3(0.0)) * (0.8 + u_intensity * 0.45), 1.0);
}
 )"
    },

    {
        20,
        "Signal Riot",
        "Aggressive scanline interference with block tearing and RGB misalignment",
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
uniform float u_warp;

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float signal(vec2 p, float t, float channel_shift) {
    float bands = sin(p.y * 72.0 + t * 5.0 + channel_shift);
    bands += sin(p.y * 181.0 - t * 8.0) * 0.35;
    bands += sin(p.x * 13.0 + p.y * 37.0 + t * 2.0) * 0.22;
    float blocks = step(0.68, hash21(vec2(floor(p.y * 22.0), floor(t * 2.5))));
    float tear = hash21(vec2(floor(p.y * 38.0), floor(t * 3.0))) - 0.5;
    float offset = tear * blocks * u_warp * 0.34;
    float flare = exp(-abs(fract(p.x + offset + sin(t + p.y * 4.0) * 0.08) - 0.5) * 18.0);
    return bands * 0.5 + flare * 1.8 + blocks * 0.18;
}

void main() {
    vec2 p = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0);
    float t = u_time * u_speed;
    float r = signal(p + vec2(0.035 * u_warp, 0.0), t, 1.7);
    float g = signal(p, t, 0.0);
    float b = signal(p - vec2(0.035 * u_warp, 0.0), t, -1.7);
    vec3 col = max(vec3(r, g, b), vec3(0.0));
    col = mix(u_palette_low * 0.08 + col * u_palette_mid, col * u_palette_high * 1.8, 0.5 + 0.5 * sin(t * 0.7));

    float scanline = 0.72 + 0.28 * sin(uv.y * u_resolution.y * 1.4);
    float vignette = smoothstep(1.5, 0.25, length(p));
    col *= scanline * vignette * (0.65 + u_intensity * 0.65);
    col += u_palette_high * step(0.93, hash21(vec2(floor(uv.y * 90.0), floor(t * 12.0)))) * u_intensity;

    fragColor = vec4(col, 1.0);
}
 )"
    },

    {
        21,
        "Electric Vortex",
        "Coiling lightning filaments around a breathing singularity",
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
uniform float u_warp;

#define PI 3.14159265359

void main() {
    vec2 p = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0);
    float t = u_time * u_speed;
    float r = length(p);
    float a = atan(p.y, p.x);
    float spiral = a + log(r + 0.08) * (3.5 + u_warp * 5.0) - t * 1.8;
    float filament = 0.0;
    float sparks = 0.0;

    for (int i = 0; i < 5; ++i) {
        float fi = float(i);
        float lane = sin(spiral * (4.0 + fi * 1.7) + sin(t * 1.4 + fi) * 2.0);
        float distance_to_lane = abs(lane) * (0.025 + r * 0.11);
        filament += exp(-distance_to_lane * 85.0) * (1.0 - fi * 0.13);
        sparks += pow(max(0.0, lane), 18.0) * smoothstep(1.25, 0.18, r);
    }

    float core = exp(-r * (7.0 - sin(t * 2.0) * 1.2));
    float event_horizon = smoothstep(0.18, 0.06, r + sin(a * 5.0 - t * 3.0) * 0.018 * u_warp);
    vec3 col = u_palette_low * (0.05 + core * 0.3);
    col += u_palette_mid * filament * (0.55 + u_intensity * 0.55);
    col += u_palette_high * (filament * filament * 0.8 + sparks * 0.35 + core * 0.7);
    col *= 1.0 - event_horizon * 0.85;
    col += u_palette_high * exp(-abs(r - 0.21) * 30.0) * (0.35 + 0.25 * sin(t * 4.0));
    col *= smoothstep(1.35, 0.12, r);

    fragColor = vec4(max(col, vec3(0.0)), 1.0);
}
 )"
    }

    ,
    {
        22,
        "Warped Cathedral",
        "Domain-warped arches and volumetric light shafts with recursive fBm detail",
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
uniform float u_warp;

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float noise(vec2 p) {
    vec2 cell = floor(p);
    vec2 local = fract(p);
    local = local * local * (3.0 - 2.0 * local);
    float a = hash21(cell);
    float b = hash21(cell + vec2(1.0, 0.0));
    float c = hash21(cell + vec2(0.0, 1.0));
    float d = hash21(cell + vec2(1.0, 1.0));
    return mix(mix(a, b, local.x), mix(c, d, local.x), local.y);
}

float fbm(vec2 p) {
    float value = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < 5; ++i) {
        value += amplitude * noise(p);
        p = p * 2.03 + vec2(17.1, -9.2);
        amplitude *= 0.5;
    }
    return value;
}

mat2 rotate(float angle) {
    float s = sin(angle);
    float c = cos(angle);
    return mat2(c, -s, s, c);
}

void main() {
    vec2 p = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0);
    float t = u_time * u_speed;
    vec2 warped = p;
    float warp_amount = 0.18 + u_warp * 0.48;

    // Nested domain warps turn a simple arch field into a moving architectural texture.
    for (int i = 0; i < 3; ++i) {
        float fi = float(i);
        warped += vec2(
            sin(warped.y * (3.0 + fi * 2.5) + t * (0.35 + fi * 0.18)),
            cos(warped.x * (4.0 + fi * 1.7) - t * (0.28 + fi * 0.16))
        ) * warp_amount / (fi + 1.0);
        warped = rotate(0.12 * sin(t * 0.35 + fi)) * warped;
    }

    float n = fbm(warped * 3.2 + vec2(t * 0.08, -t * 0.11));
    float columns = abs(sin(warped.x * 9.0 + sin(warped.y * 4.0 + t) * 0.8));
    float arches = abs(sin(warped.y * 7.0 - warped.x * 3.0 + t * 0.55 + n * 3.0));
    float ribs = smoothstep(0.88, 1.0, columns) + smoothstep(0.91, 1.0, arches);

    float ray_angle = atan(warped.y, warped.x);
    float rays = pow(max(0.0, cos(ray_angle * 7.0 + t * 0.4 + n * 2.0)), 18.0);
    rays *= smoothstep(1.45, 0.08, length(warped));

    float void_shape = smoothstep(0.16, 0.38, length(warped + vec2(0.0, 0.12 * sin(t))));
    vec3 col = mix(u_palette_low * 0.08, u_palette_mid, clamp(n * 0.9 + ribs * 0.22, 0.0, 1.0));
    col += u_palette_mid * ribs * (0.3 + u_intensity * 0.45);
    col += u_palette_high * (rays * (0.55 + u_intensity * 0.75) + pow(ribs, 3.0) * 0.8);
    col = mix(col, col.brg, 0.22 * sin(t * 0.8 + warped.x * 3.0));
    col *= 0.65 + void_shape * 0.35;
    col *= smoothstep(1.5, 0.12, length(p));

    fragColor = vec4(max(col, vec3(0.0)), 1.0);
}
 )"
    }

    ,
    {
        23,
        "3D Checkerboard",
        "Perspective checkerboard plane with XYZ placement, rotation, and texture travel",
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
uniform float u_warp;
uniform vec3 u_position;
uniform vec3 u_rotation;
uniform vec3 u_motion;

#define PI 3.14159265359

vec3 rotate_xyz(vec3 p, vec3 angles) {
    float cx = cos(angles.x), sx = sin(angles.x);
    float cy = cos(angles.y), sy = sin(angles.y);
    float cz = cos(angles.z), sz = sin(angles.z);
    p.yz = mat2(cx, -sx, sx, cx) * p.yz;
    p.xz = mat2(cy, -sy, sy, cy) * p.xz;
    p.xy = mat2(cz, -sz, sz, cz) * p.xy;
    return p;
}

void main() {
    vec2 screen = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0);
    float t = u_time * u_speed;
    vec3 q = vec3(screen, 0.0) - u_position;
    q = rotate_xyz(q, -u_rotation * PI / 180.0);
    q += u_motion * t;

    float perspective = 1.0 / max(0.18, 1.0 + q.z * 0.65);
    vec2 board = q.xy * perspective * (5.0 + u_warp * 7.0);
    vec2 cell = floor(board);
    float parity = mod(cell.x + cell.y, 2.0);
    vec3 dark = mix(u_palette_low * 0.22, u_palette_mid * 0.28, 0.35);
    vec3 light = mix(u_palette_mid, u_palette_high, 0.68);
    vec3 col = mix(dark, light, parity);

    vec2 edge = abs(fract(board) - 0.5);
    float grout = smoothstep(0.46, 0.50, max(edge.x, edge.y));
    col = mix(col * 0.35, col, grout);
    float scan = 0.92 + 0.08 * sin(board.y * 3.14159 + t * 2.0);
    col *= scan * (0.72 + u_intensity * 0.38);
    col *= smoothstep(1.55, 0.35, length(screen));

    fragColor = vec4(max(col, vec3(0.0)), 1.0);
}
 )"
    },

    {
        24,
        "3D Fog Volume",
        "Moving rotated volumetric fog with controllable XYZ placement and travel",
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
uniform float u_warp;
uniform vec3 u_position;
uniform vec3 u_rotation;
uniform vec3 u_motion;

float hash31(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

float noise3(vec3 p) {
    vec3 cell = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = hash31(cell);
    float n100 = hash31(cell + vec3(1.0, 0.0, 0.0));
    float n010 = hash31(cell + vec3(0.0, 1.0, 0.0));
    float n110 = hash31(cell + vec3(1.0, 1.0, 0.0));
    float n001 = hash31(cell + vec3(0.0, 0.0, 1.0));
    float n101 = hash31(cell + vec3(1.0, 0.0, 1.0));
    float n011 = hash31(cell + vec3(0.0, 1.0, 1.0));
    float n111 = hash31(cell + vec3(1.0, 1.0, 1.0));
    return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
               mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}

vec3 rotate_xyz(vec3 p, vec3 angles) {
    float cx = cos(angles.x), sx = sin(angles.x);
    float cy = cos(angles.y), sy = sin(angles.y);
    float cz = cos(angles.z), sz = sin(angles.z);
    p.yz = mat2(cx, -sx, sx, cx) * p.yz;
    p.xz = mat2(cy, -sy, sy, cy) * p.xz;
    p.xy = mat2(cz, -sz, sz, cz) * p.xy;
    return p;
}

void main() {
    vec2 screen = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0);
    float t = u_time * u_speed;
    vec3 origin = vec3(screen, 0.0) - u_position;
    origin = rotate_xyz(origin, -u_rotation * 3.14159265 / 180.0);
    origin += u_motion * t;

    vec3 ray = normalize(vec3(screen * 0.35, 1.0));
    float density = 0.0;
    float glow = 0.0;
    for (int i = 0; i < 28; ++i) {
        float fi = float(i);
        vec3 sample_pos = origin + ray * (fi * 0.085);
        float n = noise3(sample_pos * (2.3 + u_warp * 2.7) + vec3(t * 0.12, -t * 0.08, t * 0.16));
        float pocket = smoothstep(0.40, 0.78, n);
        float height = 0.5 + 0.5 * sin(sample_pos.y * 3.0 + t + n * 4.0);
        density += pocket * (0.65 + height * 0.55);
        glow += exp(-length(sample_pos.xy) * 2.6) * pocket;
    }

    density /= 28.0;
    glow /= 28.0;
    vec3 col = mix(u_palette_low * 0.06, u_palette_mid, smoothstep(0.08, 0.48, density));
    col = mix(col, u_palette_high, clamp(density * 1.65 + glow * 0.7, 0.0, 1.0));
    col += u_palette_high * glow * (0.35 + u_intensity * 0.55);
    col *= 0.72 + u_intensity * 0.48;
    col *= smoothstep(1.45, 0.08, length(screen));

    fragColor = vec4(max(col, vec3(0.0)), 1.0);
}
 )"
    }
};

const int g_shader_preset_count = sizeof(g_shader_presets) / sizeof(g_shader_presets[0]);

const char* GetPostEffectFragmentSource() {
    return R"(
#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform sampler2D u_scene;
uniform sampler2D u_history;
uniform int u_unpremultiply_scene;
uniform vec2 u_resolution;
uniform float u_time;
uniform int u_enabled[23];
uniform float u_intensity[23];
uniform float u_threshold[23];
uniform float u_radius[23];
uniform vec4 u_color[23];

vec3 SampleScene(vec2 coord) {
    vec4 sample_color = texture(u_scene, clamp(coord, vec2(0.0), vec2(1.0)));
    if (u_unpremultiply_scene != 0) {
        return sample_color.rgb / max(sample_color.a, 0.001);
    }
    return sample_color.rgb;
}

float SampleSceneAlpha(vec2 coord) {
    return texture(u_scene, clamp(coord, vec2(0.0), vec2(1.0))).a;
}

void main() {
    vec2 coord = uv;
    vec2 texel = 1.0 / u_resolution;
    if (u_enabled[10] != 0) {
        float shake = u_intensity[10] * 0.003;
        coord += vec2(sin(u_time * 31.0), cos(u_time * 37.0)) * shake;
    }
    float scene_alpha = SampleSceneAlpha(coord);
    if (scene_alpha <= 0.001 && u_unpremultiply_scene == 1) {
        fragColor = vec4(0.0);
        return;
    }
    vec3 color = SampleScene(coord);
    if (u_enabled[2] != 0) {
        vec3 bloom = vec3(0.0);
        float bloom_alpha = scene_alpha;
        float radius = max(u_radius[2], 0.5);
        for (int x = -2; x <= 2; ++x) for (int y = -2; y <= 2; ++y) {
            vec2 sample_coord = coord + vec2(x, y) * texel * radius;
            vec3 sample_color = SampleScene(sample_coord);
            bloom += max(sample_color - vec3(u_threshold[2]), vec3(0.0));
            bloom_alpha = max(bloom_alpha, SampleSceneAlpha(sample_coord));
        }
        color += bloom * (u_intensity[2] / 25.0);
        scene_alpha = bloom_alpha;
    }
    if (u_enabled[9] != 0) {
        float shift = u_intensity[9] * 0.004;
        vec3 shifted_right = SampleScene(coord + vec2(shift, 0.0));
        vec3 shifted_left = SampleScene(coord - vec2(shift, 0.0));
        color.r = shifted_right.r;
        color.b = shifted_left.b;
        scene_alpha = max(scene_alpha,
                          max(SampleSceneAlpha(coord + vec2(shift, 0.0)),
                              SampleSceneAlpha(coord - vec2(shift, 0.0))));
    }
    if (u_enabled[8] != 0) {
        vec3 luma = vec3(0.299, 0.587, 0.114);
        float center = dot(color, luma);
        float edge = abs(center - dot(SampleScene(coord + vec2(texel.x, 0.0)), luma));
        edge += abs(center - dot(SampleScene(coord + vec2(0.0, texel.y)), luma));
        color = mix(color, vec3(center), clamp(edge * u_intensity[8] * 2.0, 0.0, 0.35));
    }
    if (u_enabled[4] != 0) color = mix(color, color * u_color[4].rgb, clamp(u_intensity[4], 0.0, 1.0));
    if (u_enabled[7] != 0) {
        float fog = 1.0 - exp(-max(uv.y, 0.0) * u_intensity[7] * 3.0);
        color = mix(color, u_color[7].rgb, clamp(fog, 0.0, 1.0));
    }
    if (u_enabled[3] != 0) {
        vec2 centered = uv * 2.0 - 1.0;
        float vignette = smoothstep(1.2, 0.2, dot(centered, centered));
        color *= mix(1.0 - u_intensity[3], 1.0, vignette);
    }
    if (u_enabled[5] != 0 || u_enabled[6] != 0) {
        float noise = fract(sin(dot(uv * u_resolution + u_time, vec2(12.9898, 78.233))) * 43758.5453) - 0.5;
        color += noise * (u_intensity[5] * 0.08 + u_intensity[6] * 0.025);
    }
    if (u_enabled[1] != 0) {
        vec3 tone_input = max(color, vec3(0.0));
        color = tone_input / (tone_input + vec3(1.0));
        color = pow(color, vec3(1.0 / 2.2));
    }
    if (u_enabled[0] != 0) {
        vec3 tone_input = max(color, vec3(0.0));
        color = tone_input / (tone_input + vec3(1.0));
    }
    if (u_enabled[11] != 0) color += u_color[11].rgb * u_intensity[11] * max(0.0, sin(u_time * 31.416));
    if (u_enabled[12] != 0) color *= 1.0 - clamp(u_intensity[12], 0.0, 1.0);
    if (u_enabled[13] != 0) {
        vec2 centered = coord * 2.0 - 1.0;
        float barrel = dot(centered, centered) * u_intensity[13] * 0.08;
        vec2 warped = centered * (1.0 + barrel) * 0.5 + 0.5;
        color = SampleScene(warped);
        scene_alpha = SampleSceneAlpha(warped);
        float scan = 0.94 + 0.06 * sin(uv.y * u_resolution.y * 1.5708);
        color *= scan;
    }
    if (u_enabled[14] != 0) {
        float scan = 0.75 + 0.25 * sin(uv.y * u_resolution.y * 3.14159);
        color *= mix(1.0, scan, clamp(u_intensity[14], 0.0, 1.0));
    }
    if (u_enabled[15] != 0) {
        vec2 centered = coord * 2.0 - 1.0;
        float r2 = dot(centered, centered);
        vec2 warped = centered * (1.0 + r2 * u_intensity[15] * 0.12) * 0.5 + 0.5;
        color = mix(color, SampleScene(warped), clamp(u_intensity[15], 0.0, 1.0));
        scene_alpha = mix(scene_alpha, SampleSceneAlpha(warped), clamp(u_intensity[15], 0.0, 1.0));
    }
    if (u_enabled[16] != 0) {
        float phase = sin(u_time * 1.7 * max(u_intensity[16], 0.05)) * 0.5 + 0.5;
        color = mix(color, color.gbr, phase * clamp(u_intensity[16], 0.0, 1.0) * 0.35);
    }
    if (u_enabled[17] != 0) {
        float heat = sin(uv.y * 31.0 + u_time * 5.0) * sin(uv.x * 23.0 - u_time * 3.0);
        vec2 warped = coord + vec2(heat, -heat) * texel * u_intensity[17] * 8.0;
        float distortion_amount = clamp(u_intensity[17], 0.0, 1.0);
        color = mix(color, SampleScene(warped), distortion_amount);
        scene_alpha = mix(scene_alpha, SampleSceneAlpha(warped), distortion_amount);
    }
    if (u_enabled[18] != 0) {
        float block = floor(uv.y * 32.0);
        float glitch = step(0.82, fract(sin(block * 12.9898 + floor(u_time * 12.0)) * 43758.5453));
        vec2 warped = coord + vec2(glitch * sin(block * 4.7 + u_time * 9.0), 0.0) * texel * u_intensity[18] * 24.0;
        float glitch_amount = glitch * clamp(u_intensity[18], 0.0, 1.0);
        color = mix(color, SampleScene(warped), glitch_amount);
        scene_alpha = mix(scene_alpha, SampleSceneAlpha(warped), glitch_amount);
    }
    if (u_enabled[19] != 0) {
        vec3 glow = max(color - vec3(u_threshold[19]), vec3(0.0));
        color += glow * (0.5 + 0.5 * sin(u_time * 6.28318)) * u_intensity[19] * 0.25;
    }
    if (u_enabled[20] != 0) {
        vec3 previous = texture(u_history, coord).rgb;
        color = mix(color, previous, clamp(u_intensity[20] * 0.85, 0.0, 0.95));
    }
    if (u_enabled[21] != 0) {
        vec2 centered = coord * 2.0 - 1.0;
        float zoom = 1.0 + sin(u_time * 2.0) * u_intensity[21] * 0.03;
        vec2 zoomed = centered / max(zoom, 0.01) * 0.5 + 0.5;
        vec3 previous = texture(u_history, clamp(zoomed, vec2(0.0), vec2(1.0))).rgb;
        color = mix(color, previous, clamp(u_intensity[21] * 0.75, 0.0, 0.9));
    }
    if (u_enabled[22] != 0) {
        vec2 centered = coord * 2.0 - 1.0;
        float zoom = 1.0 + u_intensity[22] * 0.025;
        vec2 recursive_uv = centered / max(zoom, 0.01) * 0.5 + 0.5;
        vec3 previous = texture(u_history, clamp(recursive_uv, vec2(0.0), vec2(1.0))).rgb;
        color = mix(color, previous * (0.8 + 0.2 * sin(u_time * 3.0)), clamp(u_intensity[22] * 0.8, 0.0, 0.92));
    }
    // Sprite layers store straight RGB with meaningful alpha. Preserve that
    // alpha for both layer-local and full-scene post-processing passes.
    float output_alpha = scene_alpha;
    fragColor = vec4(max(color, vec3(0.0)), output_alpha);
}
)";
}

const char* GetShaderSourceById(int shader_id) {
    for (int i = 0; i < g_shader_preset_count; ++i) {
        if (g_shader_presets[i].id == shader_id) {
            return g_shader_presets[i].fragment_source;
        }
    }
    return nullptr;
}

const ShaderPreset* GetPresetById(int shader_id) {
    for (int i = 0; i < g_shader_preset_count; ++i) {
        if (g_shader_presets[i].id == shader_id) {
            return &g_shader_presets[i];
        }
    }
    return nullptr;
}

} // namespace editor
} // namespace rev
