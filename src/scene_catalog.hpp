#pragma once
#include "scene.hpp"
#include <future>
namespace gm {
struct SceneEntry {
    std::string id, name, file;
};
void saveCatalog(const std::filesystem::path &path, const std::vector<SceneEntry> &entries);
std::vector<SceneEntry> loadCatalog(const std::filesystem::path &path);
// Only file I/O runs off-thread. Validation and World construction run on the caller.
class SceneLoader {
  public:
    explicit SceneLoader(std::filesystem::path catalog);
    void request(const std::string &id);
    bool pending() const {
        return pending_.valid();
    }
    std::unique_ptr<CompiledScene> poll();
    const auto &entries() const {
        return entries_;
    }

  private:
    std::filesystem::path root_;
    std::vector<SceneEntry> entries_;
    std::future<std::vector<uint8_t>> pending_;
};
} // namespace gm
