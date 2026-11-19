#version 450
// The floor samples the checker texture and fades out with distance.

#include <varyn.glsl>
#include "surface.glsl"
#include "lighting.glsl"
#include "color.glsl"

layout(location = 0) out vec4 outColor;

void main() {
    vec3 albedo = texture(checker, vUV * 4.0).rgb * 0.6;
    vec3 color = shade(albedo, vWorldPosition, vNormal, lightDirection);

    float fade = smoothstep(6.0, 2.0, length(vWorldPosition.xz));
    outColor = vec4(toneMap(color * 1.6), fade);
}
