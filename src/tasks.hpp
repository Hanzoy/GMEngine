#pragma once
#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace gm {
class TaskCancelled : public std::runtime_error {
  public:
    TaskCancelled() : std::runtime_error("Background task cancelled") {}
};
inline void checkCancellation(std::stop_token token) {
    if (token.stop_requested())
        throw TaskCancelled();
}
enum class TaskPhase { Queued, Running, Succeeded, Failed, Cancelled };
const char *taskPhaseName(TaskPhase phase);
bool onTaskWorker();
struct TaskState {
    std::string name;
    std::stop_source cancellation;
    std::atomic<TaskPhase> phase{TaskPhase::Queued};
    std::atomic<double> milliseconds{0};
};
template <class T> class Task {
  public:
    Task() = default;
    Task(std::shared_ptr<TaskState> state, std::future<T> result)
        : state_(std::move(state)), result_(std::move(result)) {}
    Task(Task &&) = default;
    Task &operator=(Task &&) = default;
    bool valid() const {
        return result_.valid();
    }
    bool ready() const {
        return valid() && result_.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }
    void cancel() const {
        if (state_)
            state_->cancellation.request_stop();
    }
    TaskPhase phase() const {
        return state_ ? state_->phase.load() : TaskPhase::Cancelled;
    }
    double milliseconds() const {
        return state_ ? state_->milliseconds.load() : 0;
    }
    T get() {
        if (onTaskWorker() && !ready())
            throw std::runtime_error(
                "Task workers cannot wait on another task; collect results on the owner thread");
        return result_.get();
    }

  private:
    std::shared_ptr<TaskState> state_;
    std::future<T> result_;
};
struct TaskPoolStats {
    size_t workers = 0, queued = 0, active = 0;
    uint64_t submitted = 0, completed = 0, failed = 0, cancelled = 0, rejected = 0;
};
// Tasks own their inputs; no editor, World, registry or renderer objects may be borrowed.
// Cancellation is cooperative. Destroying a Task handle never waits for its work.
class TaskPool {
  public:
    explicit TaskPool(size_t workers = 0, size_t capacity = 64);
    ~TaskPool();
    TaskPool(const TaskPool &) = delete;
    TaskPool &operator=(const TaskPool &) = delete;
    template <class Fn> auto submit(std::string name, Fn &&function) {
        using R = std::invoke_result_t<std::decay_t<Fn> &, std::stop_token>;
        static_assert(!std::is_reference_v<R>, "Tasks must return owned results");
        auto state = std::make_shared<TaskState>();
        state->name = std::move(name);
        auto promise = std::make_shared<std::promise<R>>();
        Task<R> task(state, promise->get_future());
        auto fn = std::make_shared<std::decay_t<Fn>>(std::forward<Fn>(function));
        auto work = [state, promise, fn] {
            const auto started = std::chrono::steady_clock::now();
            auto finish = [&](TaskPhase phase) {
                state->milliseconds =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
                        .count();
                state->phase = phase;
            };
            try {
                const auto token = state->cancellation.get_token();
                checkCancellation(token);
                if constexpr (std::is_void_v<R>) {
                    (*fn)(token);
                    checkCancellation(token);
                    finish(TaskPhase::Succeeded);
                    promise->set_value();
                } else {
                    auto value = (*fn)(token);
                    checkCancellation(token);
                    finish(TaskPhase::Succeeded);
                    promise->set_value(std::move(value));
                }
            } catch (const TaskCancelled &) {
                finish(TaskPhase::Cancelled);
                promise->set_exception(std::current_exception());
            } catch (...) {
                finish(TaskPhase::Failed);
                promise->set_exception(std::current_exception());
            }
        };
        auto abandon = [state, promise] {
            state->cancellation.request_stop();
            state->phase = TaskPhase::Cancelled;
            promise->set_exception(std::make_exception_ptr(TaskCancelled()));
        };
        enqueue({state, std::move(work), std::move(abandon)});
        return task;
    }
    TaskPoolStats stats() const;
    void shutdown();

  private:
    struct Item {
        std::shared_ptr<TaskState> state;
        std::function<void()> run, cancel;
    };
    void enqueue(Item item);
    void worker();
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<Item> queue_;
    std::vector<std::shared_ptr<TaskState>> active_;
    std::vector<std::thread> workers_;
    std::once_flag shutdown_;
    TaskPoolStats stats_;
    size_t capacity_;
    bool stopping_ = false;
};
TaskPool &backgroundTasks();
} // namespace gm
