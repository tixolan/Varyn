#version 450
// An endless yellow and green checkerboard fading into the horizon. The
// shadows are drawn on top of it by shadow.vert / shadow.frag.

#include <varyn.glsl>
#include "surface.glsl"
#include "juggler.glsl"

layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(floorColor(vWorldPosition, 1.0), 1.0);
}
