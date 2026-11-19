#include "App.hpp"

#include <cstdio>
#include <iostream>

namespace varyn {

namespace {

constexpr double kWatchInterval = 0.2;  // seconds between file change checks

}  // namespace

App::App(AppOptions options) : options_(std::move(options)) {
    // The first config must load: we need it to even create the window.
    config_ = loadConfig(options_.configFile);

    if (!glfwInit()) throw std::runtime_error("failed to initialize GLFW");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window_ = glfwCreateWindow(config_.window.width, config_.window.height, config_.window.title.c_str(), nullptr,
                               nullptr);
    if (!window_) throw std::runtime_error("failed to create a window");

    glfwSetWindowUserPointer(window_, this);
    glfwSetKeyCallback(window_, onKey);
    glfwSetFramebufferSizeCallback(window_, onFramebufferResize);

    context_ = std::make_unique<VulkanContext>(window_, options_.validation);
    swapchain_ = std::make_unique<Swapchain>(*context_, window_, config_.window.vsync);
    renderer_ = std::make_unique<Renderer>(*context_, *swapchain_);
    std::cout << "[varyn] running on " << context_->deviceName() << "\n";

    renderer_->setScene(config_, std::make_unique<Scene>(*context_, config_));
    configWatcher_.setFiles(config_.watchedFiles());
    shaderWatcher_.setFiles(renderer_->shaderDependencies());

    std::cout << "[varyn] watching " << config_.sourceFile.string() << "\n"
              << "[varyn] keys: Space = pause, R = restart time, F5 = reload everything, Esc = quit\n";
    clock_.lastTimestamp = glfwGetTime();
}

App::~App() {
    // Destroy in reverse order of creation.
    renderer_.reset();
    swapchain_.reset();
    context_.reset();
    if (window_) glfwDestroyWindow(window_);
    glfwTerminate();
}

void App::run() {
    while (!glfwWindowShouldClose(window_)) {
        glfwPollEvents();

        // Nothing to draw into while minimized.
        int width = 0, height = 0;
        glfwGetFramebufferSize(window_, &width, &height);
        if (width == 0 || height == 0) {
            glfwWaitEvents();
            continue;
        }

        checkForChanges();
        tickClock();

        ExpressionContext expressionClock;
        expressionClock.time = clock_.time;
        expressionClock.deltaTime = clock_.deltaTime;
        expressionClock.frame = double(clock_.frame);

        bool swapchainValid = renderer_->drawFrame(expressionClock);
        if (!swapchainValid || framebufferResized_) recreateSwapchain();

        if (!clock_.paused) ++clock_.frame;
        updateWindowTitle();
    }
}

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------

void App::tickClock() {
    double now = glfwGetTime();
    double elapsed = now - clock_.lastTimestamp;
    clock_.lastTimestamp = now;

    clock_.deltaTime = clock_.paused ? 0.0 : elapsed;
    clock_.time += clock_.deltaTime;
}

// ---------------------------------------------------------------------------
// Hot reloading
// ---------------------------------------------------------------------------

void App::checkForChanges() {
    double now = glfwGetTime();
    if (now - lastWatchPoll_ < kWatchInterval) return;
    lastWatchPoll_ = now;

    if (configWatcher_.poll()) {
        reloadConfig();
    } else if (shaderWatcher_.poll()) {
        std::cout << "[varyn] shader changed, recompiling\n";
        renderer_->reloadShaders();
        shaderWatcher_.setFiles(renderer_->shaderDependencies());  // includes may have changed
    }
}

// Loads the config and its assets into a brand new Scene first. Only if all
// of that succeeds is the running scene replaced; otherwise the error is
// printed and the previous state keeps running.
void App::reloadConfig() {
    std::cout << "[varyn] reloading " << config_.sourceFile.filename().string() << "\n";
    try {
        Config newConfig = loadConfig(options_.configFile);
        auto newScene = std::make_unique<Scene>(*context_, newConfig);

        WindowConfig previousWindow = config_.window;
        config_ = newConfig;
        renderer_->setScene(std::move(newConfig), std::move(newScene));
        applyWindowSettings(previousWindow);
    } catch (const std::exception& error) {
        std::cerr << "[config] " << error.what() << "\n[config] keeping the previous configuration\n";
    }

    // Watch the new set of files, even if loading failed, so a fix is noticed.
    configWatcher_.setFiles(config_.watchedFiles());
    shaderWatcher_.setFiles(renderer_->shaderDependencies());
}

void App::applyWindowSettings(const WindowConfig& previous) {
    const WindowConfig& current = config_.window;
    if (current.width != previous.width || current.height != previous.height) {
        glfwSetWindowSize(window_, current.width, current.height);
    }
    if (current.vsync != previous.vsync) recreateSwapchain();
    updateWindowTitle();
}

void App::recreateSwapchain() {
    framebufferResized_ = false;
    swapchain_->recreate(config_.window.vsync);
}

// ---------------------------------------------------------------------------
// Window
// ---------------------------------------------------------------------------

void App::updateWindowTitle() {
    ++framesSinceTitleUpdate_;
    double now = glfwGetTime();
    double elapsed = now - lastTitleUpdate_;
    if (elapsed < 0.5) return;

    char title[512];
    std::snprintf(title, sizeof(title), "%s  |  %.0f fps  |  t = %.2f%s", config_.window.title.c_str(),
                  double(framesSinceTitleUpdate_) / elapsed, clock_.time, clock_.paused ? "  (paused)" : "");
    glfwSetWindowTitle(window_, title);

    lastTitleUpdate_ = now;
    framesSinceTitleUpdate_ = 0;
}

void App::onKey(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/) {
    if (action != GLFW_PRESS) return;
    App& app = *static_cast<App*>(glfwGetWindowUserPointer(window));

    switch (key) {
        case GLFW_KEY_ESCAPE:
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            break;
        case GLFW_KEY_SPACE:
            app.clock_.paused = !app.clock_.paused;
            break;
        case GLFW_KEY_R:
            app.clock_.time = 0.0;
            app.clock_.frame = 0;
            break;
        case GLFW_KEY_F5:
            app.reloadConfig();
            break;
    }
}

void App::onFramebufferResize(GLFWwindow* window, int /*width*/, int /*height*/) {
    static_cast<App*>(glfwGetWindowUserPointer(window))->framebufferResized_ = true;
}

}  // namespace varyn
