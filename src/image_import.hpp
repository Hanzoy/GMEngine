#pragma once
#include "image_types.hpp"
#include <filesystem>
namespace gm {
// Authoring/compiler only. The Player loads predecoded pixels from .gmui.
UiImage importImage(const std::filesystem::path& path);
ImageFit parseImageFit(const std::string& value);
}
