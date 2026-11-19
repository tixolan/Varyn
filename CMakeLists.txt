cmake_minimum_required(VERSION 3.24)
project(Varyn LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# ---------------------------------------------------------------------------
# Vulkan SDK
#
# If the LunarG setup-env.sh script has been sourced, VULKAN_SDK is already
# set. Otherwise fall back to the newest SDK found in ~/vulkan.
# ---------------------------------------------------------------------------
if(NOT DEFINED ENV{VULKAN_SDK})
    file(GLOB VARYN_SDK_CANDIDATES "$ENV{HOME}/vulkan/*/x86_64")
    list(SORT VARYN_SDK_CANDIDATES ORDER DESCENDING)
    list(GET VARYN_SDK_CANDIDATES 0 VARYN_DEFAULT_SDK)
    set(ENV{VULKAN_SDK} "${VARYN_DEFAULT_SDK}")
endif()
message(STATUS "Using Vulkan SDK: $ENV{VULKAN_SDK}")
list(APPEND CMAKE_PREFIX_PATH "$ENV{VULKAN_SDK}" "$ENV{VULKAN_SDK}/lib/VulkanLoader")

find_package(Vulkan REQUIRED COMPONENTS shaderc_combined)
find_package(glfw3 3.3 REQUIRED)
find_package(glm REQUIRED)
find_package(Threads REQUIRED)
find_path(STB_INCLUDE_DIR stb_image.h PATH_SUFFIXES stb REQUIRED)

# toml++ (header only)
include(FetchContent)
FetchContent_Declare(tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG        v3.4.0
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(tomlplusplus)

# ---------------------------------------------------------------------------
# Varyn
# ---------------------------------------------------------------------------
add_executable(varyn
    src/main.cpp
    src/App.cpp
    src/Config.cpp
    src/Expression.cpp
    src/FileWatcher.cpp
    src/Mesh.cpp
    src/ShaderCompiler.cpp
    src/UniformLayout.cpp
    src/vulkan/VulkanContext.cpp
    src/vulkan/Swapchain.cpp
    src/vulkan/Resources.cpp
    src/render/Scene.cpp
    src/render/Renderer.cpp
)

target_include_directories(varyn PRIVATE src ${STB_INCLUDE_DIR})
target_compile_definitions(varyn PRIVATE
    GLFW_INCLUDE_VULKAN
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_FORCE_RADIANS)
target_link_libraries(varyn PRIVATE
    Vulkan::Vulkan
    Vulkan::shaderc_combined
    glfw
    glm::glm
    tomlplusplus::tomlplusplus
    Threads::Threads)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(varyn PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
endif()
