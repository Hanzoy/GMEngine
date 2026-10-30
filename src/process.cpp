#include "process.hpp"
#include "tasks.hpp"
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
namespace gm {
#ifdef _WIN32
namespace {
struct Handle {
    HANDLE value = nullptr;
    explicit Handle(HANDLE v = nullptr) : value(v) {}
    ~Handle() {
        if (value && value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
    }
    Handle(const Handle &) = delete;
};
std::wstring quote(const std::wstring &value) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (auto c : value) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        result.append(slashes * (c == L'"' ? 2 : 1), L'\\');
        slashes = 0;
        if (c == L'"')
            result += L'\\';
        result += c;
    }
    result.append(slashes * 2, L'\\');
    return result + L'"';
}
[[noreturn]] void processError(const char *operation) {
    throw std::runtime_error(std::string(operation) + " (Windows error " + std::to_string(GetLastError()) +
                             ")");
}
} // namespace
#endif
int runProcess(const std::vector<std::wstring> &arguments, const std::filesystem::path &directory,
               const std::filesystem::path &output, std::stop_token cancellation, bool append) {
    checkCancellation(cancellation);
#ifdef _WIN32
    if (arguments.empty())
        throw std::invalid_argument("Missing process executable");
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    Handle log(CreateFileW(output.c_str(), append ? FILE_APPEND_DATA : GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                           append ? OPEN_ALWAYS : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (log.value == INVALID_HANDLE_VALUE)
        processError("Cannot open process output");
    Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (input.value == INVALID_HANDLE_VALUE)
        processError("Cannot open process input");
    Handle job(CreateJobObjectW(nullptr, nullptr));
    if (!job.value)
        processError("Cannot create process job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
        processError("Cannot configure process job");
    // Explicit inheritance prevents unrelated editor/file handles leaking into build tools.
    SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    std::vector<unsigned char> storage(bytes);
    auto *attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &bytes))
        processError("Cannot create process attributes");
    struct Attributes {
        LPPROC_THREAD_ATTRIBUTE_LIST value;
        ~Attributes() {
            DeleteProcThreadAttributeList(value);
        }
    } cleanup{attributes};
    HANDLE inherited[]{log.value, input.value};
    if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited,
                                   sizeof(inherited), nullptr, nullptr))
        processError("Cannot set process handles");
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdOutput = startup.StartupInfo.hStdError = log.value;
    startup.StartupInfo.hStdInput = input.value;
    startup.lpAttributeList = attributes;
    std::wstring command;
    for (const auto &argument : arguments) {
        if (!command.empty())
            command += L' ';
        command += quote(argument);
    }
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, nullptr,
                        directory.c_str(), &startup.StartupInfo, &info))
        processError("Cannot start build tool");
    Handle process(info.hProcess), thread(info.hThread);
    if (!AssignProcessToJobObject(job.value, process.value)) {
        auto error = GetLastError();
        TerminateProcess(process.value, 1);
        WaitForSingleObject(process.value, INFINITE);
        SetLastError(error);
        processError("Cannot contain build process");
    }
    if (ResumeThread(thread.value) == DWORD(-1))
        processError("Cannot resume build process");
    for (;;) {
        if (cancellation.stop_requested()) {
            TerminateJobObject(job.value, 1);
            WaitForSingleObject(process.value, INFINITE);
            throw TaskCancelled();
        }
        const auto wait = WaitForSingleObject(process.value, 50);
        if (wait == WAIT_OBJECT_0)
            break;
        if (wait != WAIT_TIMEOUT)
            processError("Cannot wait for build process");
    }
    DWORD code = 1;
    if (!GetExitCodeProcess(process.value, &code))
        processError("Cannot read process exit code");
    return static_cast<int>(code);
#else
    (void)arguments;
    (void)directory;
    (void)output;
    (void)append;
    throw std::runtime_error("Background build processes currently support Windows");
#endif
}
} // namespace gm
