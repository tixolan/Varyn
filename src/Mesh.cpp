#include "Mesh.hpp"

#include <array>
#include <cmath>
#include <fstream>
#include <map>
#include <numbers>
#include <sstream>
#include <tuple>

namespace varyn {

// ---------------------------------------------------------------------------
// OBJ loading
// ---------------------------------------------------------------------------

namespace {

// One corner of an OBJ face: indices into the position / uv / normal lists.
// -1 means "not specified".
struct FaceCorner {
    int position = -1;
    int uv = -1;
    int normal = -1;

    auto operator<=>(const FaceCorner&) const = default;
};

// OBJ indices are 1-based, negative values count from the end of the list.
int resolveIndex(const std::string& text, size_t listSize) {
    if (text.empty()) return -1;
    int index = std::stoi(text);
    return index < 0 ? static_cast<int>(listSize) + index : index - 1;
}

// Parses "v", "v/vt", "v//vn" or "v/vt/vn".
FaceCorner parseCorner(const std::string& token, size_t positions, size_t uvs, size_t normals) {
    std::array<std::string, 3> parts;
    size_t part = 0;
    for (char c : token) {
        if (c == '/') {
            if (++part >= parts.size()) break;
        } else {
            parts[part] += c;
        }
    }
    return {resolveIndex(parts[0], positions), resolveIndex(parts[1], uvs), resolveIndex(parts[2], normals)};
}

void computeSmoothNormals(MeshData& mesh) {
    for (Vertex& vertex : mesh.vertices) vertex.normal = glm::vec3(0.0f);

    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        Vertex& a = mesh.vertices[mesh.indices[i]];
        Vertex& b = mesh.vertices[mesh.indices[i + 1]];
        Vertex& c = mesh.vertices[mesh.indices[i + 2]];
        // Not normalized on purpose: larger faces contribute more.
        glm::vec3 faceNormal = glm::cross(b.position - a.position, c.position - a.position);
        a.normal += faceNormal;
        b.normal += faceNormal;
        c.normal += faceNormal;
    }

    for (Vertex& vertex : mesh.vertices) {
        float length = glm::length(vertex.normal);
        vertex.normal = length > 0.0f ? vertex.normal / length : glm::vec3(0.0f, 1.0f, 0.0f);
    }
}

}  // namespace

MeshData loadObj(const std::filesystem::path& file) {
    std::ifstream input(file);
    if (!input) throw MeshError("cannot open model '" + file.string() + "'");

    std::vector<glm::vec3> positions;
    std::vector<glm::vec2> uvs;
    std::vector<glm::vec3> normals;

    MeshData mesh;
    std::map<FaceCorner, uint32_t> vertexIndexOfCorner;  // shares identical corners
    bool missingNormals = false;

    std::string line;
    int lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        std::istringstream stream(line);
        std::string keyword;
        stream >> keyword;

        if (keyword == "v") {
            glm::vec3& p = positions.emplace_back();
            stream >> p.x >> p.y >> p.z;
        } else if (keyword == "vt") {
            glm::vec2& uv = uvs.emplace_back();
            stream >> uv.x >> uv.y;
        } else if (keyword == "vn") {
            glm::vec3& n = normals.emplace_back();
            stream >> n.x >> n.y >> n.z;
        } else if (keyword == "f") {
            std::vector<uint32_t> polygon;
            std::string token;
            while (stream >> token) {
                FaceCorner corner = parseCorner(token, positions.size(), uvs.size(), normals.size());
                if (corner.position < 0 || corner.position >= static_cast<int>(positions.size())) {
                    throw MeshError(file.string() + ":" + std::to_string(lineNumber) + ": invalid vertex index");
                }

                auto [it, inserted] = vertexIndexOfCorner.try_emplace(corner, uint32_t(mesh.vertices.size()));
                if (inserted) {
                    Vertex vertex{};
                    vertex.position = positions[corner.position];
                    if (corner.uv >= 0 && corner.uv < int(uvs.size())) vertex.uv = uvs[corner.uv];
                    if (corner.normal >= 0 && corner.normal < int(normals.size())) {
                        vertex.normal = normals[corner.normal];
                    } else {
                        missingNormals = true;
                    }
                    mesh.vertices.push_back(vertex);
                }
                polygon.push_back(it->second);
            }

            // Triangle fan: (0, 1, 2), (0, 2, 3), ...
            for (size_t i = 1; i + 1 < polygon.size(); ++i) {
                mesh.indices.insert(mesh.indices.end(), {polygon[0], polygon[i], polygon[i + 1]});
            }
        }
        // Everything else (o, g, s, usemtl, mtllib, comments) is ignored.
    }

    if (mesh.indices.empty()) throw MeshError("model '" + file.string() + "' contains no faces");
    if (missingNormals) computeSmoothNormals(mesh);
    return mesh;
}

