#include "tasks.hpp"
#include <algorithm>
namespace gm {
namespace {
thread_local bool taskWorker = false;
}
bool onTaskWorker() {
    return taskWorker;
}
const char *taskPhaseName(TaskPhase phase) {
    switch (phase) {
    case TaskPhase::Queued:
        return "queued";
    case TaskPhase::Running:
        return "running";
    case TaskPhase::Succeeded:
        return "succeeded";
    case TaskPhase::Failed:
        return "failed";
    case TaskPhase::Cancelled:
        return "cancelled";
    }
    return "unknown";
}
TaskPool::TaskPool(size_t workers, size_t capacity) : capacity_(capacity) {
    if (!workers) {
        auto cores = std::thread::hardware_concurrency();
        workers = std::clamp<size_t>(cores > 2 ? cores - 2 : 1, 1, 4);
    }
    if (workers > 32 || !capacity || capacity > 65536)
        throw std::invalid_argument("Invalid task pool limits");
    stats_.workers = workers;
    try {
        for (size_t i = 0; i < workers; ++i)
            workers_.emplace_back([this] { worker(); });
    } catch (...) {
        shutdown();
        throw;
    }
}
TaskPool::~TaskPool() {
    shutdown();
}
void TaskPool::enqueue(Item item) {
    {
        std::lock_guard lock(mutex_);
        if (stopping_ || queue_.size() >= capacity_) {
            ++stats_.rejected;
            throw std::runtime_error(stopping_ ? "Task pool is stopping" : "Background task queue is full");
        }
        queue_.push_back(std::move(item));
        ++stats_.submitted;
    }
    condition_.notify_one();
}
void TaskPool::worker() {
    taskWorker = true;
    for (;;) {
        Item item;
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, [&] { return stopping_ || !queue_.empty(); });
            if (stopping_ && queue_.empty())
                break;
            item = std::move(queue_.front());
            queue_.pop_front();
            active_.push_back(item.state);
            item.state->phase = TaskPhase::Running;
        }
        item.run();
        {
            std::lock_guard lock(mutex_);
            std::erase(active_, item.state);
            ++stats_.completed;
            if (item.state->phase == TaskPhase::Failed)
                ++stats_.failed;
            if (item.state->phase == TaskPhase::Cancelled)
                ++stats_.cancelled;
        }
    }
    taskWorker = false;
}
TaskPoolStats TaskPool::stats() const {
    std::lock_guard lock(mutex_);
    auto result = stats_;
    result.queued = queue_.size();
    result.active = active_.size();
    return result;
}
void TaskPool::shutdown() {
    std::call_once(shutdown_, [this] {
        std::deque<Item> pending;
        std::vector<std::shared_ptr<TaskState>> active;
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
            pending.swap(queue_);
            active = active_;
            stats_.completed += pending.size();
            stats_.cancelled += pending.size();
        }
        for (auto &item : pending)
            item.cancel();
        for (auto &item : active)
            item->cancellation.request_stop();
        condition_.notify_all();
        for (auto &worker : workers_)
            if (worker.joinable())
                worker.join();
    });
}
TaskPool &backgroundTasks() {
    static TaskPool pool;
    return pool;
}
} // namespace gm
