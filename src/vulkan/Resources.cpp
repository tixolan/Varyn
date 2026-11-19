#include "vulkan/Resources.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <cstring>
#include <memory>
#include <utility>

namespace varyn {

namespace {

VkDeviceMemory allocateMemory(const VulkanContext& context, VkMemoryRequirements requirements,
                              VkMemoryPropertyFlags properties) {
    VkMemoryAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = context.findMemoryType(requirements.memoryTypeBits, properties);

    VkDeviceMemory memory;
    VK_CHECK(vkAllocateMemory(context.device(), &allocateInfo, nullptr, &memory));
    return memory;
}

}  // namespace

// ---------------------------------------------------------------------------
// Buffer
// ---------------------------------------------------------------------------

Buffer::Buffer(const VulkanContext& context, VkDeviceSize size, VkBufferUsageFlags usage,
               VkMemoryPropertyFlags memoryProperties)
    : device_(context.device()), size_(size) {
    VkBufferCreateInfo createInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    createInfo.size = size;
    createInfo.usage = usage;
    createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateBuffer(device_, &createInfo, nullptr, &buffer_));

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device_, buffer_, &requirements);
    memory_ = allocateMemory(context, requirements, memoryProperties);
    VK_CHECK(vkBindBufferMemory(device_, buffer_, memory_, 0));

    if (memoryProperties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        VK_CHECK(vkMapMemory(device_, memory_, 0, size, 0, &mapped_));
    }
}

Buffer::~Buffer() { destroy(); }

Buffer::Buffer(Buffer&& other) noexcept { *this = std::move(other); }

Buffer& Buffer::operator=(Buffer&& other) noexcept {
    if (this != &other) {
        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        buffer_ = std::exchange(other.buffer_, VK_NULL_HANDLE);
        memory_ = std::exchange(other.memory_, VK_NULL_HANDLE);
        size_ = std::exchange(other.size_, 0);
        mapped_ = std::exchange(other.mapped_, nullptr);
    }
    return *this;
}

void Buffer::destroy() {
    if (!device_) return;
    if (mapped_) vkUnmapMemory(device_, memory_);
    vkDestroyBuffer(device_, buffer_, nullptr);
    vkFreeMemory(device_, memory_, nullptr);
    device_ = VK_NULL_HANDLE;
}

Buffer createDeviceLocalBuffer(const VulkanContext& context, const void* data, VkDeviceSize size,
                               VkBufferUsageFlags usage) {
    Buffer staging(context, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    std::memcpy(staging.mapped(), data, size);

    Buffer buffer(context, size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    context.submitAndWait([&](VkCommandBuffer commands) {
        VkBufferCopy region{0, 0, size};
        vkCmdCopyBuffer(commands, staging.handle(), buffer.handle(), 1, &region);
    });
    return buffer;
}

// ---------------------------------------------------------------------------
// Image
// ---------------------------------------------------------------------------

Image::Image(const VulkanContext& context, VkExtent2D extent, VkFormat format, VkImageUsageFlags usage,
             VkImageAspectFlags aspect)
    : device_(context.device()), format_(format) {
    VkImageCreateInfo createInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    createInfo.imageType = VK_IMAGE_TYPE_2D;
    createInfo.format = format;
    createInfo.extent = {extent.width, extent.height, 1};
    createInfo.mipLevels = 1;
    createInfo.arrayLayers = 1;
    createInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    createInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    createInfo.usage = usage;
    createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK(vkCreateImage(device_, &createInfo, nullptr, &image_));

    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device_, image_, &requirements);
    memory_ = allocateMemory(context, requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vkBindImageMemory(device_, image_, memory_, 0));

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = image_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange = {aspect, 0, 1, 0, 1};
    VK_CHECK(vkCreateImageView(device_, &viewInfo, nullptr, &view_));
}

Image::~Image() { destroy(); }

Image::Image(Image&& other) noexcept { *this = std::move(other); }

Image& Image::operator=(Image&& other) noexcept {
    if (this != &other) {
        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        image_ = std::exchange(other.image_, VK_NULL_HANDLE);
        memory_ = std::exchange(other.memory_, VK_NULL_HANDLE);
        view_ = std::exchange(other.view_, VK_NULL_HANDLE);
        format_ = std::exchange(other.format_, VK_FORMAT_UNDEFINED);
    }
    return *this;
}

void Image::destroy() {
    if (!device_) return;
    vkDestroyImageView(device_, view_, nullptr);
    vkDestroyImage(device_, image_, nullptr);
    vkFreeMemory(device_, memory_, nullptr);
    device_ = VK_NULL_HANDLE;
}

// ---------------------------------------------------------------------------
// Texture
// ---------------------------------------------------------------------------

Texture::Texture(Texture&& other) noexcept { *this = std::move(other); }

Texture& Texture::operator=(Texture&& other) noexcept {
    if (this != &other) {
        if (sampler) vkDestroySampler(device, sampler, nullptr);
        image = std::move(other.image);
        sampler = std::exchange(other.sampler, VK_NULL_HANDLE);
        device = std::exchange(other.device, VK_NULL_HANDLE);
    }
    return *this;
}

Texture::~Texture() {
    if (sampler) vkDestroySampler(device, sampler, nullptr);
}

Texture loadTexture(const VulkanContext& context, const char* path, bool linearFilter, bool repeat) {
    int width = 0, height = 0, channels = 0;
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
        stbi_load(path, &width, &height, &channels, STBI_rgb_alpha), stbi_image_free);
    if (!pixels) throw VulkanError(std::string("cannot load texture '") + path + "': " + stbi_failure_reason());

    VkDeviceSize size = VkDeviceSize(width) * VkDeviceSize(height) * 4;
    Buffer staging(context, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    std::memcpy(staging.mapped(), pixels.get(), size);

    Texture texture;
    texture.device = context.device();
    VkExtent2D extent = {uint32_t(width), uint32_t(height)};
    texture.image = Image(context, extent, VK_FORMAT_R8G8B8A8_UNORM,
                          VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

    context.submitAndWait([&](VkCommandBuffer commands) {
        transitionImage(commands, texture.image.handle(), VK_IMAGE_ASPECT_COLOR_BIT,
                        {VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                         VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT});

        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {extent.width, extent.height, 1};
        vkCmdCopyBufferToImage(commands, staging.handle(), texture.image.handle(),
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        transitionImage(commands, texture.image.handle(), VK_IMAGE_ASPECT_COLOR_BIT,
                        {VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                         VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
                         VK_ACCESS_2_SHADER_SAMPLED_READ_BIT});
    });

    VkFilter filter = linearFilter ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    VkSamplerAddressMode addressMode = repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = filter;
    samplerInfo.minFilter = filter;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = addressMode;
    samplerInfo.addressModeV = addressMode;
    samplerInfo.addressModeW = addressMode;
    samplerInfo.maxLod = 0.0f;
    VK_CHECK(vkCreateSampler(context.device(), &samplerInfo, nullptr, &texture.sampler));

    return texture;
}

// ---------------------------------------------------------------------------
// Barriers
// ---------------------------------------------------------------------------

void transitionImage(VkCommandBuffer commands, VkImage image, VkImageAspectFlags aspect,
                     const ImageTransition& transition) {
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = transition.srcStage;
    barrier.srcAccessMask = transition.srcAccess;
    barrier.dstStageMask = transition.dstStage;
    barrier.dstAccessMask = transition.dstAccess;
    barrier.oldLayout = transition.oldLayout;
    barrier.newLayout = transition.newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {aspect, 0, 1, 0, 1};

    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commands, &dependency);
}

}  // namespace varyn
