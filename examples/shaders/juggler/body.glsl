// body.glsl - a body part, shaded like material 1 of the original raytracer:
// ambient plus diffuse light and a small bright glint where the lamp catches
// it. The balls cast shadows on it. Each material file defines ALBEDO and
// includes this.
#include <varyn.glsl>
#include "surface.glsl"
#include "juggler.glsl"

layout(location = 0) out vec4 outColor;

void main() {
    vec3 N = normalize(vNormal);
    vec3 V = normalize(cameraPosition - vWorldPosition);
    vec3 L = normalize(lightDirection);
    vec3 H = normalize(L + V);

    float direct = ballShadow(vWorldPosition) * step(0.0, dot(N, L));
    float diffuse = max(dot(N, L), 0.0);
    float glint = pow(max(dot(N, H), 0.0), 80.0);

    vec3 color = ALBEDO * (kAmbient + direct * diffuse) + vec3(direct * glint);
    outColor = vec4(min(color, vec3(1.0)), 1.0);
}
