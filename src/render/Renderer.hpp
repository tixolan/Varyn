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
// Every instance of a model with a `metallic` option gets an environment
// probe: each frame the scene (minus that instance) is rendered into the six
// faces of a cube map placed at the instance's position. Shaders read it
// through sampleEnvironment(). The probes of one frame see the probes of the
// previous frame, so metallic instances also reflect each other.
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
    static constexpr uint32_t kEnvironmentSize = 256;  // pixels per cube face

    struct FrameResources {
        VkCommandBuffer commands = VK_NULL_HANDLE;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkFence inFlight = VK_NULL_HANDLE;

        // One uniform block per camera: the main camera first, then the six
        // faces of every probe. Selected with a dynamic offset.
        Buffer uniforms;

        // Cube map array written by this frame, 6 layers per probe, and one
        // 2D view per layer to render into.
        Image environment;
        std::vector<VkImageView> environmentFaceViews;

        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;             // samples this frame's environment
        VkDescriptorSet environmentPassSet = VK_NULL_HANDLE;        // samples the previous frame's
    };

    void createFrameSyncObjects();
    void createEnvironmentMaps();
    void createDescriptors();
    void createPipelineLayout();
    void destroySceneObjects();

    VkPipeline createPipeline(const std::vector<uint32_t>& vertexSpirv, const std::vector<uint32_t>& fragmentSpirv);
    void writeUniforms(FrameResources& frame, const ExpressionContext& clock);
    void recordCommands(VkCommandBuffer commands, uint32_t imageIndex, const ExpressionContext& clock);
    void recordEnvironmentPass(VkCommandBuffer commands, const ExpressionContext& clock);

    struct Probe {
        size_t model;
        int32_t instance;  // index among the model's expanded instances
    };

    // Draws everything except `skip` (nullptr to draw it all) using the
    // uniform block at `uniformSlot`.
    void drawModels(VkCommandBuffer commands, VkDescriptorSet descriptorSet, uint32_t uniformSlot, const Probe* skip,
                    const ExpressionContext& clock);

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

    // One probe per instance of every metallic model; a probe's index is its
    // cube index in the environment array. Per model: the probe of its first
    // instance, or -1.
    std::vector<Probe> probes_;
    std::vector<int32_t> environmentLayers_;
    std::vector<glm::vec3> probePositions_;  // evaluated every frame
    Image environmentDepth_;
    VkSampler environmentSampler_ = VK_NULL_HANDLE;
    uint32_t uniformStride_ = 0;  // bytes between two uniform blocks

    std::array<FrameResources, kFramesInFlight> frames_;
    uint32_t currentFrame_ = 0;
};

}  // namespace varyn
