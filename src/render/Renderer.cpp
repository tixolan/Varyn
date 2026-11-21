#include "render/Renderer.hpp"

#include "Mesh.hpp"
#include "render/Evaluate.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <set>

namespace varyn {

Renderer::Renderer(const VulkanContext& context, const Swapchain& swapchain)
    : context_(context), swapchain_(swapchain) {
    createFrameSyncObjects();
}

Renderer::~Renderer() {
    context_.waitIdle();
    destroySceneObjects();

    VkDevice device = context_.device();
    for (FrameResources& frame : frames_) {
        vkDestroySemaphore(device, frame.imageAvailable, nullptr);
        vkDestroyFence(device, frame.inFlight, nullptr);
        vkFreeCommandBuffers(device, context_.commandPool(), 1, &frame.commands);
    }
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

void Renderer::createFrameSyncObjects() {
    VkDevice device = context_.device();
    for (FrameResources& frame : frames_) {
        VkCommandBufferAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocateInfo.commandPool = context_.commandPool();
        allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount = 1;
        VK_CHECK(vkAllocateCommandBuffers(device, &allocateInfo, &frame.commands));

        VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &frame.imageAvailable));

        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // the first wait must not block
        VK_CHECK(vkCreateFence(device, &fenceInfo, nullptr, &frame.inFlight));
    }
}

void Renderer::setScene(Config config, std::unique_ptr<Scene> scene) {
    context_.waitIdle();
    destroySceneObjects();

    config_ = std::move(config);
    scene_ = std::move(scene);
    uniformLayout_ = std::make_unique<UniformLayout>(config_.uniforms);

    probes_.clear();
    environmentLayers_.assign(config_.models.size(), -1);
    for (size_t i = 0; i < config_.models.size(); ++i) {
        if (!config_.models[i].metallic) continue;
        int32_t instances = 0;
        for (const InstanceConfig& instance : config_.models[i].instances) instances += instance.count;
        if (instances == 0) continue;
        environmentLayers_[i] = int32_t(probes_.size());
        for (int32_t instance = 0; instance < instances; ++instance) probes_.push_back({i, instance});
    }
    probePositions_.assign(probes_.size(), glm::vec3(0.0f));

    createEnvironmentMaps();
    createDescriptors();
    createPipelineLayout();

    compiler_.setGeneratedHeader(generateShaderHeader(*uniformLayout_, config_.textures));
    compiler_.setIncludeDirectories(config_.shaders.includeDirs);
    reloadShaders();
}

// Two cube map arrays (one per frame in flight) with 6 layers per probe.
// They use the swapchain's formats so the same pipelines can draw into them.
// Without probes they are a 1x1 placeholder so the descriptor stays valid.
void Renderer::createEnvironmentMaps() {
    VkDevice device = context_.device();
    const uint32_t layers = 6 * std::max<uint32_t>(uint32_t(probes_.size()), 1);
    const uint32_t size = probes_.empty() ? 1 : kEnvironmentSize;
    const VkExtent2D extent{size, size};

    for (FrameResources& frame : frames_) {
        frame.environment = Image(context_, extent, swapchain_.colorFormat(),
                                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                      VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                  VK_IMAGE_ASPECT_COLOR_BIT, layers, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
                                  VK_IMAGE_VIEW_TYPE_CUBE_ARRAY);

        for (uint32_t layer = 0; layer < layers; ++layer) {
            VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            viewInfo.image = frame.environment.handle();
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = swapchain_.colorFormat();
            viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, layer, 1};
            VK_CHECK(vkCreateImageView(device, &viewInfo, nullptr, &frame.environmentFaceViews.emplace_back()));
        }
    }
    environmentDepth_ = Image(context_, extent, swapchain_.depthFormat(), VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                              VK_IMAGE_ASPECT_DEPTH_BIT);

    // Start out black and readable: the first frame samples the "previous" one.
    context_.submitAndWait([&](VkCommandBuffer commands) {
        for (FrameResources& frame : frames_) {
            VkImage image = frame.environment.handle();
            transitionImage(commands, image, VK_IMAGE_ASPECT_COLOR_BIT,
                            {VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                             VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT});
            VkClearColorValue black{};
            VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layers};
            vkCmdClearColorImage(commands, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &range);
            transitionImage(commands, image, VK_IMAGE_ASPECT_COLOR_BIT,
                            {VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                             VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                             VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT});
        }
    });

    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VK_CHECK(vkCreateSampler(device, &samplerInfo, nullptr, &environmentSampler_));
}

