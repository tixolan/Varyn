#include "Config.hpp"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cctype>
#include <iostream>
#include <set>

namespace varyn {

namespace {

// ---------------------------------------------------------------------------
// Small helpers for reading TOML nodes with good error messages. Every
// function receives `where`, the dotted key path used in error messages.
// ---------------------------------------------------------------------------

[[noreturn]] void fail(const std::string& where, const std::string& message) {
    throw ConfigError(where + ": " + message);
}

void warnAboutUnknownKeys(const toml::table& table, const std::string& where,
                          std::initializer_list<std::string_view> knownKeys) {
    for (auto&& [key, value] : table) {
        bool known = std::find(knownKeys.begin(), knownKeys.end(), key.str()) != knownKeys.end();
        if (!known) {
            std::cerr << "[config] warning: unknown key '" << where << "." << key.str() << "'\n";
        }
    }
}

bool isIdentifier(const std::string& name) {
    if (name.empty() || std::isdigit(static_cast<unsigned char>(name[0]))) return false;
    return std::all_of(name.begin(), name.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    });
}

// A number, a boolean, or a string containing an expression.
Expression readExpression(const toml::node& node, const std::string& where) {
    if (auto number = node.value<double>()) return *number;
    if (auto boolean = node.value<bool>()) return *boolean ? 1.0 : 0.0;
    if (auto text = node.value<std::string>()) {
        try {
            return Expression::parse(*text);
        } catch (const ExpressionError& error) {
            fail(where, error.what());
        }
    }
    fail(where, "expected a number or an expression string");
}

// Either an array of exactly N values, or a single value used for every
// component (handy for `scale = 2`).
template <size_t N>
std::array<Expression, N> readVector(const toml::node& node, const std::string& where) {
    std::array<Expression, N> result;

    const toml::array* array = node.as_array();
    if (!array) {
        result.fill(readExpression(node, where));
        return result;
    }
    if (array->size() != N) {
        fail(where, "expected " + std::to_string(N) + " components, got " + std::to_string(array->size()));
    }
    for (size_t i = 0; i < N; ++i) {
        result[i] = readExpression(*array->get(i), where + "[" + std::to_string(i) + "]");
    }
    return result;
}

std::string readString(const toml::node& node, const std::string& where) {
    if (auto text = node.value<std::string>()) return *text;
    fail(where, "expected a string");
}

fs::path readPath(const toml::node& node, const std::string& where, const fs::path& baseDirectory) {
    fs::path path = readString(node, where);
    return path.is_absolute() ? path : (baseDirectory / path).lexically_normal();
}

// Calls `read(table, where)` for every table in an array-of-tables.
template <typename Fn>
void forEachTable(const toml::node* node, const std::string& where, Fn&& read) {
    if (!node) return;
    const toml::array* array = node->as_array();
    if (!array) fail(where, "expected an array of tables, e.g. [[" + where + "]]");

    for (size_t i = 0; i < array->size(); ++i) {
        std::string itemWhere = where + "[" + std::to_string(i) + "]";
        const toml::table* table = array->get(i)->as_table();
        if (!table) fail(itemWhere, "expected a table");
        read(*table, itemWhere);
    }
}

// ---------------------------------------------------------------------------
// Sections
// ---------------------------------------------------------------------------

WindowConfig readWindow(const toml::table& table) {
    const std::string where = "window";
    warnAboutUnknownKeys(table, where, {"title", "width", "height", "vsync", "clear_color"});

    WindowConfig window;
    window.title = table["title"].value_or(window.title);
    window.width = table["width"].value_or(window.width);
    window.height = table["height"].value_or(window.height);
    window.vsync = table["vsync"].value_or(window.vsync);
    if (auto node = table.get("clear_color")) {
        window.clearColor = readVector<4>(*node, where + ".clear_color");
    }
    return window;
}

ShaderConfig readShaders(const toml::table& table, const fs::path& base) {
    const std::string where = "shaders";
    warnAboutUnknownKeys(table, where, {"vertex", "fragment", "include_dirs"});

    ShaderConfig shaders;
    if (auto node = table.get("vertex")) shaders.defaults.vertex = readPath(*node, where + ".vertex", base);
    if (auto node = table.get("fragment")) shaders.defaults.fragment = readPath(*node, where + ".fragment", base);

    if (shaders.defaults.vertex.empty()) fail(where, "missing 'vertex' shader path");
    if (shaders.defaults.fragment.empty()) fail(where, "missing 'fragment' shader path");

    if (auto node = table.get("include_dirs")) {
        const toml::array* array = node->as_array();
        if (!array) fail(where + ".include_dirs", "expected an array of paths");
        for (size_t i = 0; i < array->size(); ++i) {
            shaders.includeDirs.push_back(
                readPath(*array->get(i), where + ".include_dirs[" + std::to_string(i) + "]", base));
        }
    }
    return shaders;
}

CameraConfig readCamera(const toml::table& table) {
    const std::string where = "camera";
    warnAboutUnknownKeys(table, where, {"position", "target", "up", "fov", "near", "far"});

    CameraConfig camera;
    if (auto node = table.get("position")) camera.position = readVector<3>(*node, where + ".position");
    if (auto node = table.get("target")) camera.target = readVector<3>(*node, where + ".target");
    if (auto node = table.get("up")) camera.up = readVector<3>(*node, where + ".up");
    if (auto node = table.get("fov")) camera.fov = readExpression(*node, where + ".fov");
    if (auto node = table.get("near")) camera.near = readExpression(*node, where + ".near");
    if (auto node = table.get("far")) camera.far = readExpression(*node, where + ".far");
    return camera;
}

TextureConfig readTexture(const toml::table& table, const std::string& where, const fs::path& base) {
    warnAboutUnknownKeys(table, where, {"name", "path", "filter", "wrap"});

    TextureConfig texture;
    const toml::node* nameNode = table.get("name");
    if (!nameNode) fail(where, "missing 'name'");
    texture.name = readString(*nameNode, where + ".name");
    if (!isIdentifier(texture.name)) fail(where + ".name", "'" + texture.name + "' is not a valid GLSL identifier");

    const toml::node* pathNode = table.get("path");
    if (!pathNode) fail(where, "missing 'path'");
    texture.path = readPath(*pathNode, where + ".path", base);

    std::string filter = table["filter"].value_or(std::string("linear"));
    if (filter != "linear" && filter != "nearest") fail(where + ".filter", "expected \"linear\" or \"nearest\"");
    texture.linearFilter = filter == "linear";

    std::string wrap = table["wrap"].value_or(std::string("repeat"));
    if (wrap != "repeat" && wrap != "clamp") fail(where + ".wrap", "expected \"repeat\" or \"clamp\"");
    texture.repeat = wrap == "repeat";
    return texture;
}

// Like readVector<3>, but a string naming a 3-component uniform reuses that
// uniform's expressions.
Vec3Expression readInstanceVector(const toml::node& node, const std::string& where,
                                  const std::vector<UniformConfig>& uniforms) {
    if (auto text = node.value<std::string>()) {
        for (const UniformConfig& uniform : uniforms) {
            if (uniform.name != *text) continue;
            if (uniform.components.size() != 3) {
                fail(where, "uniform '" + uniform.name + "' has " + std::to_string(uniform.components.size()) +
                                " components, expected 3");
            }
            return {uniform.components[0], uniform.components[1], uniform.components[2]};
        }
    }
    return readVector<3>(node, where);
}

InstanceConfig readInstance(const toml::table& table, const std::string& where,
                            const std::vector<UniformConfig>& uniforms) {
    warnAboutUnknownKeys(table, where, {"count", "position", "rotation", "scale"});

    InstanceConfig instance;
    instance.count = table["count"].value_or(1);
    if (instance.count < 0) fail(where + ".count", "must not be negative");
    if (auto node = table.get("position")) instance.position = readInstanceVector(*node, where + ".position", uniforms);
    if (auto node = table.get("rotation")) instance.rotation = readInstanceVector(*node, where + ".rotation", uniforms);
    if (auto node = table.get("scale")) instance.scale = readInstanceVector(*node, where + ".scale", uniforms);
    return instance;
}

ModelConfig readModel(const toml::table& table, const std::string& where, const fs::path& base,
                      const ShaderPair& defaultShaders, const std::vector<UniformConfig>& uniforms) {
    warnAboutUnknownKeys(table, where, {"name", "path", "builtin", "vertex", "fragment", "metallic", "instances", "passes"});

    ModelConfig model;
    model.name = table["name"].value_or(where);

    if (auto node = table.get("path")) model.path = readPath(*node, where + ".path", base);
    model.builtin = table["builtin"].value_or(std::string());
    if (model.path && !model.builtin.empty()) fail(where, "use either 'path' or 'builtin', not both");
    if (!model.path && model.builtin.empty()) fail(where, "needs a 'path' to an .obj file or a 'builtin' mesh");

    model.shaders = defaultShaders;
    if (auto node = table.get("vertex")) model.shaders.vertex = readPath(*node, where + ".vertex", base);
    if (auto node = table.get("fragment")) model.shaders.fragment = readPath(*node, where + ".fragment", base);

    if (auto node = table.get("metallic")) {
        model.metallic = readExpression(*node, where + ".metallic");
        if (model.metallic->isConstant()) {
            double value = model.metallic->evaluate({});
            if (value < 0.0 || value > 1.0) fail(where + ".metallic", "must be between 0.0 and 1.0");
            if (value == 0.0) model.metallic.reset();  // no environment map needed
        }
    }

    forEachTable(table.get("instances"), where + ".instances", [&](const toml::table& item, const std::string& itemWhere) {
        model.instances.push_back(readInstance(item, itemWhere, uniforms));
    });
    if (model.instances.empty()) model.instances.emplace_back();  // one instance at the origin

    forEachTable(table.get("passes"), where + ".passes", [&](const toml::table& item, const std::string& itemWhere) {
        warnAboutUnknownKeys(item, itemWhere, {"vertex", "fragment"});
        ShaderPair pass = model.shaders;  // each stage defaults to the model's own
        if (auto node = item.get("vertex")) pass.vertex = readPath(*node, itemWhere + ".vertex", base);
        if (auto node = item.get("fragment")) pass.fragment = readPath(*node, itemWhere + ".fragment", base);
        model.extraPasses.push_back(std::move(pass));
    });
    return model;
}

void readUniforms(const toml::table& table, std::vector<UniformConfig>& uniforms) {
    for (auto&& [key, value] : table) {
        std::string where = "uniforms." + std::string(key.str());

        UniformConfig uniform;
        uniform.name = std::string(key.str());
        if (!isIdentifier(uniform.name)) fail(where, "not a valid GLSL identifier");

        // An array uniform: [uniforms.name] with `count` and `value`.
        const toml::node* valueNode = &value;
        if (const toml::table* arrayTable = value.as_table()) {
            warnAboutUnknownKeys(*arrayTable, where, {"count", "value"});
            uniform.count = (*arrayTable)["count"].value_or(0);
            if (uniform.count < 1) fail(where + ".count", "must be at least 1");
            valueNode = arrayTable->get("value");
            if (!valueNode) fail(where, "missing 'value'");
            where += ".value";
        }

        if (const toml::array* array = valueNode->as_array()) {
            if (array->empty() || array->size() > 4) fail(where, "arrays must have 1 to 4 components");
            for (size_t i = 0; i < array->size(); ++i) {
                uniform.components.push_back(readExpression(*array->get(i), where + "[" + std::to_string(i) + "]"));
            }
        } else {
            uniform.components.push_back(readExpression(*valueNode, where));
        }
        uniforms.push_back(std::move(uniform));
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

Config loadConfig(const fs::path& file) {
    toml::table root;
    try {
        root = toml::parse_file(file.string());
    } catch (const toml::parse_error& error) {
        const auto& begin = error.source().begin;
        throw ConfigError(file.string() + ":" + std::to_string(begin.line) + ":" + std::to_string(begin.column) +
                          ": " + std::string(error.description()));
    }

    Config config;
    config.sourceFile = fs::absolute(file).lexically_normal();
    const fs::path base = config.sourceFile.parent_path();

    warnAboutUnknownKeys(root, "<root>", {"window", "shaders", "camera", "textures", "models", "uniforms"});

    if (const toml::table* table = root["window"].as_table()) config.window = readWindow(*table);

    const toml::table* shaders = root["shaders"].as_table();
    if (!shaders) throw ConfigError("missing [shaders] section with 'vertex' and 'fragment' paths");
    config.shaders = readShaders(*shaders, base);

    if (const toml::table* table = root["camera"].as_table()) config.camera = readCamera(*table);

    forEachTable(root.get("textures"), "textures", [&](const toml::table& table, const std::string& where) {
        config.textures.push_back(readTexture(table, where, base));
    });

    // Uniforms first: instances may refer to them.
    if (const toml::table* table = root["uniforms"].as_table()) readUniforms(*table, config.uniforms);

    forEachTable(root.get("models"), "models", [&](const toml::table& table, const std::string& where) {
        config.models.push_back(readModel(table, where, base, config.shaders.defaults, config.uniforms));
    });

    // Without any models we draw a single fullscreen quad, which turns Varyn
    // into a classic "fragment shader playground".
    if (config.models.empty()) {
        ModelConfig fullscreen;
        fullscreen.name = "fullscreen";
        fullscreen.builtin = "quad";
        fullscreen.shaders = config.shaders.defaults;
        fullscreen.instances.emplace_back();
        config.models.push_back(std::move(fullscreen));
    }

    // Names must be unique since they all end up in the same GLSL scope, and
    // must not clash with what the generated varyn.glsl header declares.
    std::set<std::string> names = {"view",       "projection",    "viewProjection", "cameraPosition",
                                   "time",       "resolution",    "deltaTime",      "frame",
                                   "modelMatrix", "modelIndex",   "instanceIndex",  "metallic",
                                   "environmentLayer", "varynEnvironment", "sampleEnvironment",
                                   "inPosition", "inNormal",      "inUV"};
    for (const TextureConfig& texture : config.textures) {
        if (!names.insert(texture.name).second) throw ConfigError("textures: name '" + texture.name + "' is reserved or already used");
    }
    for (const UniformConfig& uniform : config.uniforms) {
        if (!names.insert(uniform.name).second) throw ConfigError("uniforms: name '" + uniform.name + "' is reserved or already used");
    }

    return config;
}

std::vector<fs::path> Config::watchedFiles() const {
    std::vector<fs::path> files = {sourceFile};
    for (const TextureConfig& texture : textures) files.push_back(texture.path);
    for (const ModelConfig& model : models) {
        if (model.path) files.push_back(*model.path);
    }
    return files;
}

}  // namespace varyn
