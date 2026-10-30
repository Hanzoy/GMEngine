#pragma once
#include "project.hpp"
#include "tasks.hpp"
namespace gm {
Task<Json> startProjectTask(const GameProject &project, const Json &request,
                            const std::filesystem::path &taskDirectory);
}
