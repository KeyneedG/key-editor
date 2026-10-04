#include "tiny3d/project.hpp"
#include "tiny3d/eryscript.hpp"
#include "tiny3d/scene.hpp"
#include "tiny3d/json.hpp"
#include "tiny3d/physics.hpp"
#include "tiny3d/ui.hpp"
#ifdef TINY3D_EDITOR
#include "tiny3d/editor.hpp"
#endif
#ifdef TINY3D_HAS_D3D11
#include "tiny3d/d3d11.hpp"
#endif

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <fstream>

// CMake supplies the real values; these defaults are only for Visual Studio's parser.
#ifdef __INTELLISENSE__
#ifndef TINY3D_PROJECT_DIRECTORY
#define TINY3D_PROJECT_DIRECTORY "."
#endif
#ifndef TINY3D_PROJECT_MODE
#define TINY3D_PROJECT_MODE "editor"
#endif
#endif

namespace {
tiny3d::json::Value projectSettings(const std::filesystem::path& directory) {
    std::ifstream in(directory / "project.json");
    if (!in) throw std::runtime_error("Cannot read project.json");
    return tiny3d::json::parse(std::string((std::istreambuf_iterator<char>(in)), {}));
}
void saveFrame(const std::filesystem::path& path, tiny3d::RenderBackend& renderer) {
    const auto pixels = renderer.readPixels();
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("Cannot open image: " + path.string());
    out << "P6\n" << renderer.width() << ' ' << renderer.height() << "\n255\n";
    for (auto pixel : pixels) {
        const char rgb[]{char(pixel >> 16), char(pixel >> 8), char(pixel)}; out.write(rgb, 3);
    }
    out.close(); if (!out) throw std::runtime_error("Cannot write image: " + path.string());
}
#ifdef TINY3D_SCENE_PROJECT
class SceneGame final : public tiny3d::Game {
public:
    explicit SceneGame(const std::filesystem::path& directory) {
        if (directory.empty()) return;
        const auto settings = projectSettings(directory);
        if (settings.has("startupScene")) scene_ = tiny3d::loadScene(directory / settings.at("startupScene").string());
        if (settings.has("width")) screen_.width = static_cast<int>(settings.at("width").number());
        if (settings.has("height")) screen_.height = static_cast<int>(settings.at("height").number());
        if (settings.has("fullscreen")) screen_.fullscreen = settings.at("fullscreen").boolean();
    }
    void update(float seconds, const tiny3d::Input& input) override { ui_.update(scene_, input); physics_.step(scene_, seconds); }
    tiny3d::Scene& scene() override { return scene_; }
    const tiny3d::Scene& scene() const override { return scene_; }
    tiny3d::ScreenSettings screenSettings() const override { return screen_; }
    std::string title() const override { return "EryEngine"; }
private:
    tiny3d::Scene scene_;
    tiny3d::UI ui_;
    tiny3d::Physics physics_;
    tiny3d::ScreenSettings screen_{1280, 800, false};
};
#endif
}

