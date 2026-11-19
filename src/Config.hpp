#pragma once

#include "Expression.hpp"

#include <array>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace varyn {

namespace fs = std::filesystem;

// Every number in the config file may also be written as a string
// expression, e.g. `fov = "60 + 10 * sin(time)"`. That is why these structs
// store Expressions instead of plain floats.
using Vec2Expression = std::array<Expression, 2>;
using Vec3Expression = std::array<Expression, 3>;
using Vec4Expression = std::array<Expression, 4>;

struct WindowConfig {
    std::string title = "Varyn";
    int width = 1280;
    int height = 720;
    bool vsync = true;
    Vec4Expression clearColor = {0.0, 0.0, 0.0, 1.0};
};

struct ShaderPair {
    fs::path vertex;
    fs::path fragment;

    auto operator<=>(const ShaderPair&) const = default;
};

struct ShaderConfig {
    ShaderPair defaults;                // used by every model without its own shaders
    std::vector<fs::path> includeDirs;  // searched by #include "module.glsl"
};

struct CameraConfig {
    Vec3Expression position = {0.0, 0.0, 3.0};
    Vec3Expression target = {0.0, 0.0, 0.0};
    Vec3Expression up = {0.0, 1.0, 0.0};
    Expression fov = 60.0;  // vertical, in degrees
    Expression near = 0.1;
    Expression far = 100.0;
};

struct TextureConfig {
    std::string name;  // becomes `uniform sampler2D <name>` in shaders
    fs::path path;
    bool linearFilter = true;
    bool repeat = true;
};

// Placement of one copy of a model. Expressions may use `i` (the instance
// index) and `n` (the instance count) in addition to the time variables.
struct InstanceConfig {
    int count = 1;
    Vec3Expression position = {0.0, 0.0, 0.0};
    Vec3Expression rotation = {0.0, 0.0, 0.0};  // degrees around X, Y, Z
    Vec3Expression scale = {1.0, 1.0, 1.0};
};

struct ModelConfig {
    std::string name;
    std::optional<fs::path> path;  // an .obj file ...
    std::string builtin;           // ... or one of: quad, cube, sphere
    ShaderPair shaders;            // falls back to ShaderConfig::defaults
    std::vector<InstanceConfig> instances;
};

// A user defined value exposed to shaders. One to four components map to
// float, vec2, vec3 or vec4.
struct UniformConfig {
    std::string name;
    std::vector<Expression> components;
};

struct Config {
    fs::path sourceFile;
    WindowConfig window;
    ShaderConfig shaders;
    CameraConfig camera;
    std::vector<TextureConfig> textures;
    std::vector<ModelConfig> models;
    std::vector<UniformConfig> uniforms;

    // Files whose modification should trigger a full reload.
    std::vector<fs::path> watchedFiles() const;
};

class ConfigError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

Config loadConfig(const fs::path& file);  // throws ConfigError

}  // namespace varyn
