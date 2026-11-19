#include "App.hpp"
#include "UniformLayout.hpp"

#include <cstring>
#include <iostream>

namespace {

void printUsage() {
    std::cout << "usage: varyn [options] <config.toml>\n"
                 "\n"
                 "options:\n"
                 "  --validation     enable the Vulkan validation layer\n"
                 "  --print-header   print the generated varyn.glsl for this config and exit\n"
                 "  -h, --help       show this message\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::cout << std::unitbuf;  // show log lines immediately, even when piped

    varyn::AppOptions options;
    bool printHeader = false;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "--validation") == 0) {
            options.validation = true;
        } else if (std::strcmp(arg, "--print-header") == 0) {
            printHeader = true;
        } else if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            printUsage();
            return 0;
        } else if (arg[0] == '-') {
            std::cerr << "unknown option '" << arg << "'\n";
            printUsage();
            return 1;
        } else {
            options.configFile = arg;
        }
    }

    if (options.configFile.empty()) {
        printUsage();
        return 1;
    }

    try {
        if (printHeader) {
            varyn::Config config = varyn::loadConfig(options.configFile);
            std::cout << varyn::generateShaderHeader(varyn::UniformLayout(config.uniforms), config.textures);
            return 0;
        }

        varyn::App app(std::move(options));
        app.run();
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
    return 0;
}
