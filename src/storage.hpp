#pragma once
#include <gm/extensions.hpp>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>
namespace gm {
std::filesystem::path userDataRoot();
class StorageSession {
  public:
    // A profile may be empty for the original engine save directory.
    StorageSession(const std::string &app, const std::string &profile = "");
    ~StorageSession();
    StorageSession(const StorageSession &) = delete;
    std::vector<uint8_t> read(const std::string &slot) const;
    void write(const std::string &slot, std::span<const uint8_t> bytes);
    bool exists(const std::string &slot) const;
    std::filesystem::path path(const std::string &slot) const;

  private:
    std::filesystem::path root_;
    std::shared_ptr<void> lease_;
};
const sdk::HostServices &hostServices();
} // namespace gm
