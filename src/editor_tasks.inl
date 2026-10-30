gm::Task<Json> projectTask_;
std::string projectTaskId_, projectTaskOp_;
Json lastProjectTask_ = nullptr;
std::filesystem::path deferredProjectRequest_;
std::chrono::steady_clock::time_point projectTaskStart_, projectTaskRedraw_;
bool cancellingProjectTask_ = false;
bool projectBusy() const {
    return projectTask_.valid();
}
std::string projectTaskLabel() const {
    return projectTaskOp_ == "project.build"     ? "构建项目"
           : projectTaskOp_ == "project.package" ? "生成运行包"
                                                 : "导入资源";
}
std::string projectTaskText() const {
    auto seconds =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - projectTaskStart_)
            .count();
    return projectTaskLabel() + (cancellingProjectTask_ ? " · 正在取消" : " · 后台执行中") + " · " +
           std::to_string(seconds) + " s";
}
Json projectTaskStatus() const {
    if (!projectBusy())
        return lastProjectTask_;
    return {{"id", projectTaskId_},
            {"op", projectTaskOp_},
            {"active", true},
            {"state", cancellingProjectTask_ ? "cancelling" : gm::taskPhaseName(projectTask_.phase())},
            {"elapsedMs",
             std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - projectTaskStart_)
                 .count()}};
}
bool allowedDuringProjectTask(const std::string &op) const {
    return op == "query" || op == "describe" || op == "select" || op == "system.list" || op == "menu" ||
           op == "tab.select" || op.starts_with("view.") || op.starts_with("tree.") ||
           op.starts_with("group.") || op.starts_with("files.") || op.starts_with("output.") ||
           op == "bottom.tab" || op == "layout.reset" || op == "project.query" || op == "project.sources" ||
           op == "project.task.status" || op == "project.task.cancel";
}
Json beginProjectTask(const Json &request) {
    (void)request.value("async", false); // Validate before starting work or deferring a live response.
    if (!commit())
        throw std::runtime_error("请先完成当前属性编辑。");
    if (playing() || !allSaved())
        throw std::runtime_error("请先停止运行并保存所有资源。");
    projectTaskOp_ = request.at("op").get<std::string>();
    projectTaskId_ = std::to_string(GetCurrentProcessId()) + "-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    projectTask_ = gm::startProjectTask(gameProject_, request, session_ / "tasks" / projectTaskId_);
    projectTaskStart_ = projectTaskRedraw_ = std::chrono::steady_clock::now();
    cancellingProjectTask_ = false;
    error_ = false;
    inspectorPage_ = "project";
    assetInspection_ = false;
    message_ = projectTaskText();
    logs_.push_back(message_);
    return {{"ok", true}, {"pending", true}, {"task", projectTaskId_}};
}
void pollProjectTask(bool closing = false) {
    if (!projectBusy())
        return;
    auto now = std::chrono::steady_clock::now();
    if (now - projectTaskRedraw_ >= std::chrono::seconds(1)) {
        projectTaskRedraw_ = now;
        requestFrame();
    }
    if (!closing && !projectTask_.ready())
        return;
    Json response;
    std::string phase = "succeeded";
    try {
        response = projectTask_.get();
        if (closing)
            throw gm::TaskCancelled();
        if (!response.value("ok", false))
            throw std::runtime_error(response.value("error", "Project task failed"));
        if (projectTaskOp_ != "project.assets") {
            auto operation = gm::lockProjectOperation(gameProject_);
            auto previous = gm::gameRegistry();
            auto next = gm::loadProjectModule(gameProject_);
            gm::useGameRegistry(next);
            try {
                for (const auto &tab : doc_.tabs())
                    gm::validateDocument(tab->data());
            } catch (...) {
                gm::useGameRegistry(previous);
                throw;
            }
            for (const auto &tab : doc_.tabs())
                tab->resetHistory();
        }
        message_ = projectTaskLabel() + "完成";
        logs_.push_back(message_);
        error_ = false;
        images_->clear();
        refreshFiles();
    } catch (const gm::TaskCancelled &) {
        phase = "cancelled";
        response = {{"ok", false}, {"cancelled", true}, {"error", "Project task cancelled"}};
        message_ = projectTaskLabel() + "已取消";
        logs_.push_back(message_);
        error_ = false;
    } catch (const std::exception &e) {
        phase = "failed";
        response = {{"ok", false}, {"error", e.what()}};
        fail(e.what());
    }
    response["task"] = projectTaskId_;
    lastProjectTask_ = {
        {"id", projectTaskId_},
        {"op", projectTaskOp_},
        {"active", false},
        {"state", phase},
        {"elapsedMs", std::chrono::duration<double, std::milli>(now - projectTaskStart_).count()},
        {"response", response}};
    projectTask_ = {};
    if (!deferredProjectRequest_.empty()) {
        try {
            gm::writeJsonAtomic(session_ / "outbox" / deferredProjectRequest_.filename(), response);
            std::filesystem::remove(deferredProjectRequest_);
        } catch (const std::exception &e) {
            fail(e.what());
        }
        deferredProjectRequest_.clear();
    }
    requestFrame();
}
