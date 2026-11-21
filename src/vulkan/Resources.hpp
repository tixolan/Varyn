#pragma once

#include "vulkan/VulkanContext.hpp"

#include <cstddef>

namespace varyn {

// A VkBuffer with its own memory. Move-only; frees itself on destruction.
// Host visible buffers stay mapped for their whole lifetime.
class Buffer {
public:
    Buffer() = default;
    Buffer(const VulkanContext& context, VkDeviceSize size, VkBufferUsageFlags usage,
           VkMemoryPropertyFlags memoryProperties);
    ~Buffer();

    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    VkBuffer handle() const { return buffer_; }
    VkDeviceSize size() const { return size_; }
    void* mapped() const { return mapped_; }  // nullptr unless host visible

private:
    void destroy();

    VkDevice device_ = VK_NULL_HANDLE;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkDeviceSize size_ = 0;
    void* mapped_ = nullptr;
};

// Creates a GPU-only buffer and fills it through a temporary staging buffer.
Buffer createDeviceLocalBuffer(const VulkanContext& context, const void* data, VkDeviceSize size,
                               VkBufferUsageFlags usage);

// A 2D VkImage (optionally with several array layers) with memory and a
// view covering every layer. Move-only.
class Image {
public:
    Image() = default;
    Image(const VulkanContext& context, VkExtent2D extent, VkFormat format, VkImageUsageFlags usage,
          VkImageAspectFlags aspect, uint32_t arrayLayers = 1, VkImageCreateFlags flags = 0,
          VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D);
    ~Image();

    Image(Image&& other) noexcept;
    Image& operator=(Image&& other) noexcept;
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;

    VkImage handle() const { return image_; }
    VkImageView view() const { return view_; }
    VkFormat format() const { return format_; }

private:
    void destroy();

    VkDevice device_ = VK_NULL_HANDLE;
    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
};

// An image plus its sampler, ready to be bound as a sampler2D.
struct Texture {
    Image image;
    VkSampler sampler = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;

    Texture() = default;
    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;
    ~Texture();
};

// Loads a PNG/JPG/TGA/BMP/... file into a texture (RGBA8).
Texture loadTexture(const VulkanContext& context, const char* path, bool linearFilter, bool repeat);

// Records a pipeline barrier that moves `image` (every mip level and array
// layer) from one layout to another.
struct ImageTransition {
    VkImageLayout oldLayout;
    VkImageLayout newLayout;
    VkPipelineStageFlags2 srcStage;
    VkAccessFlags2 srcAccess;
    VkPipelineStageFlags2 dstStage;
    VkAccessFlags2 dstAccess;
};
void transitionImage(VkCommandBuffer commands, VkImage image, VkImageAspectFlags aspect,
                     const ImageTransition& transition);

}  // namespace varyn
