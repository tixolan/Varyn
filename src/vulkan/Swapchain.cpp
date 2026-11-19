#include "vulkan/Swapchain.hpp"

#include <algorithm>

namespace varyn {

namespace {

// UNORM formats: what the shader writes is exactly what ends up on screen,
// like in most shader playgrounds.
VkSurfaceFormatKHR chooseSurfaceFormat(VkPhysicalDevice device, VkSurfaceKHR surface) {
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &count, formats.data());

    for (const VkSurfaceFormatKHR& format : formats) {
        bool isUnorm = format.format == VK_FORMAT_B8G8R8A8_UNORM || format.format == VK_FORMAT_R8G8B8A8_UNORM;
        if (isUnorm && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) return format;
    }
    return formats.front();
}

VkPresentModeKHR choosePresentMode(VkPhysicalDevice device, VkSurfaceKHR surface, bool vsync) {
    if (vsync) return VK_PRESENT_MODE_FIFO_KHR;  // always supported

    uint32_t count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &count, nullptr);
    std::vector<VkPresentModeKHR> modes(count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &count, modes.data());

    for (VkPresentModeKHR preferred : {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR}) {
        if (std::find(modes.begin(), modes.end(), preferred) != modes.end()) return preferred;
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

}  // namespace

Swapchain::Swapchain(const VulkanContext& context, GLFWwindow* window, bool vsync)
    : context_(context), window_(window) {
    create(vsync);
}

Swapchain::~Swapchain() { destroy(); }

void Swapchain::recreate(bool vsync) {
    context_.waitIdle();
    destroy();
    create(vsync);
}

void Swapchain::create(bool vsync) {
    VkPhysicalDevice physicalDevice = context_.physicalDevice();
    VkSurfaceKHR surface = context_.surface();

    VkSurfaceCapabilitiesKHR capabilities;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities));

    // Some platforms (Wayland) let us choose the size; use the framebuffer size then.
    if (capabilities.currentExtent.width != UINT32_MAX) {
        extent_ = capabilities.currentExtent;
    } else {
        int width = 0, height = 0;
        glfwGetFramebufferSize(window_, &width, &height);
        extent_.width = std::clamp(uint32_t(width), capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        extent_.height =
            std::clamp(uint32_t(height), capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    }

    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0) imageCount = std::min(imageCount, capabilities.maxImageCount);

    VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(physicalDevice, surface);
    colorFormat_ = surfaceFormat.format;

    VkSwapchainCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent_;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = choosePresentMode(physicalDevice, surface, vsync);
    createInfo.clipped = VK_TRUE;
    VK_CHECK(vkCreateSwapchainKHR(context_.device(), &createInfo, nullptr, &swapchain_));

    uint32_t count = 0;
    vkGetSwapchainImagesKHR(context_.device(), swapchain_, &count, nullptr);
    images_.resize(count);
    vkGetSwapchainImagesKHR(context_.device(), swapchain_, &count, images_.data());

    for (VkImage image : images_) {
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = colorFormat_;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK_CHECK(vkCreateImageView(context_.device(), &viewInfo, nullptr, &imageViews_.emplace_back()));

        VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VK_CHECK(vkCreateSemaphore(context_.device(), &semaphoreInfo, nullptr, &renderFinished_.emplace_back()));
    }

    depthImage_ = Image(context_, extent_, depthFormat_, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                        VK_IMAGE_ASPECT_DEPTH_BIT);
}

void Swapchain::destroy() {
    VkDevice device = context_.device();
    depthImage_ = Image();
    for (VkSemaphore semaphore : renderFinished_) vkDestroySemaphore(device, semaphore, nullptr);
    for (VkImageView view : imageViews_) vkDestroyImageView(device, view, nullptr);
    renderFinished_.clear();
    imageViews_.clear();
    images_.clear();
    if (swapchain_) vkDestroySwapchainKHR(device, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
}

}  // namespace varyn
