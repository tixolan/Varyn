#version 450
// Default fragment shader: every instance gets its own colour from the
// palette, lit by the light direction defined in the config.

#include <varyn.glsl>
#include "surface.glsl"
#include "lighting.glsl"
#include "color.glsl"

layout(location = 0) out vec4 outColor;

void main() {
    vec3 albedo = palette(float(instanceIndex) * 0.08 + time * 0.1) * tint;
    albedo = mix(albedo, vec3(1.0), pulse * 0.25);

    vec3 color = shade(albedo, vWorldPosition, vNormal, lightDirection);
    outColor = vec4(toneMap(color * 1.6), 1.0);
}