int main(int argc, char** argv) {
    try {
        // Resolve output paths before switching to the project's asset directory.
        std::filesystem::path output;
        unsigned frames = 0;
        std::string rendererMode = "auto";
#ifdef TINY3D_CONFIG_FILE
        std::ifstream settingsFile(TINY3D_CONFIG_FILE);
        if (settingsFile) {
            const auto settings = tiny3d::json::parse(std::string((std::istreambuf_iterator<char>(settingsFile)), {}));
            if (settings.has("renderer")) rendererMode = settings.at("renderer").string();
        }
        settingsFile.close();
#endif
        for (int i = 1; i < argc; ++i) {
            const std::string argument = argv[i];
            if (i + 1 == argc) throw std::invalid_argument("Missing value for " + argument);
            if (argument == "--renderer") { rendererMode = argv[++i]; continue; }
            if (argument == "--render" && output.empty() && !frames) { output = std::filesystem::absolute(argv[++i]); continue; }
            if (argument != "--frames" || frames || !output.empty()) throw std::invalid_argument("Usage: tiny3d_project [--render file.ppm | --frames count] [--renderer auto|d3d11|software]");
            const std::string count = argv[++i];
            if (count.empty() || count.find_first_not_of("0123456789") != std::string::npos) {
                throw std::invalid_argument("Frame count must be a positive integer");
            }
            const auto value = std::stoul(count);
            if (value == 0 || value > 1000000) throw std::invalid_argument("Frame count must be between 1 and 1000000");
            frames = static_cast<unsigned>(value);
        }
        if (rendererMode != "auto" && rendererMode != "d3d11" && rendererMode != "software")
            throw std::invalid_argument("Renderer must be auto, d3d11, or software");
        std::filesystem::path directory = TINY3D_PROJECT_DIRECTORY;
#ifdef TINY3D_EDITOR
        // Folder changes made in the editor are remembered without recompiling its binary.
        std::ifstream config(TINY3D_CONFIG_FILE);
        if (config) {
            const auto settings = tiny3d::json::parse(std::string((std::istreambuf_iterator<char>(config)), {}));
            config.close(); // Allow the editor to atomically replace settings after a folder switch.
            const auto selected = settings.at("project").string();
            directory = selected.empty() ? std::filesystem::path{} :
                std::filesystem::weakly_canonical(std::filesystem::path(TINY3D_CONFIG_FILE).parent_path() / selected);
        }
        tiny3d::Scene initial;
#ifndef TINY3D_SCENE_PROJECT
        if (directory == std::filesystem::path(TINY3D_PROJECT_DIRECTORY) &&
            !projectSettings(directory).has("startupScene")) {
            std::filesystem::current_path(directory);
            auto source = tiny3d::createGame();
            if (!source) throw std::runtime_error("createGame() returned null");
            initial = tiny3d::deserializeScene(tiny3d::serializeScene(source->scene()));
        }
#endif
        std::unique_ptr<tiny3d::Game> game = std::make_unique<tiny3d::editor::Editor>(std::move(initial),
            TINY3D_ENGINE_DIRECTORY, directory, TINY3D_CONFIG_FILE);
#else
        if (!directory.empty()) std::filesystem::current_path(directory);
#ifdef TINY3D_SCENE_PROJECT
        std::unique_ptr<tiny3d::Game> game = std::make_unique<SceneGame>(directory);
#else
        auto game = tiny3d::createGame();
#endif
        if (!game) throw std::runtime_error("createGame() returned null");
#endif
        std::cout << "Project: " << TINY3D_PROJECT_DIRECTORY << " | Mode: " << TINY3D_PROJECT_MODE << '\n';
        if (!output.empty()) {
            tiny3d::Scripts scripts;
            if (game->executeScripts()) scripts.awake(game->scene());
            const auto settings = game->screenSettings();
            const int width = settings.width ? settings.width : 800;
            const int height = settings.height ? settings.height : 500;
            tiny3d::Input input;
            input.renderWidth = width;
            input.renderHeight = height;
            input.viewport = {0, 0, width, height};
            game->update(0, input);
            if (game->executeScripts()) scripts.update(game->scene(), 0, input);
            std::unique_ptr<tiny3d::RenderBackend> renderer;
            if (rendererMode == "d3d11") {
#ifdef TINY3D_HAS_D3D11
                renderer = tiny3d::d3d11::createRenderer(nullptr, width, height);
#else
                throw std::runtime_error("The Direct3D 11 module is unavailable in this build");
#endif
            } else renderer = tiny3d::createSoftwareRenderer(nullptr, width, height);
            renderer->render(game->scene(), {28, 36, 52});
            saveFrame(output, *renderer);
            std::cout << "Saved " << output.string() << '\n';
            return 0;
        }
        tiny3d::graphics::BackendFactory factory;
        if (rendererMode != "software") {
#ifdef TINY3D_HAS_D3D11
            factory = [rendererMode](void* window, int w, int h) -> std::unique_ptr<tiny3d::RenderBackend> {
                try { return tiny3d::d3d11::createRenderer(window, w, h); }
                catch (const std::exception& error) {
                    if (rendererMode == "d3d11") throw;
                    std::cerr << error.what() << "; falling back to software rendering.\n";
                    return tiny3d::createSoftwareRenderer(window, w, h);
                }
            };
#else
            if (rendererMode == "d3d11") throw std::runtime_error("The Direct3D 11 module is unavailable in this build");
#endif
        }
        return tiny3d::run(*game, 800, 500, frames, std::move(factory));
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
