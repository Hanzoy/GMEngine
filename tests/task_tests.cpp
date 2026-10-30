#include "tasks.hpp"
#include <iostream>
using namespace std::chrono_literals;
namespace {
void check(bool value, const char *error) {
    if (!value)
        throw std::runtime_error(error);
}
template <class Fn> void rejects(Fn fn) {
    bool rejected = false;
    try {
        fn();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, "Expected error propagation");
}
void until(const std::function<bool()> &ready) {
    auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!ready()) {
        if (std::chrono::steady_clock::now() >= deadline)
            throw std::runtime_error("Task test timed out");
        std::this_thread::sleep_for(1ms);
    }
}
void hold(std::stop_token token, const std::atomic<bool> &release) {
    while (!release) {
        gm::checkCancellation(token);
        std::this_thread::sleep_for(1ms);
    }
}
} // namespace
int main() {
    try {
        {
            std::atomic<int> started{0};
            std::atomic<bool> release{false};
            gm::TaskPool pool(2, 8);
            auto run = [&](std::stop_token token) {
                ++started;
                hold(token, release);
                return std::this_thread::get_id();
            };
            auto a = pool.submit("a", run), b = pool.submit("b", run);
            until([&] { return started == 2; });
            check(pool.stats().active == 2, "Two tasks must run concurrently");
            release = true;
            check(a.get() != b.get(), "Tasks must use distinct workers");
            auto bad =
                pool.submit("error", [](std::stop_token) -> int { throw std::runtime_error("expected"); });
            rejects([&] { bad.get(); });
            auto recovered = pool.submit("recovery", [](std::stop_token) { return 42; });
            check(recovered.get() == 42, "Worker survives exceptions");
            pool.shutdown();
            auto stats = pool.stats();
            check(stats.completed == 4 && stats.failed == 1 && stats.active == 0,
                  "Completed task statistics");
            rejects([&] { pool.submit("closed", [](std::stop_token) {}); });
        }
        {
            std::atomic<bool> release{false}, started{false}, executed{false};
            gm::TaskPool pool(1, 1);
            auto active = pool.submit("hold", [&](std::stop_token token) {
                started = true;
                hold(token, release);
            });
            until([&] { return started.load(); });
            auto queued = pool.submit("queued", [&](std::stop_token) { executed = true; });
            rejects([&] { pool.submit("overflow", [](std::stop_token) {}); });
            queued.cancel();
            release = true;
            active.get();
            rejects([&] { queued.get(); });
            check(!executed && queued.phase() == gm::TaskPhase::Cancelled, "Queued cancellation skips work");
            pool.shutdown();
            check(pool.stats().rejected == 1 && pool.stats().cancelled == 1,
                  "Queue limits and cancellation stats");
        }
        {
            std::atomic<bool> release{false}, started{false};
            gm::TaskPool pool(1);
            auto abandoned = pool.submit("abandoned handle", [&](std::stop_token token) {
                started = true;
                hold(token, release);
            });
            until([&] { return started.load(); });
            abandoned = {}; // A future from std::async would block here.
            release = true;
            pool.shutdown();
        }
        {
            std::atomic<bool> started{false}, release{false};
            gm::TaskPool pool(1);
            auto active = pool.submit("shutdown", [&](std::stop_token token) {
                started = true;
                hold(token, release);
            });
            until([&] { return started.load(); });
            auto queued = pool.submit("queued", [](std::stop_token) { return 1; });
            pool.shutdown();
            rejects([&] { active.get(); });
            rejects([&] { queued.get(); });
            check(pool.stats().cancelled == 2, "Shutdown cancels running and queued tasks");
        }
        {
            gm::TaskPool pool(1);
            auto outer = pool.submit("outer", [&](std::stop_token) {
                auto inner = pool.submit("inner", [](std::stop_token) { return 7; });
                rejects([&] { inner.get(); });
                return 9;
            });
            check(outer.get() == 9, "Worker must reject nested blocking waits");
            pool.shutdown();
        }
        std::cout << "Concurrency, bounded queue, exception recovery, nonblocking handles, cancellation and "
                     "shutdown passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
