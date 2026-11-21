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

#ifdef VARYN_FRAGMENT_SHADER
// shade() blended with a mirror reflection of the scene. `metallic` (0..1)
// usually is the model's `metallic` config option, declared in varyn.glsl.
vec3 shadeMetallic(vec3 albedo, vec3 position, vec3 normal, vec3 lightDir, float metallic) {
    vec3 color = shade(albedo, position, normal, lightDir);
    if (metallic <= 0.0) return color;

    vec3 N = normalize(normal);
    vec3 V = normalize(cameraPosition - position);
    vec3 H = normalize(normalize(lightDir) + V);

    // The environment map holds finished screen colours. Undo the
    // toneMap(color * 1.6) the example shaders end with, so the reflection
    // goes through the same tone mapping as the rest of the lighting.
    vec3 reflection = min(sampleEnvironment(reflect(-V, N)), vec3(0.98));
    reflection = reflection / (1.0 - reflection) / 1.6;

    // Metals tint what they reflect with their own colour, except at grazing
    // angles where every surface turns into a white mirror (Fresnel).
    float fresnel = pow(1.0 - max(dot(N, V), 0.0), 5.0);
    vec3 tint = mix(albedo, vec3(1.0), fresnel);
    float specular = pow(max(dot(N, H), 0.0), 96.0);

    vec3 metal = reflection * tint + vec3(specular);
    return mix(color, metal, metallic);
}
#endif

#endif
