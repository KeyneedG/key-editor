#pragma once

#include "tiny3d/engine.hpp"

namespace tiny3d {
// Implement this factory in your project's source files. The engine supplies main().
std::unique_ptr<Game> createGame();
} // namespace tiny3d
