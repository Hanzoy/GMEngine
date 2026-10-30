#include "game_jobs.hpp"
#include "tasks.hpp"
#include <cstring>
namespace gm {
namespace {
constexpr size_t MaxBytes = 8 * 1024 * 1024;
struct Output {
    std::vector<uint8_t> bytes;
};
struct Item {
    uint64_t id;
    Task<Output> task;
};
void message(sdk::Error *e, const char *text) {
    if (e) {
        *e = {};
        std::strncpy(e->message, text, 511);
    }
}
class Group {
  public:
    Group() {
        items.reserve(64);
    }
    ~Group() {
        for (auto &item : items)
            item.task.cancel();
        for (auto &item : items)
            try {
                item.task.get();
            } catch (...) {
            }
    }
    void owner() const {
        if (thread != std::this_thread::get_id())
            throw std::runtime_error("Job group used from a different thread");
    }
    uint64_t submit(const char *name, sdk::JobWork work, const void *data, uint32_t size) {
        owner();
        if (!work || (!data && size) || size > MaxBytes || items.size() >= 64)
            throw std::runtime_error("Invalid job or job/input capacity exceeded");
        std::vector<uint8_t> input;
        if (size)
            input.assign(static_cast<const uint8_t *>(data), static_cast<const uint8_t *>(data) + size);
        auto task = backgroundTasks().submit(
            name ? name : "game.job", [work, input = std::move(input)](std::stop_token token) {
                Output out;
                sdk::Error e{};
                bool overflow = false;
                struct Sink {
                    Output *out;
                    bool *overflow;
                };
                Sink sink{&out, &overflow};
                sdk::ByteSink bytes{&sink, [](void *p, const void *d, uint32_t n) {
                                        auto &s = *static_cast<Sink *>(p);
                                        if ((!d && n) || s.out->bytes.size() + n > MaxBytes) {
                                            *s.overflow = true;
                                            return false;
                                        }
                                        if (n)
                                            s.out->bytes.insert(s.out->bytes.end(),
                                                                static_cast<const uint8_t *>(d),
                                                                static_cast<const uint8_t *>(d) + n);
                                        return true;
                                    }};
                sdk::JobControl control{
                    &token, [](void *p) { return static_cast<std::stop_token *>(p)->stop_requested(); }};
                bool ok = work(input.data(), uint32_t(input.size()), &control, &bytes, &e);
                e.message[511] = 0;
                checkCancellation(token);
                if (!ok || overflow)
                    throw std::runtime_error(overflow       ? "Job output exceeds 8 MiB"
                                             : e.message[0] ? e.message
                                                            : "Game job failed");
                return out;
            });
        const uint64_t id = next++;
        items.push_back({id, std::move(task)});
        return id;
    }
    bool cancel(uint64_t id) {
        owner();
        for (auto &item : items)
            if (item.id == id) {
                item.task.cancel();
                return true;
            }
        return false;
    }
    bool poll(sdk::JobResult &result) {
        owner();
        current = {};
        result = {};
        for (auto it = items.begin(); it != items.end(); ++it)
            if (it->task.ready()) {
                result.id = it->id;
                result.milliseconds = it->task.milliseconds();
                try {
                    current = it->task.get();
                    result.status = sdk::JobStatus::Succeeded;
                    result.data = current.bytes.data();
                    result.size = uint32_t(current.bytes.size());
                } catch (const TaskCancelled &) {
                    result.status = sdk::JobStatus::Cancelled;
                } catch (const std::exception &e) {
                    result.status = sdk::JobStatus::Failed;
                    std::strncpy(result.error, e.what(), 511);
                } catch (...) {
                    result.status = sdk::JobStatus::Failed;
                    std::strcpy(result.error, "Game job threw an unknown exception");
                }
                items.erase(it);
                return true;
            }
        return false;
    }

  private:
    uint64_t next = 1;
    std::thread::id thread = std::this_thread::get_id();
    std::vector<Item> items;
    Output current;
};
} // namespace
const sdk::JobService &jobService() {
    static const sdk::JobService service{
        sizeof(sdk::JobService),
        1,
        [](sdk::Error *e) -> void * {
            message(e, "");
            try {
                return new Group;
            } catch (const std::exception &x) {
                message(e, x.what());
                return nullptr;
            }
        },
        [](void *p) { delete static_cast<Group *>(p); },
        [](void *p, const char *n, sdk::JobWork f, const void *d, uint32_t s, sdk::Error *e) -> uint64_t {
            message(e, "");
            try {
                if (!p)
                    throw std::runtime_error("Missing job group");
                return static_cast<Group *>(p)->submit(n, f, d, s);
            } catch (const std::exception &x) {
                message(e, x.what());
                return 0;
            }
        },
        [](void *p, uint64_t id) {
            try {
                return p && static_cast<Group *>(p)->cancel(id);
            } catch (...) {
                return false;
            }
        },
        [](void *p, sdk::JobResult *r) {
            try {
                return p && r && static_cast<Group *>(p)->poll(*r);
            } catch (...) {
                return false;
            }
        }};
    return service;
}
} // namespace gm
