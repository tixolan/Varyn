#version 450
// A mirror ball (material 2 in the original, colour <.9,.9,.9>): 90% of what
// it reflects, plus the lamp's highlight.

#include <varyn.glsl>
#include "surface.glsl"
#include "juggler.glsl"

layout(location = 0) out vec4 outColor;

void main() {
    vec3 N = normalize(vNormal);
    vec3 V = normalize(cameraPosition - vWorldPosition);
    vec3 H = normalize(normalize(lightDirection) + V);

    vec3 color = 0.9 * sampleEnvironment(reflect(-V, N)) + vec3(pow(max(dot(N, H), 0.0), 400.0));
    outColor = vec4(min(color, vec3(1.0)), 1.0);
}
