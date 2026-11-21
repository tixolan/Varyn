#pragma once

#include "Config.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace varyn {

enum class GlslType { Int, Float, Vec2, Vec3, Vec4, Mat4 };

struct UniformField {
    std::string name;
    GlslType type;
    uint32_t offset;     // byte offset inside the uniform buffer (std140 rules)
    uint32_t count = 0;  // array length, 0 for a single value
};

// Values Varyn computes itself every frame.
struct FrameUniforms {
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::vec3 cameraPosition{0.0f};
    glm::vec2 resolution{0.0f};
    float time = 0.0f;
    float deltaTime = 0.0f;
    int frame = 0;
};

// Describes the per-frame uniform buffer: the built-in fields (time,
// deltaTime, frame, camera matrices, ...) followed by the user's [uniforms].
// Offsets follow the std140 layout rules so the GLSL block declared by
// generateShaderHeader() matches the bytes written by write().
class UniformLayout {
public:
    explicit UniformLayout(const std::vector<UniformConfig>& userUniforms);

    uint32_t size() const { return size_; }
    const std::vector<UniformField>& fields() const { return fields_; }

    // Fills `destination` (at least size() bytes) for the current frame.
    void write(std::byte* destination, const FrameUniforms& frame, const std::vector<UniformConfig>& userUniforms,
               const ExpressionContext& context) const;

private:
    void add(std::string name, GlslType type, uint32_t count = 0);

    std::vector<UniformField> fields_;
    size_t firstUserField_ = 0;
    uint32_t size_ = 0;
};

// Source of the virtual `#include <varyn.glsl>` header. It declares the
// uniform block, the per-draw push constants, one sampler per texture, the
// environment maps used for reflections and (in the vertex stage) the vertex
// inputs.
std::string generateShaderHeader(const UniformLayout& layout, const std::vector<TextureConfig>& textures);

// Bindings in descriptor set 0: the uniform buffer, the environment maps
// (a cube map array, one cube per metallic model) and then the textures.
constexpr uint32_t kUniformBufferBinding = 0;
constexpr uint32_t kEnvironmentBinding = 1;
constexpr uint32_t kFirstTextureBinding = 2;

// Per-draw data sent through push constants.
struct ObjectPushConstants {
    glm::mat4 modelMatrix;
    int32_t modelIndex;
    int32_t instanceIndex;
    float metallic;
    int32_t environmentLayer;  // cube index in the environment array, -1 if none
};

}  // namespace varyn