// Binding 0 is the uniform buffer (dynamic offset = which camera), binding 1
// the environment maps, bindings 2..N the textures. Each frame has two sets
// that differ only in which environment array they read.
void Renderer::createDescriptors() {
    VkDevice device = context_.device();
    const uint32_t textureCount = uint32_t(config_.textures.size());
    const VkShaderStageFlags stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    const uint32_t setCount = 2 * kFramesInFlight;

    std::vector<VkDescriptorSetLayoutBinding> bindings;
    bindings.push_back({kUniformBufferBinding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, stages, nullptr});
    bindings.push_back({kEnvironmentBinding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, stages, nullptr});
    for (uint32_t i = 0; i < textureCount; ++i) {
        bindings.push_back({kFirstTextureBinding + i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, stages, nullptr});
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = uint32_t(bindings.size());
    layoutInfo.pBindings = bindings.data();
    VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout_));

    std::vector<VkDescriptorPoolSize> poolSizes = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, setCount},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, setCount * (textureCount + 1)},
    };
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = setCount;
    poolInfo.poolSizeCount = uint32_t(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool_));

    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(context_.physicalDevice(), &properties);
    const uint32_t alignment = uint32_t(properties.limits.minUniformBufferOffsetAlignment);
    uniformStride_ = (uniformLayout_->size() + alignment - 1) / alignment * alignment;
    const uint32_t uniformSlots = 1 + 6 * uint32_t(probes_.size());

    std::vector<VkDescriptorImageInfo> textureInfos;
    for (const Texture& texture : scene_->textures()) {
        textureInfos.push_back({texture.sampler, texture.image.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
    }

    for (uint32_t f = 0; f < kFramesInFlight; ++f) {
        FrameResources& frame = frames_[f];
        const FrameResources& previous = frames_[(f + kFramesInFlight - 1) % kFramesInFlight];
        frame.uniforms = Buffer(context_, VkDeviceSize(uniformStride_) * uniformSlots,
                                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        using SetTarget = std::pair<VkDescriptorSet*, const Image*>;
        for (auto [set, environment] : {SetTarget{&frame.descriptorSet, &frame.environment},
                                        SetTarget{&frame.environmentPassSet, &previous.environment}}) {
            VkDescriptorSetAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            allocateInfo.descriptorPool = descriptorPool_;
            allocateInfo.descriptorSetCount = 1;
            allocateInfo.pSetLayouts = &descriptorSetLayout_;
            VK_CHECK(vkAllocateDescriptorSets(device, &allocateInfo, set));

            VkDescriptorBufferInfo bufferInfo{frame.uniforms.handle(), 0, uniformLayout_->size()};
            VkDescriptorImageInfo environmentInfo{environmentSampler_, environment->view(),
                                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

            std::vector<VkWriteDescriptorSet> writes;
            VkWriteDescriptorSet uniformWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            uniformWrite.dstSet = *set;
            uniformWrite.dstBinding = kUniformBufferBinding;
            uniformWrite.descriptorCount = 1;
            uniformWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
            uniformWrite.pBufferInfo = &bufferInfo;
            writes.push_back(uniformWrite);

            VkWriteDescriptorSet environmentWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            environmentWrite.dstSet = *set;
            environmentWrite.dstBinding = kEnvironmentBinding;
            environmentWrite.descriptorCount = 1;
            environmentWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            environmentWrite.pImageInfo = &environmentInfo;
            writes.push_back(environmentWrite);

            for (uint32_t i = 0; i < textureCount; ++i) {
                VkWriteDescriptorSet textureWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                textureWrite.dstSet = *set;
                textureWrite.dstBinding = kFirstTextureBinding + i;
                textureWrite.descriptorCount = 1;
                textureWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                textureWrite.pImageInfo = &textureInfos[i];
                writes.push_back(textureWrite);
            }

            vkUpdateDescriptorSets(device, uint32_t(writes.size()), writes.data(), 0, nullptr);
        }
    }
}

void Renderer::createPipelineLayout() {
    VkPushConstantRange pushConstants{};
    pushConstants.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstants.size = sizeof(ObjectPushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout_;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstants;
    VK_CHECK(vkCreatePipelineLayout(context_.device(), &layoutInfo, nullptr, &pipelineLayout_));
}

void Renderer::destroySceneObjects() {
    VkDevice device = context_.device();
    for (auto& [shaders, pipeline] : pipelines_) vkDestroyPipeline(device, pipeline, nullptr);
    pipelines_.clear();

    if (pipelineLayout_) vkDestroyPipelineLayout(device, pipelineLayout_, nullptr);
    if (descriptorPool_) vkDestroyDescriptorPool(device, descriptorPool_, nullptr);  // frees the sets too
    if (descriptorSetLayout_) vkDestroyDescriptorSetLayout(device, descriptorSetLayout_, nullptr);
    if (environmentSampler_) vkDestroySampler(device, environmentSampler_, nullptr);
    pipelineLayout_ = VK_NULL_HANDLE;
    descriptorPool_ = VK_NULL_HANDLE;
    descriptorSetLayout_ = VK_NULL_HANDLE;
    environmentSampler_ = VK_NULL_HANDLE;

    for (FrameResources& frame : frames_) {
        for (VkImageView view : frame.environmentFaceViews) vkDestroyImageView(device, view, nullptr);
        frame.environmentFaceViews.clear();
        frame.environment = Image();
        frame.uniforms = Buffer();
        frame.descriptorSet = VK_NULL_HANDLE;
        frame.environmentPassSet = VK_NULL_HANDLE;
    }
    environmentDepth_ = Image();
    scene_.reset();
}

// ---------------------------------------------------------------------------
// Shaders and pipelines
// ---------------------------------------------------------------------------

void Renderer::reloadShaders() {
    std::set<ShaderPair> shaderPairs;
    for (const ModelConfig& model : config_.models) {
        shaderPairs.insert(model.shaders);
        shaderPairs.insert(model.extraPasses.begin(), model.extraPasses.end());
    }

    std::set<fs::path> dependencies;
    for (const ShaderPair& shaders : shaderPairs) {
        auto start = std::chrono::steady_clock::now();
        ShaderCompileResult vertex = compiler_.compile(shaders.vertex, ShaderStage::Vertex);
        ShaderCompileResult fragment = compiler_.compile(shaders.fragment, ShaderStage::Fragment);
        dependencies.insert(vertex.dependencies.begin(), vertex.dependencies.end());
        dependencies.insert(fragment.dependencies.begin(), fragment.dependencies.end());

        for (const ShaderCompileResult* result : {&vertex, &fragment}) {
            if (!result->log.empty()) std::cerr << result->log;
        }

        std::string label = shaders.vertex.filename().string() + " + " + shaders.fragment.filename().string();
        if (!vertex.success || !fragment.success) {
            bool hasOld = pipelines_.contains(shaders);
            std::cerr << "[shader] " << label << ": compilation failed"
                      << (hasOld ? ", keeping the previous version\n" : "\n");
            continue;
        }

        VkPipeline pipeline = createPipeline(vertex.spirv, fragment.spirv);
        auto existing = pipelines_.find(shaders);
        if (existing != pipelines_.end()) {
            context_.waitIdle();  // the old pipeline may still be in use by the GPU
            vkDestroyPipeline(context_.device(), existing->second, nullptr);
        }
        pipelines_[shaders] = pipeline;

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
        std::cout << "[shader] " << label << ": ok (" << elapsed.count() << " ms)\n";
    }

    shaderDependencies_.assign(dependencies.begin(), dependencies.end());
}

VkPipeline Renderer::createPipeline(const std::vector<uint32_t>& vertexSpirv,
                                    const std::vector<uint32_t>& fragmentSpirv) {
    VkDevice device = context_.device();

    auto createModule = [&](const std::vector<uint32_t>& spirv) {
        VkShaderModuleCreateInfo moduleInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        moduleInfo.codeSize = spirv.size() * sizeof(uint32_t);
        moduleInfo.pCode = spirv.data();
        VkShaderModule module;
        VK_CHECK(vkCreateShaderModule(device, &moduleInfo, nullptr, &module));
        return module;
    };
    VkShaderModule vertexModule = createModule(vertexSpirv);
    VkShaderModule fragmentModule = createModule(fragmentSpirv);

    VkPipelineShaderStageCreateInfo stages[2] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},
    };
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertexModule;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragmentModule;
    stages[1].pName = "main";

    // Vertex layout: matches struct Vertex and the inputs in varyn.glsl.
    VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributes[] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
        {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)},
    };
    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 3;
    vertexInput.pVertexAttributeDescriptions = attributes;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;  // the actual viewport and scissor are set while recording
    viewport.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterization{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.cullMode = VK_CULL_MODE_NONE;
    rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    // Standard alpha blending; opaque output (alpha = 1) is unaffected.
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.blendEnable = VK_TRUE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo colorBlend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    // Dynamic rendering: we only declare the attachment formats.
    VkFormat colorFormat = swapchain_.colorFormat();
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &colorFormat;
    rendering.depthAttachmentFormat = swapchain_.depthFormat();

    VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.pNext = &rendering;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewport;
    pipelineInfo.pRasterizationState = &rasterization;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout_;

    VkPipeline pipeline;
    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);

    vkDestroyShaderModule(device, vertexModule, nullptr);
    vkDestroyShaderModule(device, fragmentModule, nullptr);
    VK_CHECK(result);
    return pipeline;
}

