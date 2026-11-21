#version 450
// Shadow pass: the floor as it looks without direct light.

#include <varyn.glsl>
#include "surface.glsl"
#include "juggler.glsl"

layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(floorColor(vWorldPosition, 0.0), 1.0);
}
