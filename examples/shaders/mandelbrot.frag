#version 450
// A Mandelbrot set painted on the floor. It slowly zooms in and out of
// mandelbrotCenter (see [uniforms] in mandelbrot.toml) and fades out with
// distance like the checkerboard floor does.

#include <varyn.glsl>
#include "surface.glsl"
#include "lighting.glsl"
#include "color.glsl"

layout(location = 0) out vec4 outColor;

const int MAX_ITERATIONS = 256;

// Smooth (continuous) iteration count, or -1 inside the set.
float mandelbrot(vec2 c) {
    vec2 z = vec2(0.0);
    for (int i = 0; i < MAX_ITERATIONS; ++i) {
        z = vec2(z.x * z.x - z.y * z.y, 2.0 * z.x * z.y) + c;
        float r2 = dot(z, z);
        if (r2 > 256.0) {
            return float(i) - log2(log2(r2)) + 4.0;
        }
    }
    return -1.0;
}

void main() {
    // UVs go 0..1 across the quad; map them to a square centred on 0.
    vec2 p = (vUV - 0.5) * 3.0;
    vec2 c = mandelbrotCenter + p * mandelbrotZoom;

    float n = mandelbrot(c);
    vec3 albedo = n < 0.0
        ? vec3(0.0)
        : palette(n * 0.02 + time * 0.05) * 0.8;

    vec3 color = shade(albedo, vWorldPosition, vNormal, lightDirection);
    // A little self-illumination so the fractal stays readable in shadow.
    color += albedo * 0.35;

    float fade = smoothstep(6.0, 2.0, length(vWorldPosition.xz));
    outColor = vec4(toneMap(color * 1.6), fade);
}