// ---------------------------------------------------------------------------
// Per frame
// ---------------------------------------------------------------------------

bool Renderer::drawFrame(const ExpressionContext& clock) {
    VkDevice device = context_.device();
    FrameResources& frame = frames_[currentFrame_];

    VK_CHECK(vkWaitForFences(device, 1, &frame.inFlight, VK_TRUE, UINT64_MAX));

    uint32_t imageIndex = 0;
    VkResult acquired = vkAcquireNextImageKHR(device, swapchain_.handle(), UINT64_MAX, frame.imageAvailable,
                                              VK_NULL_HANDLE, &imageIndex);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) return false;
    if (acquired != VK_SUBOPTIMAL_KHR) VK_CHECK(acquired);

    VK_CHECK(vkResetFences(device, 1, &frame.inFlight));

    writeUniforms(frame, clock);

    VK_CHECK(vkResetCommandBuffer(frame.commands, 0));
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(frame.commands, &beginInfo));
    recordCommands(frame.commands, imageIndex, clock);
    VK_CHECK(vkEndCommandBuffer(frame.commands));

    // Wait for the image to be acquired before writing to it, then signal
    // presentation when rendering is done.
    VkSemaphoreSubmitInfo waitInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    waitInfo.semaphore = frame.imageAvailable;
    waitInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSemaphoreSubmitInfo signalInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signalInfo.semaphore = swapchain_.renderFinished(imageIndex);
    signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;

    VkCommandBufferSubmitInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    commandInfo.commandBuffer = frame.commands;

    VkSubmitInfo2 submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submitInfo.waitSemaphoreInfoCount = 1;
    submitInfo.pWaitSemaphoreInfos = &waitInfo;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &commandInfo;
    submitInfo.signalSemaphoreInfoCount = 1;
    submitInfo.pSignalSemaphoreInfos = &signalInfo;
    VK_CHECK(vkQueueSubmit2(context_.queue(), 1, &submitInfo, frame.inFlight));

    VkSemaphore renderFinished = swapchain_.renderFinished(imageIndex);
    VkSwapchainKHR swapchain = swapchain_.handle();
    VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinished;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &imageIndex;
    VkResult presented = vkQueuePresentKHR(context_.queue(), &presentInfo);

    currentFrame_ = (currentFrame_ + 1) % kFramesInFlight;

    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) return false;
    VK_CHECK(presented);
    return true;
}

