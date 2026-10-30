#pragma once
#include <filesystem>
#include <stop_token>
#include <string>
#include <vector>
namespace gm {
// Runs without a shell. On Windows the entire child process tree belongs to a kill-on-close job.
int runProcess(const std::vector<std::wstring> &arguments, const std::filesystem::path &directory,
               const std::filesystem::path &output, std::stop_token cancellation = {}, bool append = false);
} // namespace gm
