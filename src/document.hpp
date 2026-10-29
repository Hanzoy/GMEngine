#pragma once
#include "ui_types.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <string>
#include <vector>

namespace gm {
using Json = nlohmann::ordered_json;
Json readJson(const std::filesystem::path& path);
std::filesystem::path resolveResource(const std::filesystem::path& directory,const std::string& reference);
void writeJsonAtomic(const std::filesystem::path& path, const Json& data);
Json* findNode(Json& node, const std::string& id);
const Json* findNode(const Json& node, const std::string& id);
void validateDocument(const Json& document);
Json commandSchema();

class Document {
public:
    explicit Document(std::filesystem::path path);
    Json execute(const Json& request);
    const Json& data() const { return data_; }
    const std::string& selected() const { return selected_; }
    const std::filesystem::path& path() const { return path_; }
    bool dirty() const { return data_ != saved_; }
    uint64_t revision() const { return revision_; }
    const std::vector<std::string>& history() const { return log_; }
    void resetHistory() {undo_.clear();redo_.clear();++revision_;}
private:
    struct Snapshot { Json data; std::string selected; bool patch=false; };
    Json applyEdit(const Json& request);
    Json state() const;
    void ensureSelection();
    void record(const std::string& text);
    std::filesystem::path path_;
    Json data_, saved_;
    std::string selected_;
    uint64_t revision_ = 0;
    std::vector<Snapshot> undo_, redo_;
    std::vector<std::string> log_;
};
}
