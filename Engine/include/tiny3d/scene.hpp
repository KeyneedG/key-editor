#pragma once
#include "tiny3d/engine.hpp"
#include <filesystem>
#include <string_view>

namespace tiny3d {
// Built-in components, mesh data, and hierarchy links. Runtime callbacks are not assets.
std::string serializeScene(const Scene& scene);
Scene deserializeScene(std::string_view json);
void saveScene(const Scene& scene, const std::filesystem::path& path);
Scene loadScene(const std::filesystem::path& path);
} // namespace tiny3d
