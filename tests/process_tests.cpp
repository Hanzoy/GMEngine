#include "process.hpp"
#include "project.hpp"
#include "tasks.hpp"
#include <fstream>
#include <iostream>
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
using namespace std::chrono_literals;
void check(bool ok, const char *error) {
    if (!ok)
        throw std::runtime_error(error);
}
int main(int argc, char **argv) {
    try {
        wchar_t filename[32768]{};
        GetModuleFileNameW(nullptr, filename, 32768);
        if (argc > 1 && std::string(argv[1]) == "--hold") {
            std::cout << GetCurrentProcessId() << std::endl;
            for (;;)
                Sleep(100);
        }
        if (argc > 1 && std::string(argv[1]) == "--tree")
            return gm::runProcess({filename, L"--hold"}, std::filesystem::current_path(), "child.log");
        auto temp = std::filesystem::temp_directory_path() /
                    ("gm-process-test-" + std::to_string(GetCurrentProcessId()));
        std::filesystem::create_directories(temp);
        gm::GameProject project;
        project.manifest = temp / "game.gmproject";
        project.root = temp;
        auto guard = gm::lockProjectOperation(project), nested = gm::lockProjectOperation(project);
        gm::TaskPool pool(1);
        auto other = pool.submit("lock", [project](std::stop_token) {
            try {
                auto rejected = gm::lockProjectOperation(project);
                return false;
            } catch (...) {
                return true;
            }
        });
        check(other.get(), "Concurrent project writers must be rejected");
        guard.reset();
        nested.reset();
        {
            auto recovered = gm::lockProjectOperation(project);
        }
        auto process =
            pool.submit("tree", [temp, executable = std::wstring(filename)](std::stop_token token) {
                return gm::runProcess({executable, L"--tree"}, temp, temp / "parent.log", token);
            });
        DWORD child = 0;
        auto deadline = std::chrono::steady_clock::now() + 10s;
        while (!child && std::chrono::steady_clock::now() < deadline) {
            std::ifstream in(temp / "child.log");
            in >> child;
            std::this_thread::sleep_for(10ms);
        }
        check(child != 0, "Child process did not start");
        auto handle = OpenProcess(SYNCHRONIZE, FALSE, child);
        check(handle != nullptr, "Cannot observe child");
        process.cancel();
        bool cancelled = false;
        try {
            process.get();
        } catch (const gm::TaskCancelled &) {
            cancelled = true;
        }
        const auto stopped = WaitForSingleObject(handle, 5000);
        CloseHandle(handle);
        check(cancelled && stopped == WAIT_OBJECT_0, "Cancellation must terminate the entire process tree");
        pool.shutdown();
        std::filesystem::remove_all(temp);
        std::cout << "Nested process tree cancellation and project writer exclusion passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
