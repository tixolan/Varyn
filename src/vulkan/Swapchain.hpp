#pragma once

#include "vulkan/Resources.hpp"
#include "vulkan/VulkanContext.hpp"

#include <vector>

namespace varyn {

// The swapchain images we render into, plus a matching depth buffer.
class Swapchain {
public:
    Swapchain(const VulkanContext& context, GLFWwindow* window, bool vsync);
    ~Swapchain();

    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;

    // Rebuilds everything for the current window size. Call after a resize
    // or when presentation reports the swapchain as out of date.
    void recreate(bool vsync);

    VkSwapchainKHR handle() const { return swapchain_; }
    VkExtent2D extent() const { return extent_; }
    VkFormat colorFormat() const { return colorFormat_; }
    VkFormat depthFormat() const { return depthFormat_; }

    uint32_t imageCount() const { return uint32_t(images_.size()); }
    VkImage image(uint32_t index) const { return images_[index]; }
    VkImageView imageView(uint32_t index) const { return imageViews_[index]; }
    const Image& depthImage() const { return depthImage_; }

    // Signalled when rendering into image `index` has finished. One per
    // image, because presentation may still hold the previous one.
    VkSemaphore renderFinished(uint32_t index) const { return renderFinished_[index]; }

private:
    void create(bool vsync);
    void destroy();

    const VulkanContext& context_;
    GLFWwindow* window_;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkExtent2D extent_{};
    VkFormat colorFormat_ = VK_FORMAT_UNDEFINED;
    VkFormat depthFormat_ = VK_FORMAT_D32_SFLOAT;
    std::vector<VkImage> images_;
    std::vector<VkImageView> imageViews_;
    std::vector<VkSemaphore> renderFinished_;
    Image depthImage_;
};

}  // namespace varyn
