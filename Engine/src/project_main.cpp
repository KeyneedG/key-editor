#include "tiny3d/project.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>

// CMake supplies the real values; these defaults are only for Visual Studio's parser.
#ifdef __INTELLISENSE__
#ifndef TINY3D_PROJECT_DIRECTORY
#define TINY3D_PROJECT_DIRECTORY "."
#endif
#ifndef TINY3D_PROJECT_MODE
#define TINY3D_PROJECT_MODE "editor"
#endif
#endif

int main(int argc, char** argv) {
    try {
        // Resolve output paths before switching to the project's asset directory.
        std::filesystem::path output;
        unsigned frames = 0;
        if (argc == 3 && std::string(argv[1]) == "--render") {
            output = std::filesystem::absolute(argv[2]);
        } else if (argc == 3 && std::string(argv[1]) == "--frames") {
            const std::string count = argv[2];
            if (count.empty() || count.find_first_not_of("0123456789") != std::string::npos) {
                throw std::invalid_argument("Frame count must be a positive integer");
            }
            const auto value = std::stoul(count);
            if (value == 0 || value > 1000000) throw std::invalid_argument("Frame count must be between 1 and 1000000");
            frames = static_cast<unsigned>(value);
        } else if (argc != 1) {
            std::cerr << "Usage: tiny3d_project [--render file.ppm | --frames count]\n";
            return 1;
        }
        std::filesystem::current_path(TINY3D_PROJECT_DIRECTORY);
        auto game = tiny3d::createGame();
        if (!game) throw std::runtime_error("createGame() returned null");
        std::cout << "Project: " << TINY3D_PROJECT_DIRECTORY << " | Mode: " << TINY3D_PROJECT_MODE << '\n';
        if (!output.empty()) {
            const auto settings = game->screenSettings();
            const int width = settings.width ? settings.width : 800;
            const int height = settings.height ? settings.height : 500;
            tiny3d::Input input;
            input.renderWidth = width;
            input.renderHeight = height;
            input.viewport = {0, 0, width, height};
            game->update(0, input);
            tiny3d::Renderer renderer(width, height);
            renderer.render(game->scene());
            renderer.savePPM(output.string());
            std::cout << "Saved " << output.string() << '\n';
            return 0;
        }
        // Both modes intentionally use the same runtime until editor tools are added.
        return tiny3d::run(*game, 800, 500, frames);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
