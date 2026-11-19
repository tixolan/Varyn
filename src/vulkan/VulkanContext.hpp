#pragma once

#include <GLFW/glfw3.h>  // GLFW_INCLUDE_VULKAN is defined by CMake

#include <functional>
#include <stdexcept>
#include <string>

namespace varyn {

class VulkanError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Throws a VulkanError naming the failed call if `result` is an error.
void checkVk(VkResult result, const char* call);
#define VK_CHECK(call) ::varyn::checkVk((call), #call)

// Owns the Vulkan instance, the window surface, the logical device and the
// one queue we use for both graphics and presentation.
//
// Varyn requires Vulkan 1.3 for dynamic rendering (no render pass objects)
// and synchronization2 (simpler barriers).
class VulkanContext {
public:
    VulkanContext(GLFWwindow* window, bool enableValidation);
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    VkInstance instance() const { return instance_; }
    VkSurfaceKHR surface() const { return surface_; }
    VkPhysicalDevice physicalDevice() const { return physicalDevice_; }
    VkDevice device() const { return device_; }
    VkQueue queue() const { return queue_; }
    uint32_t queueFamily() const { return queueFamily_; }
    VkCommandPool commandPool() const { return commandPool_; }
    const std::string& deviceName() const { return deviceName_; }

    uint32_t findMemoryType(uint32_t allowedTypeBits, VkMemoryPropertyFlags properties) const;

    // Records commands with `record`, submits them and waits for completion.
    // Meant for one-off work such as uploading buffers and textures.
    void submitAndWait(const std::function<void(VkCommandBuffer)>& record) const;

    void waitIdle() const { vkDeviceWaitIdle(device_); }

private:
    void createInstance(bool enableValidation);
    void pickPhysicalDevice();
    void createDevice();

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queueFamily_ = 0;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::string deviceName_;
};

}  // namespace varyn
