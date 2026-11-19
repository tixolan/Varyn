#pragma once

#include "Config.hpp"
#include "vulkan/Resources.hpp"

#include <vector>

namespace varyn {

struct GpuMesh {
    Buffer vertices;
    Buffer indices;
    uint32_t indexCount = 0;
};

// The GPU resources described by a config: one mesh per [[models]] entry and
// one texture per [[textures]] entry, in the same order as in the file.
//
// Loading happens entirely in the constructor, so a broken model or texture
// throws before the previous scene is replaced.
class Scene {
public:
    Scene(const VulkanContext& context, const Config& config);

    const std::vector<GpuMesh>& meshes() const { return meshes_; }
    const std::vector<Texture>& textures() const { return textures_; }

private:
    std::vector<GpuMesh> meshes_;
    std::vector<Texture> textures_;
};

}  // namespace varyn
