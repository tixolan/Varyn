// color.glsl - colour helpers.
#ifndef COLOR_GLSL
#define COLOR_GLSL

// A smooth cosine based colour palette: t in [0, 1] loops through hues.
vec3 palette(float t) {
    vec3 a = vec3(0.5);
    vec3 b = vec3(0.5);
    vec3 c = vec3(1.0);
    vec3 d = vec3(0.00, 0.33, 0.67);
    return a + b * cos(6.28318 * (c * t + d));
}

vec3 toneMap(vec3 color) {
    return color / (color + vec3(1.0));
}

#endif
