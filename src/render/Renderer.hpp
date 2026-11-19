#pragma once

#include "Config.hpp"
#include "ShaderCompiler.hpp"
#include "UniformLayout.hpp"
#include "render/Scene.hpp"
#include "vulkan/Swapchain.hpp"

#include <array>
#include <map>
#include <memory>

namespace varyn {

// Draws a Config every frame: evaluates its expressions, fills the uniform
// buffer and records one draw per model instance.
//
// Shaders are compiled at runtime. When a shader fails to compile, the
// previous working pipeline stays active and the error is printed.
class Renderer {
public:
    Renderer(const VulkanContext& context, const Swapchain& swapchain);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Takes over a new config and its loaded scene. Rebuilds descriptors and
    // recompiles every shader.
    void setScene(Config config, std::unique_ptr<Scene> scene);

    // Recompiles all shaders (e.g. after a shader file changed on disk).
    void reloadShaders();

    // Every shader source and module the current pipelines were built from.
    const std::vector<fs::path>& shaderDependencies() const { return shaderDependencies_; }

    // Returns false when the swapchain is out of date and must be recreated.
    bool drawFrame(const ExpressionContext& clock);

private:
    static constexpr uint32_t kFramesInFlight = 2;

    struct FrameResources {
        VkCommandBuffer commands = VK_NULL_HANDLE;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkFence inFlight = VK_NULL_HANDLE;
        Buffer uniforms;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    };

    void createFrameSyncObjects();
    void createDescriptors();
    void createPipelineLayout();
    void destroySceneObjects();

    VkPipeline createPipeline(const std::vector<uint32_t>& vertexSpirv, const std::vector<uint32_t>& fragmentSpirv);
    void writeUniforms(FrameResources& frame, const ExpressionContext& clock);
    void recordCommands(VkCommandBuffer commands, uint32_t imageIndex, const ExpressionContext& clock);

    const VulkanContext& context_;
    const Swapchain& swapchain_;

    Config config_;
    std::unique_ptr<Scene> scene_;
    std::unique_ptr<UniformLayout> uniformLayout_;
    ShaderCompiler compiler_;
    std::vector<fs::path> shaderDependencies_;

    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    std::map<ShaderPair, VkPipeline> pipelines_;

    std::array<FrameResources, kFramesInFlight> frames_;
    uint32_t currentFrame_ = 0;
};

}  // namespace varyn
