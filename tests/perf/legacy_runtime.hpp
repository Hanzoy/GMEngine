#pragma once
#include "document.hpp"

namespace gm {
// A running instance owns its state. Button events never save the authoring asset.
class LegacyRuntimeDocument {
public:
    explicit LegacyRuntimeDocument(std::filesystem::path path);
    const Json& data() const { return data_; }
    Json activate(const std::string& buttonId);
    void reload();
private:
    std::filesystem::path path_;
    Json data_;
};
}