namespace {

// Looking direction and up vector of each cube map face, in the Vulkan face
// order +X, -X, +Y, -Y, +Z, -Z. Rendered with a viewport that is not flipped,
// these match the orientation the hardware samples cube maps with.
struct CubeFace {
    glm::vec3 direction;
    glm::vec3 up;
};
constexpr CubeFace kCubeFaces[6] = {
    {{1, 0, 0}, {0, -1, 0}}, {{-1, 0, 0}, {0, -1, 0}}, {{0, 1, 0}, {0, 0, 1}},
    {{0, -1, 0}, {0, 0, -1}}, {{0, 0, 1}, {0, -1, 0}}, {{0, 0, -1}, {0, -1, 0}},
};

}  // namespace

void Renderer::writeUniforms(FrameResources& frame, const ExpressionContext& clock) {
    VkExtent2D extent = swapchain_.extent();
    float aspect = float(extent.width) / float(std::max(extent.height, 1u));
    CameraMatrices camera = evaluateCamera(config_.camera, clock, aspect);
    auto* destination = static_cast<std::byte*>(frame.uniforms.mapped());

    FrameUniforms values;
    values.view = camera.view;
    values.projection = camera.projection;
    values.cameraPosition = camera.position;
    values.resolution = {float(extent.width), float(extent.height)};
    values.time = float(clock.time);
    values.deltaTime = float(clock.deltaTime);
    values.frame = int(clock.frame);
    uniformLayout_->write(destination, values, config_.uniforms, clock);

    if (probes_.empty()) return;

    // Each probe sits at the position of its instance.
    size_t probe = 0;
    while (probe < probes_.size()) {
        const size_t model = probes_[probe].model;
        for (const InstanceConfig& instance : config_.models[model].instances) {
            ExpressionContext context = clock;
            context.count = instance.count;
            for (int i = 0; i < instance.count; ++i) {
                context.index = i;
                probePositions_[probe++] = glm::vec3(evaluateModelMatrix(instance, context)[3]);
            }
        }
    }

    const float near = float(config_.camera.near.evaluate(clock));
    const float far = float(config_.camera.far.evaluate(clock));
    values.projection = glm::perspective(glm::radians(90.0f), 1.0f, near, far);
    values.resolution = {float(kEnvironmentSize), float(kEnvironmentSize)};
    for (size_t probe = 0; probe < probes_.size(); ++probe) {
        values.cameraPosition = probePositions_[probe];
        for (uint32_t face = 0; face < 6; ++face) {
            const CubeFace& cube = kCubeFaces[face];
            values.view = glm::lookAt(values.cameraPosition, values.cameraPosition + cube.direction, cube.up);
            uint32_t slot = 1 + uint32_t(probe) * 6 + face;
            uniformLayout_->write(destination + size_t(slot) * uniformStride_, values, config_.uniforms, clock);
        }
    }
}

