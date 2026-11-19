#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace shaderc {
class Compiler;
}

namespace varyn {

namespace fs = std::filesystem;

enum class ShaderStage { Vertex, Fragment };

struct ShaderCompileResult {
    bool success = false;
    std::vector<uint32_t> spirv;
    std::string log;                     // errors and warnings
    std::vector<fs::path> dependencies;  // the shader file plus every file it #included
};

// Compiles GLSL to SPIR-V in-process using shaderc.
//
// Modules: shaders can `#include "noise.glsl"`. Includes are resolved
// relative to the including file first, then in each include directory.
// `#include <varyn.glsl>` is special: it returns the generated header with
// all uniforms, push constants, textures and vertex inputs.
class ShaderCompiler {
public:
    ShaderCompiler();
    ~ShaderCompiler();

    void setGeneratedHeader(std::string header) { generatedHeader_ = std::move(header); }
    void setIncludeDirectories(std::vector<fs::path> directories) { includeDirectories_ = std::move(directories); }

    ShaderCompileResult compile(const fs::path& file, ShaderStage stage);

private:
    // Kept alive between compiles: creating a shaderc compiler and running
    // its first compilation is slow (~100+ ms), later ones take ~1 ms.
    std::unique_ptr<shaderc::Compiler> compiler_;
    std::string generatedHeader_;
    std::vector<fs::path> includeDirectories_;
};

}  // namespace varyn
