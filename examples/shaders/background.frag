#version 450
// Animated noise backdrop, written in the classic shader-playground style
// using gl_FragCoord, resolution and time.

#include <varyn.glsl>
#include "noise.glsl"
#include "color.glsl"

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

void main() {
    vec2 p = (gl_FragCoord.xy - 0.5 * resolution) / resolution.y;

    float n = fbm(p * 3.0 + vec2(time * 0.08, -time * 0.05));
    float vignette = 1.0 - smoothstep(0.3, 1.2, length(p));

    vec3 color = palette(n * 0.6 + time * 0.03) * (0.08 + 0.18 * n) * vignette;
    outColor = vec4(color, 1.0);
}