void Renderer::drawModels(VkCommandBuffer commands, VkDescriptorSet descriptorSet, uint32_t uniformSlot,
                          const Probe* skip, const ExpressionContext& clock) {
    uint32_t uniformOffset = uniformSlot * uniformStride_;
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &descriptorSet, 1,
                            &uniformOffset);

    std::vector<ObjectPushConstants> draws;
    for (size_t modelIndex = 0; modelIndex < config_.models.size(); ++modelIndex) {
        const ModelConfig& model = config_.models[modelIndex];

        // Each [[models.instances]] entry may expand into `count` copies.
        draws.clear();
        int32_t instanceCounter = 0;
        for (const InstanceConfig& instance : model.instances) {
            ExpressionContext context = clock;
            context.count = instance.count;
            for (int i = 0; i < instance.count; ++i) {
                context.index = i;

                const int32_t instanceIndex = instanceCounter++;
                if (skip && skip->model == modelIndex && skip->instance == instanceIndex) continue;

                ObjectPushConstants& push = draws.emplace_back();
                push.modelMatrix = evaluateModelMatrix(instance, context);
                push.modelIndex = int32_t(modelIndex);
                push.instanceIndex = instanceIndex;
                push.metallic = model.metallic ? std::clamp(float(model.metallic->evaluate(context)), 0.0f, 1.0f) : 0.0f;
                push.environmentLayer = environmentLayers_[modelIndex] < 0 ? -1 : environmentLayers_[modelIndex] + instanceIndex;
            }
        }

        const GpuMesh& mesh = scene_->meshes()[modelIndex];
        VkBuffer vertexBuffer = mesh.vertices.handle();
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(commands, 0, 1, &vertexBuffer, &offset);
        vkCmdBindIndexBuffer(commands, mesh.indices.handle(), 0, VK_INDEX_TYPE_UINT32);

        // The model's own shaders first, then its [[models.passes]].
        for (size_t pass = 0; pass <= model.extraPasses.size(); ++pass) {
            const ShaderPair& shaders = pass == 0 ? model.shaders : model.extraPasses[pass - 1];
            auto pipeline = pipelines_.find(shaders);
            if (pipeline == pipelines_.end()) continue;  // shaders never compiled successfully

            vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->second);
            for (const ObjectPushConstants& push : draws) {
                vkCmdPushConstants(commands, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, sizeof(push), &push);
                vkCmdDrawIndexed(commands, mesh.indexCount, 1, 0, 0, 0);
            }
        }
    }
}

