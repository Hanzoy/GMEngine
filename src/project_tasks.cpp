#include "project_tasks.hpp"
#include "process.hpp"
namespace gm {
Task<Json> startProjectTask(const GameProject &project, const Json &request,
                            const std::filesystem::path &taskDirectory) {
    if (!project.valid())
        throw std::runtime_error("Open a .gmproject first");
    auto op = request.at("op").get<std::string>();
    if (op != "project.build" && op != "project.package" && op != "project.assets")
        throw std::invalid_argument("Unsupported background project operation");
    const auto executable = engineRoot() / "bin/gmctl.exe";
    std::filesystem::create_directories(taskDirectory);
    // Do not forward editor revisions/async flags to the child's independent document.
    writeJsonAtomic(taskDirectory / "request.json", {{"op", op}});
    return backgroundTasks().submit(op, [project, executable, taskDirectory](std::stop_token token) {
        auto reply = taskDirectory / "response.json";
        const auto code = runProcess({executable.wstring(), L"--project", project.manifest.wstring(),
                                      L"--request", (taskDirectory / "request.json").wstring()},
                                     project.root, reply, token);
        checkCancellation(token);
        if (std::filesystem::file_size(reply) > 16 * 1024 * 1024)
            throw std::runtime_error("Project task response exceeds limit");
        auto result = readJson(reply);
        if (!result.is_object() || !result.contains("ok") || (code && result.value("ok", false)))
            throw std::runtime_error("Invalid project task response; inspect task log");
        return result;
    });
}
} // namespace gm
