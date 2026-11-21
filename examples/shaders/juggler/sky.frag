#version 450
// The inside of a huge sphere: a gradient from pale blue at the horizon to
// deep blue overhead. Being a real model, it also shows up in reflections.

#include <varyn.glsl>
#include "surface.glsl"
#include "juggler.glsl"

layout(location = 0) out vec4 outColor;

void main() {
    vec3 direction = normalize(vWorldPosition - cameraPosition);
    outColor = vec4(skyColor(direction), 1.0);
}
