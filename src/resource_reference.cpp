#include "document.hpp"
namespace gm {
std::filesystem::path resolveResource(const std::filesystem::path &directory, const std::string &reference) {
    auto path = [](const std::string &s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); };
    if (!reference.starts_with("asset://"))
        return std::filesystem::absolute(directory / path(reference)).lexically_normal();
    auto id = reference.substr(8);
    if (id.empty() || id.size() > 80)
        throw std::runtime_error("Invalid asset reference");
    auto root = std::filesystem::absolute(directory);
    while (!std::filesystem::exists(root / "game.gmproject")) {
        auto parent = root.parent_path();
        if (parent == root || parent.empty())
            throw std::runtime_error("asset:// references require a game project");
        root = parent;
    }
    root = std::filesystem::canonical(root);
    auto valid = [&](const std::filesystem::path &candidate) {
        if (!std::filesystem::exists(candidate))
            return false;
        auto meta = candidate;
        meta += ".gmmeta";
        if (!std::filesystem::exists(meta) || readJson(meta).value("id", std::string()) != id)
            return false;
        auto relative = std::filesystem::canonical(candidate).lexically_relative(root);
        if (relative.empty() || *relative.begin() == "..")
            throw std::runtime_error("Asset reference escapes project root");
        return true;
    };
    auto index = root / ".gm/assets.json";
    if (std::filesystem::exists(index)) {
        auto catalog = readJson(index);
        for (const auto &asset : catalog.at("assets"))
            if (asset.at("id") == id) {
                auto candidate = root / path(asset.at("path").get<std::string>());
                if (valid(candidate))
                    return std::filesystem::canonical(candidate);
            }
    }
    // A checked-out project need not carry the generated index. Sidecars are authoritative.
    std::filesystem::path found;
    for (const auto &item : std::filesystem::recursive_directory_iterator(root / "assets"))
        if (item.is_regular_file() && item.path().extension() == ".gmmeta") {
            auto candidate = item.path();
            candidate.replace_extension();
            if (valid(candidate)) {
                if (!found.empty())
                    throw std::runtime_error("Duplicate asset ID");
                found = candidate;
            }
        }
    if (found.empty())
        throw std::runtime_error("Asset ID not found: " + id);
    return std::filesystem::canonical(found);
}
} // namespace gm
