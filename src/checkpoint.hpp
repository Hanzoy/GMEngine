#pragma once
#include "scene.hpp"
namespace gm {
struct SavedSystem {
    std::string name;
    uint32_t version = 0;
    std::vector<uint8_t> bytes;
};
struct SavedEvent {
    sdk::Event event{};
    std::string source, target;
};
struct RuntimeCheckpoint {
    CompiledScene scene;
    uint64_t tick = 0;
    uint64_t runtimeSequence = 1;
    double remainder = 0, dropped = 0;
    std::vector<SavedSystem> systems;
    std::vector<SavedEvent> events;
    std::vector<std::pair<std::string, std::string>> contacts;
};
std::vector<uint8_t> encodeCheckpoint(const RuntimeCheckpoint &);
RuntimeCheckpoint decodeCheckpoint(std::span<const uint8_t>);
} // namespace gm
