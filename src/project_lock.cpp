#include "project.hpp"
#include <algorithm>
#include <cwctype>
#include <map>
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
std::shared_ptr<void> lockProjectOperation(const GameProject &project) {
    if (!project.valid())
        throw std::runtime_error("Open a game project first");
    auto root = std::filesystem::canonical(project.root);
    auto key = root.wstring();
#ifdef _WIN32
    std::transform(key.begin(), key.end(), key.begin(), [](wchar_t c) { return std::towlower(c); });
#endif
    thread_local std::map<std::wstring, std::weak_ptr<void>> locks;
    std::erase_if(locks, [](const auto &item) { return item.second.expired(); });
    if (auto found = locks.find(key); found != locks.end())
        if (auto owned = found->second.lock())
            return owned;
    std::filesystem::create_directories(root / ".gm");
    auto path = root / ".gm/project-operation.lock";
#ifdef _WIN32
    auto handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Project is busy or its build lock is inaccessible; retry after the other "
                                 "build/import/package finishes");
    std::shared_ptr<void> owner(handle, [](void *value) { CloseHandle(value); });
#else
    int fd = open(path.c_str(), O_CREAT | O_RDWR, 0600);
    if (fd < 0)
        throw std::runtime_error("Cannot open project build lock");
    if (flock(fd, LOCK_EX | LOCK_NB)) {
        close(fd);
        throw std::runtime_error("Project is busy");
    }
    std::shared_ptr<void> owner(new int(fd), [](void *value) {
        auto p = static_cast<int *>(value);
        close(*p);
        delete p;
    });
#endif
    locks[key] = owner;
    return owner;
}
} // namespace gm