// Renders the six faces of every probe into this frame's environment array.
// Reflective models drawn here sample the previous frame's array instead, so
// nothing reads the image while it is being written.
void Renderer::recordEnvironmentPass(VkCommandBuffer commands, const ExpressionContext& clock) {
    const FrameResources& frame = frames_[currentFrame_];
    const VkImage environment = frame.environment.handle();
    const VkExtent2D extent{kEnvironmentSize, kEnvironmentSize};

    // The previous use of this image was being sampled two frames ago.
    transitionImage(commands, environment, VK_IMAGE_ASPECT_COLOR_BIT,
                    {VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_NONE,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT});

    glm::vec4 clearColor = evaluate(config_.window.clearColor, clock);
    const VkPipelineStageFlags2 depthStages =
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;

    for (size_t probe = 0; probe < probes_.size(); ++probe) {
        for (uint32_t face = 0; face < 6; ++face) {
            uint32_t layer = uint32_t(probe) * 6 + face;

            // One depth image for every face: wait for the previous face.
            transitionImage(commands, environmentDepth_.handle(), VK_IMAGE_ASPECT_DEPTH_BIT,
                            {VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                             depthStages, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                             depthStages, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                              VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT});

            VkRenderingAttachmentInfo colorAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            colorAttachment.imageView = frame.environmentFaceViews[layer];
            colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttachment.clearValue.color = {{clearColor.r, clearColor.g, clearColor.b, clearColor.a}};

            VkRenderingAttachmentInfo depthAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            depthAttachment.imageView = environmentDepth_.view();
            depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depthAttachment.clearValue.depthStencil = {1.0f, 0};

            VkRenderingInfo renderingInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
            renderingInfo.renderArea = {{0, 0}, extent};
            renderingInfo.layerCount = 1;
            renderingInfo.colorAttachmentCount = 1;
            renderingInfo.pColorAttachments = &colorAttachment;
            renderingInfo.pDepthAttachment = &depthAttachment;
            vkCmdBeginRendering(commands, &renderingInfo);

            // Not flipped, unlike the main pass: see kCubeFaces.
            VkViewport viewport{0.0f, 0.0f, float(extent.width), float(extent.height), 0.0f, 1.0f};
            VkRect2D scissor{{0, 0}, extent};
            vkCmdSetViewport(commands, 0, 1, &viewport);
            vkCmdSetScissor(commands, 0, 1, &scissor);

            drawModels(commands, frame.environmentPassSet, 1 + layer, &probes_[probe], clock);
            vkCmdEndRendering(commands);
        }
    }

    transitionImage(commands, environment, VK_IMAGE_ASPECT_COLOR_BIT,
                    {VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                     VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT});
}

void Renderer::recordCommands(VkCommandBuffer commands, uint32_t imageIndex, const ExpressionContext& clock) {
    if (!probes_.empty()) recordEnvironmentPass(commands, clock);

    VkExtent2D extent = swapchain_.extent();
    VkImage colorImage = swapchain_.image(imageIndex);
    VkImage depthImage = swapchain_.depthImage().handle();

    transitionImage(commands, colorImage, VK_IMAGE_ASPECT_COLOR_BIT,
                    {VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_NONE,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT});

    // The depth image is shared by all frames in flight, so also wait for the
    // previous frame's depth writes.
    const VkPipelineStageFlags2 depthStages =
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    transitionImage(commands, depthImage, VK_IMAGE_ASPECT_DEPTH_BIT,
                    {VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                     depthStages, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                     depthStages, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                      VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT});

    glm::vec4 clearColor = evaluate(config_.window.clearColor, clock);

    VkRenderingAttachmentInfo colorAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    colorAttachment.imageView = swapchain_.imageView(imageIndex);
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = {{clearColor.r, clearColor.g, clearColor.b, clearColor.a}};

    VkRenderingAttachmentInfo depthAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depthAttachment.imageView = swapchain_.depthImage().view();
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.clearValue.depthStencil = {1.0f, 0};

    VkRenderingInfo renderingInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
    renderingInfo.renderArea = {{0, 0}, extent};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.pDepthAttachment = &depthAttachment;
    vkCmdBeginRendering(commands, &renderingInfo);

    // A negative viewport height flips Y so clip space points up, like in
    // OpenGL. That keeps glm's matrices and most shader tutorials working.
    VkViewport viewport{0.0f, float(extent.height), float(extent.width), -float(extent.height), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(commands, 0, 1, &viewport);
    vkCmdSetScissor(commands, 0, 1, &scissor);

    drawModels(commands, frames_[currentFrame_].descriptorSet, 0, nullptr, clock);

    vkCmdEndRendering(commands);

    transitionImage(commands, colorImage, VK_IMAGE_ASPECT_COLOR_BIT,
                    {VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                     VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE});
}

}  // namespace varyn
