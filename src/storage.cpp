#include "storage.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif
namespace gm {
namespace {
constexpr size_t Limit = 256ull * 1024 * 1024;
void safeName(const std::string &name) {
    if (name.empty() || name.size() > 128 || name == "." || name == ".." ||
        !std::all_of(name.begin(), name.end(), [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
                   c == '-' || c == '.';
        }))
        throw std::runtime_error("Invalid storage name; use ASCII letters, digits, '.', '_' or '-'");
}
void error(sdk::Error *out, const char *message) {
    if (out) {
        std::strncpy(out->message, message, sizeof(out->message) - 1);
        out->message[sizeof(out->message) - 1] = 0;
    }
}
} // namespace
std::filesystem::path userDataRoot() {
#ifdef _WIN32
    auto env = [](const wchar_t *name) {
        wchar_t path[32768]{};
        auto n = GetEnvironmentVariableW(name, path, 32768);
        return n && n < 32768 ? std::filesystem::path(path) : std::filesystem::path{};
    };
    if (auto path = env(L"GM_ENGINE_USER_DATA"); !path.empty())
        return std::filesystem::absolute(path).lexically_normal();
    if (auto path = env(L"LOCALAPPDATA"); !path.empty())
        return path / "GMEngine";
#else
    if (auto *overridePath = std::getenv("GM_ENGINE_USER_DATA"))
        return std::filesystem::absolute(overridePath).lexically_normal();
    if (auto *home = std::getenv("HOME"))
        return std::filesystem::path(home) / ".local/share/GMEngine";
#endif
    throw std::runtime_error("Cannot locate per-user engine data directory");
}
StorageSession::StorageSession(const std::string &app, const std::string &profile) {
    safeName(app);
    if (!profile.empty())
        safeName(profile);
    root_ = userDataRoot() / "saves" / app;
    if (!profile.empty())
        root_ /= profile;
    std::filesystem::create_directories(root_);
    auto lock = root_ / ".writer.lock";
#ifdef _WIN32
    auto handle = CreateFileW(lock.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Save profile is already in use or inaccessible");
    lease_ = std::shared_ptr<void>(handle, [](void *p) { CloseHandle(p); });
#else
    int fd = open(lock.c_str(), O_CREAT | O_RDWR, 0600);
    if (fd < 0)
        throw std::runtime_error("Cannot open save profile lock");
    if (flock(fd, LOCK_EX | LOCK_NB)) {
        close(fd);
        throw std::runtime_error("Save profile is already in use");
    }
    lease_ = std::shared_ptr<void>(new int(fd), [](void *p) {
        auto n = static_cast<int *>(p);
        close(*n);
        delete n;
    });
#endif
}
StorageSession::~StorageSession() = default;
std::filesystem::path StorageSession::path(const std::string &slot) const {
    safeName(slot);
    if (slot.starts_with('.'))
        throw std::runtime_error("Reserved storage slot");
    return root_ / slot;
}
bool StorageSession::exists(const std::string &slot) const {
    return std::filesystem::is_regular_file(path(slot));
}
std::vector<uint8_t> StorageSession::read(const std::string &slot) const {
    std::ifstream in(path(slot), std::ios::binary | std::ios::ate);
    if (!in)
        throw std::runtime_error("Save slot does not exist or cannot be read");
    auto size = in.tellg();
    if (size < 0 || uint64_t(size) > Limit)
        throw std::runtime_error("Save exceeds size limit");
    std::vector<uint8_t> result(static_cast<size_t>(size));
    in.seekg(0);
    in.read(reinterpret_cast<char *>(result.data()), size);
    if (!in)
        throw std::runtime_error("Save read failed");
    return result;
}
void StorageSession::write(const std::string &slot, std::span<const uint8_t> bytes) {
    if (bytes.size() > Limit)
        throw std::runtime_error("Save exceeds size limit");
    auto target = path(slot);
    static std::atomic<uint64_t> sequence{0};
    auto temporary = target;
    temporary += "." + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                 std::to_string(++sequence) + ".tmp";
    try {
#ifdef _WIN32
        auto file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            throw std::runtime_error("Cannot create save temporary file");
        DWORD written = 0;
        bool good = WriteFile(file, bytes.data(), DWORD(bytes.size()), &written, nullptr) &&
                    written == bytes.size() && FlushFileBuffers(file);
        CloseHandle(file);
        if (!good)
            throw std::runtime_error("Cannot flush save temporary file");
#else
        std::ofstream file(temporary, std::ios::binary);
        file.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        file.flush();
        if (!file)
            throw std::runtime_error("Cannot write save temporary file");
        file.close();
#endif
        // Keep the previous complete save. Failure to create a backup leaves the primary untouched.
        if (std::filesystem::exists(target)) {
            auto backup = target;
            backup += ".bak";
            auto pendingBackup = temporary;
            pendingBackup += ".bak";
            try {
                std::filesystem::copy_file(target, pendingBackup);
#ifdef _WIN32
                if (!MoveFileExW(pendingBackup.c_str(), backup.c_str(),
                                 MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                    throw std::runtime_error("Cannot replace save backup");
#else
                std::filesystem::rename(pendingBackup, backup);
#endif
            } catch (...) {
                std::error_code ec;
                std::filesystem::remove(pendingBackup, ec);
                throw;
            }
        }
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), target.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot replace save; previous file retained");
#else
        std::filesystem::rename(temporary, target);
#endif
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove(temporary, ec);
        throw;
    }
}
const sdk::HostServices &hostServices() {
    static const sdk::StorageService storage{
        sizeof(sdk::StorageService),
        1,
        [](const char *app, const char *profile, sdk::Error *e) -> void * {
            try {
                if (!app)
                    throw std::runtime_error("Missing storage app");
                return new StorageSession(app, profile ? profile : "");
            } catch (const std::exception &x) {
                error(e, x.what());
                return nullptr;
            }
        },
        [](void *ptr) { delete static_cast<StorageSession *>(ptr); },
        [](void *ptr, const char *slot, const sdk::ByteSink *sink, sdk::Error *e) {
            try {
                if (!ptr || !slot || !sink || !sink->write)
                    throw std::runtime_error("Invalid storage read");
                auto bytes = static_cast<StorageSession *>(ptr)->read(slot);
                if (!sink->write(sink->user, bytes.data(), uint32_t(bytes.size())))
                    throw std::runtime_error("Save consumer rejected data");
                return true;
            } catch (const std::exception &x) {
                error(e, x.what());
                return false;
            }
        },
        [](void *ptr, const char *slot, const void *data, uint32_t size, sdk::Error *e) {
            try {
                if (!ptr || !slot || (!data && size))
                    throw std::runtime_error("Invalid storage write");
                static_cast<StorageSession *>(ptr)->write(slot, {static_cast<const uint8_t *>(data), size});
                return true;
            } catch (const std::exception &x) {
                error(e, x.what());
                return false;
            }
        },
        [](void *ptr, const char *slot) {
            try {
                return ptr && slot && static_cast<StorageSession *>(ptr)->exists(slot);
            } catch (...) {
                return false;
            }
        }};
    static const sdk::HostServices host{
        sizeof(sdk::HostServices), 1, [](sdk::TypeId id, uint32_t version) -> const void * {
            return id == sdk::StorageService::Id && version == 1 ? &storage : nullptr;
        }};
    return host;
}
} // namespace gm
