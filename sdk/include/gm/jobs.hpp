#pragma once
#include "extensions.hpp"
namespace gm::sdk {
enum class JobStatus : uint32_t { Succeeded, Failed, Cancelled };
struct JobControl {
    void *state;
    bool (*cancelled)(void *);
};
// Callback runs on a worker. Input is a copy. No Context/World/UI/module-owned mutable
// state may be borrowed. Return bytes through the bounded sink. Catch exceptions here.
using JobWork = bool (*)(const void *, uint32_t, const JobControl *, const ByteSink *, Error *);
struct JobResult {
    uint64_t id = 0;
    JobStatus status{};
    double milliseconds = 0;
    const void *data = nullptr;
    uint32_t size = 0;
    char error[512]{};
};
struct JobService {
    static constexpr TypeId Id = id("gm.Jobs");
    static constexpr uint32_t Version = 1;
    uint32_t structSize, version;
    void *(*open)(Error *);
    // Cancels and joins every callback. MUST close in System::destroy before DLL unload.
    // Workers must observe cancellation; no forcibly terminating foreign C++ code.
    void (*close)(void *);
    uint64_t (*submit)(void *, const char *name, JobWork, const void *, uint32_t, Error *);
    bool (*cancel)(void *, uint64_t);
    // Owner-thread only. Data lives until next poll/close; apply to ECS after polling.
    bool (*poll)(void *, JobResult *);
};
} // namespace gm::sdk
