#version 450
// Shadow pass: squashes the model flat onto the floor along the light
// direction, like the shadow a lamp would cast, just above the floor.

#include <varyn.glsl>
#include "surface.glsl"

void main() {
    vec3 world = (modelMatrix * vec4(inPosition, 1.0)).xyz;
    vec3 toLight = normalize(lightDirection);
    world -= toLight * (world.y / toLight.y);
    world.y = 0.003;  // just above the floor so it wins the depth test

    vWorldPosition = world;
    vNormal = vec3(0.0, 1.0, 0.0);
    vUV = inUV;
    gl_Position = viewProjection * vec4(world, 1.0);
}
