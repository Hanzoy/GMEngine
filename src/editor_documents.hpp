#pragma once
#include "document.hpp"
#include <memory>
#include <stdexcept>
namespace gm {
// Each tab owns its document state and undo history. Merely changing tabs never saves.
class EditorDocuments {
public:
    explicit EditorDocuments(const std::filesystem::path& path) {open(path);}
    void open(const std::filesystem::path& path) {
        const auto full=std::filesystem::absolute(path).lexically_normal();
        for(size_t i=0;i<tabs_.size();++i) {std::error_code ec;const bool same=std::filesystem::equivalent(tabs_[i]->path(),full,ec);if(same || tabs_[i]->path()==full) {active_=i;return;}}
        if(tabs_.size()>=8) throw std::runtime_error("最多同时打开 8 个资源，请先关闭一个标签");
        auto next=std::make_unique<Document>(full);tabs_.push_back(std::move(next));active_=tabs_.size()-1;
    }
    void close(bool discard=false) {
        if(tabs_.size()==1) throw std::runtime_error("当前只有一个资源标签");
        if(current().dirty() && !discard) throw std::runtime_error("资源尚未保存");
        tabs_.erase(tabs_.begin()+active_);active_=std::min(active_,tabs_.size()-1);
    }
    Document& current() {return *tabs_[active_];}
    const Document& current() const {return *tabs_[active_];}
    const auto& tabs() const {return tabs_;}
    size_t active() const {return active_;}
    void activate(size_t index) {if(index>=tabs_.size()) throw std::runtime_error("Invalid tab index");active_=index;}
    Json execute(const Json& request) {return current().execute(request);}
    const Json& data() const {return current().data();}
    const std::string& selected() const {return current().selected();}
    const std::filesystem::path& path() const {return current().path();}
    uint64_t revision() const {return current().revision();}
    bool dirty() const {return current().dirty();}
private:
    std::vector<std::unique_ptr<Document>> tabs_;size_t active_=0;
};
}
