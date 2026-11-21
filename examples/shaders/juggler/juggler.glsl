// juggler.glsl - the world of the juggler example, using the colours of the
// original scene file (robot.dat): sky, checkerboard floor, ambient light and
// the ball shadows. Include <varyn.glsl> before this file.
//
// Like the original raytracer there is no tone mapping: colour = albedo *
// (ambient + direct light) + highlight, clamped by the display.
#ifndef JUGGLER_GLSL
#define JUGGLER_GLSL

const vec3 kHorizon = vec3(0.7, 0.7, 1.0);
const vec3 kZenith = vec3(0.1, 0.1, 1.0);
const vec3 kGround0 = vec3(1.5, 1.5, 0.0);
const vec3 kGround1 = vec3(0.0, 1.5, 0.0);
const float kAmbient = 0.25;

// Colour of the sky in a world direction.
vec3 skyColor(vec3 direction) {
    return mix(kHorizon, kZenith, clamp(direction.y, 0.0, 1.0));
}

// 0 or 1 per square, filtered so distant squares blur instead of shimmering.
float checker(vec2 p) {
    vec2 w = clamp(fwidth(p), vec2(1e-4), vec2(1.0));
    vec2 i = 2.0 * (abs(fract((p - 0.5 * w) * 0.5) - 0.5) - abs(fract((p + 0.5 * w) * 0.5) - 0.5)) / w;
    return 0.5 - 0.5 * i.x * i.y;
}

// Colour of the floor at a point on it, lit (1) or in shadow (0). The floor
// and the shadow pass both use this, so overlapping shadows don't stack.
vec3 floorColor(vec3 position, float lit) {
    vec3 albedo = mix(kGround0, kGround1, checker(position.xz));
    float diffuse = max(normalize(lightDirection).y, 0.0) * lit;
    vec3 color = albedo * (kAmbient + diffuse);
    float fog = smoothstep(15.0, 70.0, length(position - cameraPosition));
    return mix(min(color, vec3(1.0)), kHorizon, fog);
}

// Radius of the juggling balls (their `scale` in juggler.toml).
const float kBallRadius = 0.2;

// 0 where one of the balls blocks the light from `position`, 1 elsewhere,
// with a slightly soft edge. The ball centres come from the `balls` uniform array.
float ballShadow(vec3 position) {
    vec3 toLight = normalize(lightDirection);
    float light = 1.0;
    for (int k = 0; k < 3; ++k) {
        vec3 toCenter = balls[k] - position;
        float along = dot(toCenter, toLight);
        if (along <= 0.0) continue;  // the ball is behind the surface
        float distanceToRay = sqrt(max(dot(toCenter, toCenter) - along * along, 0.0));
        light *= smoothstep(kBallRadius * 0.9, kBallRadius * 1.05, distanceToRay);
    }
    return light;
}

#endif
