#version 450
// Draws the built-in quad straight onto the screen, ignoring the camera.
// z = 1 puts it on the far plane, behind everything else.

#include <varyn.glsl>

layout(location = 0) out vec2 vUV;

void main() {
    vUV = inUV;
    gl_Position = vec4(inPosition.xy, 1.0, 1.0);
}
