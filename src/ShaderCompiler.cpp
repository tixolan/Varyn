#include "ShaderCompiler.hpp"

#include <shaderc/shaderc.hpp>

#include <fstream>
#include <memory>
#include <optional>
#include <sstream>

namespace varyn {

namespace {

constexpr const char* kGeneratedHeaderName = "varyn.glsl";

std::optional<std::string> readTextFile(const fs::path& file) {
    std::ifstream input(file, std::ios::binary);
    if (!input) return std::nullopt;
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

// Resolves #include directives for shaderc and records every file it opens,
// so the caller knows which files to watch for changes.
class Includer : public shaderc::CompileOptions::IncluderInterface {
public:
    Includer(const std::string& generatedHeader, const std::vector<fs::path>& includeDirectories,
             std::shared_ptr<std::vector<fs::path>> dependencies)
        : generatedHeader_(generatedHeader),
          includeDirectories_(includeDirectories),
          dependencies_(std::move(dependencies)) {}

    shaderc_include_result* GetInclude(const char* requestedSource, shaderc_include_type type,
                                       const char* requestingSource, size_t /*includeDepth*/) override {
        auto* result = new IncludeResult;
        std::string requested = requestedSource;

        if (requested == kGeneratedHeaderName) {
            result->setContent(kGeneratedHeaderName, generatedHeader_);
            return result;
        }

        std::optional<fs::path> resolved = resolve(requested, type, requestingSource);
        if (!resolved) {
            result->setError("cannot find include file '" + requested + "'");
            return result;
        }

        dependencies_->push_back(*resolved);
        std::optional<std::string> contents = readTextFile(*resolved);
        if (!contents) {
            result->setError("cannot read include file '" + resolved->string() + "'");
            return result;
        }
        result->setContent(resolved->string(), *contents);
        return result;
    }

    void ReleaseInclude(shaderc_include_result* data) override { delete static_cast<IncludeResult*>(data); }

private:
    // shaderc wants a C struct whose strings stay alive until ReleaseInclude;
    // we keep them in the same allocation.
    struct IncludeResult : shaderc_include_result {
        std::string nameStorage;
        std::string contentStorage;

        void setContent(std::string sourceName, std::string text) {
            nameStorage = std::move(sourceName);
            contentStorage = std::move(text);
            publish();
        }
        void setError(std::string message) {
            nameStorage.clear();  // an empty name tells shaderc the include failed
            contentStorage = std::move(message);
            publish();
        }
        void publish() {
            source_name = nameStorage.c_str();
            source_name_length = nameStorage.size();
            content = contentStorage.c_str();
            content_length = contentStorage.size();
            user_data = nullptr;
        }
    };

    std::optional<fs::path> resolve(const std::string& requested, shaderc_include_type type,
                                     const std::string& requestingSource) const {
        std::vector<fs::path> candidates;
        if (type == shaderc_include_type_relative) {
            candidates.push_back(fs::path(requestingSource).parent_path() / requested);
        }
        for (const fs::path& directory : includeDirectories_) candidates.push_back(directory / requested);

        for (const fs::path& candidate : candidates) {
            std::error_code error;
            if (fs::is_regular_file(candidate, error)) return candidate.lexically_normal();
        }
        return std::nullopt;
    }

    const std::string& generatedHeader_;
    const std::vector<fs::path>& includeDirectories_;
    std::shared_ptr<std::vector<fs::path>> dependencies_;
};

}  // namespace

ShaderCompiler::ShaderCompiler() : compiler_(std::make_unique<shaderc::Compiler>()) {}

ShaderCompiler::~ShaderCompiler() = default;

ShaderCompileResult ShaderCompiler::compile(const fs::path& file, ShaderStage stage) {
    ShaderCompileResult result;
    result.dependencies.push_back(file);

    std::optional<std::string> source = readTextFile(file);
    if (!source) {
        result.log = "cannot read shader '" + file.string() + "'";
        return result;
    }

    auto dependencies = std::make_shared<std::vector<fs::path>>();

    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
    options.SetSourceLanguage(shaderc_source_language_glsl);
    // No SPIR-V optimization: the driver optimizes anyway, and skipping it
    // keeps reloads fast. Debug info lets tools like RenderDoc show the source.
    options.SetOptimizationLevel(shaderc_optimization_level_zero);
    options.SetGenerateDebugInfo();
    options.SetIncluder(std::make_unique<Includer>(generatedHeader_, includeDirectories_, dependencies));
    options.AddMacroDefinition(stage == ShaderStage::Vertex ? "VARYN_VERTEX_SHADER" : "VARYN_FRAGMENT_SHADER");

    shaderc_shader_kind kind = stage == ShaderStage::Vertex ? shaderc_vertex_shader : shaderc_fragment_shader;

    shaderc::SpvCompilationResult compilation =
        compiler_->CompileGlslToSpv(*source, kind, file.string().c_str(), "main", options);

    result.dependencies.insert(result.dependencies.end(), dependencies->begin(), dependencies->end());
    result.log = compilation.GetErrorMessage();
    result.success = compilation.GetCompilationStatus() == shaderc_compilation_status_success;
    if (result.success) result.spirv.assign(compilation.cbegin(), compilation.cend());
    return result;
}

}  // namespace varyn
