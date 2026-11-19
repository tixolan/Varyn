#pragma once

#include "Config.hpp"
#include "FileWatcher.hpp"
#include "render/Renderer.hpp"
#include "vulkan/Swapchain.hpp"
#include "vulkan/VulkanContext.hpp"

#include <memory>

namespace varyn {

struct AppOptions {
    fs::path configFile;
    bool validation = false;
};

// Owns the window and ties everything together:
//   - keeps the clock (time, delta time, frame count)
//   - watches the config, assets and shaders for changes and reloads them
//   - handles keyboard shortcuts
class App {
public:
    explicit App(AppOptions options);
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    void run();

private:
    struct Clock {
        double time = 0.0;
        double deltaTime = 0.0;
        uint64_t frame = 0;
        bool paused = false;
        double lastTimestamp = 0.0;  // glfwGetTime() of the previous frame
    };

    void tickClock();
    void checkForChanges();
    void reloadConfig();
    void applyWindowSettings(const WindowConfig& previous);
    void recreateSwapchain();
    void updateWindowTitle();

    static void onKey(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void onFramebufferResize(GLFWwindow* window, int width, int height);

    AppOptions options_;
    Config config_;
    Clock clock_;

    GLFWwindow* window_ = nullptr;
    std::unique_ptr<VulkanContext> context_;
    std::unique_ptr<Swapchain> swapchain_;
    std::unique_ptr<Renderer> renderer_;

    FileWatcher configWatcher_;  // config file, textures and models: full reload
    FileWatcher shaderWatcher_;  // shaders and their modules: recompile only
    double lastWatchPoll_ = 0.0;
    double lastTitleUpdate_ = 0.0;
    uint64_t framesSinceTitleUpdate_ = 0;
    bool framebufferResized_ = false;
};

}  // namespace varyn
