#pragma once
#include <filesystem>
#include <string>

namespace gm {
// Paths stay native for filesystem calls. JSON, logs and UI strings use UTF-8,
// independent of the Windows system code page.
inline std::string pathText(const std::filesystem::path& path) {
    const auto text=path.generic_u8string();
    return {text.begin(),text.end()};
}
}
