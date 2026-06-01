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
    // Three horizontal bands with left-to-right color fades
    // Bottom band: palette_low
    // Middle band: palette_mid
    // Top band: palette_high
    // Default (black): all palette colors at (0,0,0)
    
    float y = uv.y;  // 0 at bottom, 1 at top
    float x = uv.x;  // 0 at left, 1 at right (for horizontal fade)
    
    vec3 col;
    
    // Bottom third (0.0 - 0.33): palette_low fades from black to full color
    if (y < 0.33) {
        col = u_palette_low * x;
    }
    // Middle third (0.33 - 0.66): palette_mid fades from black to full color
    else if (y < 0.66) {
        col = u_palette_mid * x;
    }
    // Top third (0.66 - 1.0): palette_high fades from black to full color
    else {
        col = u_palette_high * x;
    }
    
    fragColor = vec4(col, 1.0);
}
)"
    },
    
    {
        1,
        "Plasma Vibrant",
        "Colorful plasma effect",
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
    
    float v = sin(p.x * 10.0 + t) + sin(p.y * 10.0 + t * 0.5) + sin((p.x + p.y) * 10.0 + t * 0.3);
    v = (v + 3.0) / 6.0;
    
    vec3 col = mix(mix(u_palette_low, u_palette_mid, v), u_palette_high, smoothstep(0.5, 1.0, v));
    
    fragColor = vec4(col, 1.0);
}
)"
    },
    
    {
        2,
        "Tunnel Neon",
        "3D tunnel with neon lighting",
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
    
    float r = length(p);
    float a = atan(p.y, p.x);
    float d = 1.0 / (r + 0.1);
    
    float tunnel = fract(d - t * 0.5);
    float rings = abs(sin(tunnel * 20.0));
    
    vec3 col = mix(u_palette_low, u_palette_high, rings);
    col = mix(col, u_palette_mid, smoothstep(0.3, 0.7, tunnel));
    
    fragColor = vec4(col, 1.0);
}
)"
    },
    
    {
        3,
        "Raymarcher SDF",
        "Raymarched signed distance fields",
        R"(
#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform float u_time;
uniform vec2 u_resolution;
uniform vec3 u_palette_low;
uniform vec3 u_palette_high;
uniform float u_speed;

float sdSphere(vec3 p, float r) { return length(p) - r; }

void main() {
    vec2 p = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0);
    float t = u_time * u_speed;
    
    vec3 ro = vec3(0, 0, -3);
    vec3 rd = normalize(vec3(p, 1.0));
    
    float dist = 0.0;
    for (int i = 0; i < 32; i++) {
        vec3 pos = ro + rd * dist;
        float d = sdSphere(pos - vec3(sin(t) * 0.5, cos(t * 0.7) * 0.5, 0), 0.5);
        if (d < 0.001) break;
        dist += d;
    }
    
    vec3 col = mix(u_palette_low, u_palette_high, smoothstep(2.0, 5.0, dist));
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
        "Sine wave distortion field",
        R"(
#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform float u_time;
uniform vec2 u_resolution;
uniform vec3 u_palette_low;
uniform vec3 u_palette_high;
uniform float u_speed;
uniform float u_warp;

void main() {
    vec2 p = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0);
    float t = u_time * u_speed;
    
    p.x += sin(p.y * 5.0 + t) * u_warp;
    p.y += cos(p.x * 5.0 + t * 0.7) * u_warp;
    
    float d = length(p);
    float wave = sin(d * 10.0 - t * 2.0) * 0.5 + 0.5;
    
    vec3 col = mix(u_palette_low, u_palette_high, wave);
    
    fragColor = vec4(col, 1.0);
}
)"
    },
    
    {
        7,
        "Particle System",
        "GPU particle simulation",
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

void main() {
    vec2 p = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0);
    float t = u_time * u_speed;
    
    vec3 col = vec3(0.0);
    
    for (int i = 0; i < 32; i++) {
        float fi = float(i);
        float h = hash(vec2(fi, fi * 0.5));
        float angle = h * 6.28;
        float radius = fract(h * 7.13 + t * 0.3) * 2.0;
        
        vec2 pos = vec2(cos(angle), sin(angle)) * radius;
        float d = length(p - pos);
        float particle = smoothstep(0.05, 0.0, d);
        
        vec3 pcol = mix(u_palette_low, mix(u_palette_mid, u_palette_high, h), fract(radius));
        col += pcol * particle;
    }
    
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
    }
};

const int g_shader_preset_count = sizeof(g_shader_presets) / sizeof(g_shader_presets[0]);

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
