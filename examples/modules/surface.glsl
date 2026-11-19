// surface.glsl - the values passed from vertex to fragment shader.
//
// Including the same module in both stages guarantees the interfaces match:
// it declares `out` variables in the vertex shader and `in` variables in
// the fragment shader.
#ifndef SURFACE_GLSL
#define SURFACE_GLSL

#ifdef VARYN_VERTEX_SHADER
#define VARYING out
#else
#define VARYING in
#endif

layout(location = 0) VARYING vec3 vWorldPosition;
layout(location = 1) VARYING vec3 vNormal;
layout(location = 2) VARYING vec2 vUV;

#endif
