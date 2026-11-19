#include "render/Scene.hpp"

#include "Mesh.hpp"

namespace varyn {

Scene::Scene(const VulkanContext& context, const Config& config) {
    for (const ModelConfig& model : config.models) {
        MeshData data = model.path ? loadObj(*model.path) : makeBuiltinMesh(model.builtin);

        GpuMesh& mesh = meshes_.emplace_back();
        mesh.vertices = createDeviceLocalBuffer(context, data.vertices.data(), data.vertices.size() * sizeof(Vertex),
                                                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        mesh.indices = createDeviceLocalBuffer(context, data.indices.data(), data.indices.size() * sizeof(uint32_t),
                                               VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        mesh.indexCount = uint32_t(data.indices.size());
    }

    for (const TextureConfig& texture : config.textures) {
        textures_.push_back(loadTexture(context, texture.path.c_str(), texture.linearFilter, texture.repeat));
    }
}

}  // namespace varyn
