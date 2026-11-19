#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace varyn {

// Matches the vertex inputs declared in the generated varyn.glsl header:
//   location 0: inPosition, location 1: inNormal, location 2: inUV
struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

// CPU side mesh data, uploaded to the GPU by the Scene.
struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

class MeshError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Loads a Wavefront .obj file. Polygons are triangulated as fans; missing
// normals are computed by averaging the face normals.
MeshData loadObj(const std::filesystem::path& file);

// Built-in meshes: "quad" (2x2 in the XY plane, perfect for fullscreen
// effects), "cube" (2x2x2) and "sphere" (radius 1).
MeshData makeBuiltinMesh(const std::string& name);

}  // namespace varyn
