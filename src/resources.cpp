#include "resources.hpp"
#include "tasks.hpp"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
namespace gm {
namespace {
constexpr uint64_t FileLimit = 64 * 1024 * 1024, TotalLimit = 256 * 1024 * 1024;
std::atomic<uint64_t> reservedBytes{0};
struct Reservation {
    uint64_t size;
    ~Reservation() {
        reservedBytes.fetch_sub(size);
    }
};
struct Buffer {
    std::shared_ptr<Reservation> reservation;
    std::vector<uint8_t> bytes;
};
struct File {
    std::mutex mutex;
    Task<Buffer> task;
    Buffer buffer;
    std::string error;
    sdk::ResourceStatus status = sdk::ResourceStatus::Pending;
    ~File() {
        task.cancel();
    }
};
using Key = std::tuple<std::filesystem::path, std::filesystem::file_time_type, uint64_t>;
std::mutex cacheMutex;
std::map<Key, std::weak_ptr<File>> cache;
void check(bool ok, const char *s) {
    if (!ok)
        throw std::runtime_error(s);
}
std::filesystem::path path(const char *s) {
    check(s && *s, "Missing resource path");
    std::string t(s);
    return std::filesystem::path(std::u8string(t.begin(), t.end()));
}
struct Session {
    std::filesystem::path root;
    std::thread::id thread = std::this_thread::get_id();
    std::map<uint64_t, std::shared_ptr<File>> files;
    uint64_t next = 1;
    explicit Session(const char *r) : root(std::filesystem::canonical(path(r))) {
        check(std::filesystem::is_directory(root), "Resource root must be a directory");
    }
    void owner() {
        check(thread == std::this_thread::get_id(), "Resource session used from a different thread");
    }
    uint64_t request(const char *relative) {
        owner();
        check(files.size() < 64, "Resource handle limit reached");
        auto name = path(relative);
        check(!name.is_absolute() && !name.has_root_name(), "Resource path must be relative");
        auto full = std::filesystem::canonical(root / name), rel = full.lexically_relative(root);
        check(!rel.empty() && *rel.begin() != "..", "Resource path escapes root");
        uint64_t size = std::filesystem::file_size(full);
        check(size <= FileLimit, "Resource exceeds 64 MiB");
        Key key{full, std::filesystem::last_write_time(full), size};
        std::shared_ptr<File> file;
        std::lock_guard lock(cacheMutex);
        std::erase_if(cache, [](auto &p) { return p.second.expired(); });
        if (auto it = cache.find(key); it != cache.end())
            file = it->second.lock();
        if (!file) {
            check(reservedBytes.load() + size <= TotalLimit, "Shared resource byte budget exceeded");
            auto reservation = std::make_shared<Reservation>();
            reservation->size = 0;
            reservedBytes.fetch_add(size);
            reservation->size = size;
            file = std::make_shared<File>();
            file->task = backgroundTasks().submit("resource.read", [full, size, modified = std::get<1>(key),
                                                                    reservation](std::stop_token token) {
                Buffer result{reservation, std::vector<uint8_t>(size_t(size))};
                std::ifstream stream(full, std::ios::binary);
                check(bool(stream), "Cannot open resource");
                for (size_t offset = 0; offset < size;) {
                    checkCancellation(token);
                    auto n = std::min<size_t>(1024 * 1024, size - offset);
                    stream.read(reinterpret_cast<char *>(result.bytes.data() + offset), n);
                    check(bool(stream), "Resource changed or read failed");
                    offset += n;
                }
                check(stream.peek() == EOF && std::filesystem::last_write_time(full) == modified &&
                          std::filesystem::file_size(full) == size,
                      "Resource changed while loading");
                return result;
            });
            cache[key] = file;
        }
        auto id = next++;
        files.emplace(id, std::move(file));
        return id;
    }
    void poll(uint64_t id, sdk::ResourceData &out) {
        owner();
        auto it = files.find(id);
        check(it != files.end(), "Unknown resource handle");
        auto &file = *it->second;
        std::lock_guard lock(file.mutex);
        if (file.status == sdk::ResourceStatus::Pending && file.task.ready())
            try {
                file.buffer = file.task.get();
                file.status = sdk::ResourceStatus::Ready;
            } catch (const std::exception &e) {
                file.error = e.what();
                file.status = sdk::ResourceStatus::Failed;
            }
        out = {};
        out.status = file.status;
        out.data = file.buffer.bytes.data();
        out.size = uint32_t(file.buffer.bytes.size());
        std::strncpy(out.error, file.error.c_str(), 511);
    }
};
template <class F> bool guard(sdk::Error *e, F &&fn) {
    if (e)
        *e = {};
    try {
        fn();
        return true;
    } catch (const std::exception &x) {
        if (e)
            std::strncpy(e->message, x.what(), 511);
        return false;
    } catch (...) {
        return false;
    }
}
Session &get(void *p) {
    check(p, "Missing resource session");
    return *static_cast<Session *>(p);
}
} // namespace
const sdk::ResourceService &resourceService() {
    static const sdk::ResourceService api{sizeof(sdk::ResourceService),
                                          1,
                                          [](const char *r, sdk::Error *e) -> void * {
                                              void *p = nullptr;
                                              guard(e, [&] { p = new Session(r); });
                                              return p;
                                          },
                                          [](void *p) { delete static_cast<Session *>(p); },
                                          [](void *p, const char *r, sdk::Error *e) {
                                              uint64_t id = 0;
                                              guard(e, [&] { id = get(p).request(r); });
                                              return id;
                                          },
                                          [](void *p, uint64_t id, sdk::ResourceData *r, sdk::Error *e) {
                                              return guard(e, [&] {
                                                  check(r, "Missing resource output");
                                                  get(p).poll(id, *r);
                                              });
                                          },
                                          [](void *p, uint64_t id, sdk::Error *e) {
                                              return guard(e, [&] {
                                                  auto &s = get(p);
                                                  s.owner();
                                                  check(s.files.erase(id) != 0, "Unknown resource handle");
                                              });
                                          }};
    return api;
}
} // namespace gm
