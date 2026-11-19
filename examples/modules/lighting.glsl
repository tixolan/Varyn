// lighting.glsl - a small Blinn-Phong lighting model with a rim light.
#ifndef LIGHTING_GLSL
#define LIGHTING_GLSL

vec3 shade(vec3 albedo, vec3 position, vec3 normal, vec3 lightDir) {
    vec3 N = normalize(normal);
    vec3 L = normalize(lightDir);
    vec3 V = normalize(cameraPosition - position);
    vec3 H = normalize(L + V);

    float diffuse = max(dot(N, L), 0.0);
    float specular = pow(max(dot(N, H), 0.0), 48.0);
    float rim = pow(1.0 - max(dot(N, V), 0.0), 3.0);

    vec3 ambient = albedo * 0.12;
    return ambient + albedo * diffuse + vec3(specular * 0.6) + rim * 0.25 * albedo;
}

#endif
