#include "scene_catalog.hpp"
#include <fstream>
#include <chrono>
#include <set>
#include <algorithm>
namespace gm {
namespace {
void check(bool ok, const char *error) {
    if (!ok)
        throw std::runtime_error(error);
}
void word(std::ostream &out, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        out.put(char(v >> (i * 8)));
}
uint32_t word(std::istream &in) {
    uint32_t result = 0;
    for (int i = 0; i < 4; ++i) {
        int b = in.get();
        check(b != EOF, "Truncated scene catalog");
        result |= uint32_t(b) << (i * 8);
    }
    return result;
}
void string(std::ostream &out, const std::string &s) {
    check(s.size() <= 512, "Catalog string exceeds limit");
    word(out, uint32_t(s.size()));
    out.write(s.data(), s.size());
}
std::string string(std::istream &in) {
    auto n = word(in);
    check(n > 0 && n <= 512, "Invalid catalog string");
    std::string s(n, '\0');
    in.read(s.data(), n);
    check(bool(in) && s.find('\0') == std::string::npos, "Truncated catalog string");
    return s;
}
} // namespace
void saveCatalog(const std::filesystem::path &path, const std::vector<SceneEntry> &entries) {
    check(entries.size() <= 4096, "Too many catalog scenes");
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write("GMCAT001", 8);
    word(out, uint32_t(entries.size()));
    for (const auto &entry : entries) {
        string(out, entry.id);
        string(out, entry.name);
        string(out, entry.file);
    }
    out.flush();
    check(bool(out), "Cannot write scene catalog");
}
std::vector<SceneEntry> loadCatalog(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    char header[8]{};
    in.read(header, 8);
    check(std::string(header, 8) == "GMCAT001", "Invalid scene catalog");
    auto count = word(in);
    check(count <= 4096, "Too many catalog scenes");
    std::vector<SceneEntry> entries;
    std::set<std::string> ids;
    for (uint32_t i = 0; i < count; ++i) {
        SceneEntry e{string(in), string(in), string(in)};
        auto file = std::filesystem::path(std::u8string(e.file.begin(), e.file.end()));
        check(!file.is_absolute() && !file.empty() && file.lexically_normal().begin()->string() != ".." &&
                  (file.extension() == ".gmscene" || file.extension()==".gmprefab"),
              "Unsafe catalog file path");
        check(ids.insert(e.id).second, "Duplicate catalog ID");
        entries.push_back(std::move(e));
    }
    check(in.peek() == EOF, "Trailing catalog data");
    return entries;
}
SceneLoader::SceneLoader(std::filesystem::path catalog)
    : root_(std::filesystem::canonical(catalog).parent_path()), entries_(loadCatalog(catalog)) {}
void SceneLoader::request(const std::string &id) {
    if (pending())
        throw std::runtime_error("A scene load is already pending");
    auto entry = std::find_if(entries_.begin(), entries_.end(),
                              [&](const SceneEntry &e) { return e.id == id || e.name == id; });
    if (entry == entries_.end())
        throw std::runtime_error("Scene ID/name is not in the package catalog");
    auto path = std::filesystem::canonical(
        root_ / std::filesystem::path(std::u8string(entry->file.begin(), entry->file.end())));
    auto relative = path.lexically_relative(root_);
    if (relative.empty() || *relative.begin() == "..")
        throw std::runtime_error("Scene path escapes catalog root");
    pending_ = backgroundTasks().submit("scene.read", [path](std::stop_token token) {
        std::ifstream in(path, std::ios::binary | std::ios::ate);
        check(bool(in), "Cannot open scene");
        auto size = in.tellg();
        check(size >= 0 && size <= 192 * 1024 * 1024 + 12, "Invalid scene file size");
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        in.seekg(0);
        for (size_t offset = 0; offset < bytes.size();) {
            checkCancellation(token);
            auto count = std::min<size_t>(1024 * 1024, bytes.size() - offset);
            in.read(reinterpret_cast<char *>(bytes.data() + offset), count);
            check(bool(in), "Scene read failed");
            offset += count;
        }
        return bytes;
    });
}
std::unique_ptr<CompiledScene> SceneLoader::poll() {
    if (!pending_.ready())
        return {};
    auto bytes = pending_.get();
    return std::make_unique<CompiledScene>(decodeScene(bytes));
}
} // namespace gm