// ---------------------------------------------------------------------------
// Built-in meshes
// ---------------------------------------------------------------------------

namespace {

MeshData makeQuad() {
    MeshData mesh;
    mesh.vertices = {
        {{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
        {{ 1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        {{ 1.0f,  1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{-1.0f,  1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
    };
    mesh.indices = {0, 1, 2, 0, 2, 3};
    return mesh;
}

MeshData makeCube() {
    // Each face: normal plus the two axes spanning it.
    struct Face { glm::vec3 normal, right, up; };
    const Face faces[] = {
        {{ 1, 0, 0}, { 0, 0,-1}, {0, 1, 0}},
        {{-1, 0, 0}, { 0, 0, 1}, {0, 1, 0}},
        {{ 0, 1, 0}, { 1, 0, 0}, {0, 0,-1}},
        {{ 0,-1, 0}, { 1, 0, 0}, {0, 0, 1}},
        {{ 0, 0, 1}, { 1, 0, 0}, {0, 1, 0}},
        {{ 0, 0,-1}, {-1, 0, 0}, {0, 1, 0}},
    };

    MeshData mesh;
    for (const Face& face : faces) {
        uint32_t first = uint32_t(mesh.vertices.size());
        const glm::vec2 corners[] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        for (glm::vec2 c : corners) {
            glm::vec3 position = face.normal + face.right * c.x + face.up * c.y;
            glm::vec2 uv = {(c.x + 1.0f) * 0.5f, 1.0f - (c.y + 1.0f) * 0.5f};
            mesh.vertices.push_back({position, face.normal, uv});
        }
        mesh.indices.insert(mesh.indices.end(), {first, first + 1, first + 2, first, first + 2, first + 3});
    }
    return mesh;
}

MeshData makeSphere(int rings = 32, int segments = 64) {
    MeshData mesh;
    for (int ring = 0; ring <= rings; ++ring) {
        float v = float(ring) / float(rings);
        float phi = v * std::numbers::pi_v<float>;  // 0 at the north pole
        for (int segment = 0; segment <= segments; ++segment) {
            float u = float(segment) / float(segments);
            float theta = u * 2.0f * std::numbers::pi_v<float>;
            glm::vec3 normal = {std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta)};
            mesh.vertices.push_back({normal, normal, {u, v}});
        }
    }

    const uint32_t stride = uint32_t(segments + 1);
    for (uint32_t ring = 0; ring < uint32_t(rings); ++ring) {
        for (uint32_t segment = 0; segment < uint32_t(segments); ++segment) {
            uint32_t a = ring * stride + segment;
            uint32_t b = a + stride;
            mesh.indices.insert(mesh.indices.end(), {a, b, a + 1, a + 1, b, b + 1});
        }
    }
    return mesh;
}

}  // namespace

MeshData makeBuiltinMesh(const std::string& name) {
    if (name == "quad") return makeQuad();
    if (name == "cube") return makeCube();
    if (name == "sphere") return makeSphere();
    throw MeshError("unknown builtin mesh '" + name + "' (expected quad, cube or sphere)");
}

}  // namespace varyn
