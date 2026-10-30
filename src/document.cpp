#include "document.hpp"
#include "path_text.hpp"
#include "scene_authoring.hpp"
#include <fstream>
#include <set>
#include <functional>
#include <map>
#include <chrono>
#include <cmath>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace gm {
namespace {
void require(bool good, const std::string& message) { if (!good) throw std::runtime_error(message); }
bool validId(const std::string& s) {
    if (s.empty() || s.size() > 80) return false;
    for (unsigned char c : s) if (!(std::isalnum(c) || c == '_' || c == '-')) return false;
    return true;
}
bool validColor(const std::string& s) {
    if (s.size() != 7 || s.front() != '#') return false;
    for (size_t i=1; i<s.size(); ++i) if (!std::isxdigit(static_cast<unsigned char>(s[i]))) return false;
    return true;
}
const std::set<std::string> kinds = {"panel","label","button","image","list","tree","inspector","viewport","history","status","menubar","toolbar","files","output"};
void validateNode(const Json& node, std::set<std::string>& ids, int depth, int& count) {
    require(node.is_object(), "Node must be an object");
    require(depth <= 16 && ++count <= 512, "Document exceeds 16 levels / 512 nodes");
    const auto id = node.at("id").get<std::string>();
    require(validId(id) && ids.insert(id).second, "Invalid or duplicate node id: " + id);
    require(kinds.count(node.at("type").get<std::string>()) != 0, "Unknown node type");
    for (auto key : {"x","y","w","h"}) {
        require(node.contains(key) && node[key].is_number(), std::string("Expected numeric ") + key);
        const double v = node[key].get<double>();
        require(std::isfinite(v) && std::abs(v) <= 16384, "Geometry out of range");
        if (key[0]=='w' || key[0]=='h') require(v >= 1, "Size must be positive");
    }
    for (auto key : {"color","textColor","tint"}) if (node.contains(key)) require(node[key].is_string() && validColor(node[key].get<std::string>()), "Color must be #RRGGBB");
    if(node.contains("source")) require(node["source"].is_string() && node["source"].get<std::string>().size()<=2048 && node["source"].get<std::string>().find('\0')==std::string::npos,"source must be an image path of at most 2048 bytes");
    if(node.contains("fit")) require(node["fit"]=="contain" || node["fit"]=="cover" || node["fit"]=="stretch","fit must be contain, cover or stretch");
    if(node.contains("opacity")) require(node["opacity"].is_number() && std::isfinite(node["opacity"].get<double>()) && node["opacity"].get<double>()>=0 && node["opacity"].get<double>()<=1,"opacity must be between 0 and 1");
    if (node.contains("text")) require(node["text"].is_string() && node["text"].get<std::string>().size() <= 2048, "Text must be a string of at most 2048 UTF-8 bytes");
    if (node.contains("name")) require(node["name"].is_string() && node["name"].get<std::string>().size() <= 256, "name must be a string of at most 256 UTF-8 bytes");
    if (node.contains("visible")) require(node["visible"].is_boolean(), "visible must be boolean");
    if (node.contains("fontSize")) require(node["fontSize"].is_number() && node["fontSize"].get<double>() >= 10 && node["fontSize"].get<double>() <= 48, "fontSize must be between 10 and 48");
    if (node.contains("action")) require(node["action"].is_object(), "action must be a command object");
    for(auto key:{"bindText","event"})if(node.contains(key))require(node[key].is_string()&&node[key].get<std::string>().size()<=80,"Invalid UI binding/event name");
    if(node.contains("layout"))require(node["layout"]=="absolute"||node["layout"]=="row"||node["layout"]=="column"||node["layout"]=="grid","Invalid layout");
    for(auto key:{"padding","gap","rowHeight","anchorX","anchorY","pivotX","pivotY"})if(node.contains(key)){
        require(node[key].is_number(),"UI spacing must be numeric");double v=node[key].get<double>();
        const std::string name=key;const bool fraction=name.starts_with("anchor")||name.starts_with("pivot");
        require(std::isfinite(v)&&v>=(name=="rowHeight"?1:0)&&v<=(fraction?1:16384),"UI spacing out of range");
    }
    for(auto key:{"fillX","fillY"})if(node.contains(key))require(node[key].is_boolean(),"UI fill must be boolean");
    if(node.contains("columns"))require(node["columns"].is_number_unsigned()||(node["columns"].is_number_integer()&&node["columns"].get<int64_t>()>0),"Grid columns must be positive integers");
    if(node.contains("columns"))require(node["columns"].get<double>()<=128,"Too many grid columns");
    if (node.contains("dock")) {
        const std::set<std::string> docks={"none","menubar","toolbar","left-top","left-bottom","center","right","bottom","status"};
        require(node["dock"].is_string() && docks.count(node["dock"].get<std::string>())!=0,"Unknown dock region");
    }
    if (node.contains("children")) {
        require(node["children"].is_array(), "children must be an array");
        for (const auto& child : node["children"]) validateNode(child, ids, depth+1, count);
    }
}
bool removeNode(Json& node, const std::string& id) {
    if (!node.contains("children")) return false;
    auto& children = node["children"];
    for (auto it=children.begin(); it!=children.end(); ++it) {
        if (it->at("id") == id) { children.erase(it); return true; }
        if (removeNode(*it, id)) return true;
    }
    return false;
}
}
Json readJson(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    require(bool(stream), "Cannot read: " + pathText(path));
    require(std::filesystem::file_size(path) <= 4*1024*1024, "JSON file exceeds 4 MiB");
    return Json::parse(stream);
}
void writeJsonAtomic(const std::filesystem::path& path, const Json& data) {
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    auto temp = path;
    temp += "." + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".tmp";
    try {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        require(bool(out), "Cannot create temporary file");
        out << data.dump(2) << '\n'; out.flush();
        require(bool(out), "Failed writing temporary file"); out.close();
#ifdef _WIN32
        require(MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0, "Atomic file replacement failed");
#else
        std::filesystem::rename(temp, path);
#endif
    } catch (...) { std::error_code ec; std::filesystem::remove(temp, ec); throw; }
}
Json* findNode(Json& node, const std::string& id) {
    if (node.at("id") == id) return &node;
    if (node.contains("children")) for (auto& child : node["children"]) if (auto* found=findNode(child,id)) return found;
    return nullptr;
}
const Json* findNode(const Json& node, const std::string& id) {
    if (node.at("id") == id) return &node;
    if (node.contains("children")) for (const auto& child : node["children"]) if (auto* found=findNode(child,id)) return found;
    return nullptr;
}
void validateDocument(const Json& document) {
    if(isScene(document)) {validateScene(document);return;}
    require(document.at("format") == "gm.ui" && document.at("version") == 1, "Expected gm.ui version 1");
    std::set<std::string> ids; int count=0;
    validateNode(document.at("root"), ids, 0, count);
    require(document["root"]["type"] == "panel", "Root must be a panel");
    require(document["root"]["w"].get<double>() >= 320 && document["root"]["h"].get<double>() >= 240, "Root must be at least 320 x 240");
}
Json commandSchema() {
    return Json{
        {"protocol","gm.editor/1"},
        {"commands",{
            {"describe","Discover commands and editable properties"},
            {"query","Optional id; returns document/node and revision"},
            {"select",{{"id","node id"}}},
            {"set",{{"id","node id"},{"property","editable property"},{"value","typed JSON value"}}},
            {"add",{{"parent","node id"},{"node","complete node object"}}},
            {"create",{{"type","panel, label, button or image"},{"parent","optional parent id (defaults to root)"}}},
            {"remove",{{"id","non-root node id"}}},
            {"duplicate",{{"id","non-root node id; clones descendants and internal action references"}}},
            {"batch",{{"commands","array of set/add/remove; commits as one undo step"}}},
            {"undo","Undo last document edit"}, {"redo","Redo last undone edit"},
            {"save","Atomically save; rejects external disk changes"},
            {"reload",{{"discard", "optional boolean; required when unsaved edits exist"}}}
        }},
        {"editable",Json::array({"name","text","x","y","w","h","color","textColor","fontSize","visible","action","dock","source","fit","tint","opacity","layout","padding","gap","anchorX","anchorY","pivotX","pivotY","fillX","fillY","columns","rowHeight","bindText","event"})},
        {"types",Json::array({"panel","label","button","image","tree","inspector","viewport","history","status","menubar","toolbar","files","output"})},
        {"concurrency","Optional expected_revision on requests; mismatches are rejected"}
    };
}
Document::Document(std::filesystem::path path) : path_(std::filesystem::absolute(path)), data_(readJson(path_)), saved_(data_) {
    validateDocument(data_); if(isScene(data_)) ensureSelection();else selected_ = data_["root"]["id"].get<std::string>(); record("Opened " + pathText(path_.filename()));
}
void Document::record(const std::string& text) { log_.push_back(text); if (log_.size()>100) log_.erase(log_.begin()); }
void Document::ensureSelection() { if(isScene(data_)){if(!findEntity(data_,selected_)) selected_=data_["entities"].empty()?"":data_["entities"][0]["id"].get<std::string>();return;}if (!findNode(data_["root"], selected_)) selected_ = data_["root"]["id"].get<std::string>(); }
Json Document::state() const { return {{"revision",revision_},{"dirty",dirty()},{"selected",selected_},{"undo",undo_.size()},{"redo",redo_.size()}}; }
Json Document::applyEdit(const Json& request) {
    if(isScene(data_)) return editScene(data_,selected_,request);
    const auto op=request.at("op").get<std::string>();
    if (op=="set") {
        const auto id=request.at("id").get<std::string>();
        auto* node=findNode(data_["root"],id); require(node!=nullptr,"Node not found: " + id);
        const auto key=request.at("property").get<std::string>();
        const std::set<std::string> allowed={"name","text","x","y","w","h","color","textColor","fontSize","visible","action","dock","source","fit","tint","opacity","layout","padding","gap","anchorX","anchorY","pivotX","pivotY","fillX","fillY","columns","rowHeight","bindText","event"};
        require(allowed.count(key)!=0,"Property is not editable: " + key);
        (*node)[key]=request.at("value");
    } else if (op=="create") {
        const auto type=request.at("type").get<std::string>(); require(type=="panel" || type=="label" || type=="button" || type=="image","create supports panel, label, button and image");
        const auto parentId=request.value("parent",data_["root"]["id"].get<std::string>());
        const auto* parent=findNode(data_["root"],parentId); require(parent!=nullptr,"Parent not found");
        int index=1; std::string id; do { id=type+"_"+std::to_string(index++); } while(findNode(data_["root"],id));
        const float w=type=="panel"?320.f:160.f,h=type=="panel"?180.f:40.f;
        Json node={{"id",id},{"name",type=="panel"?"Panel":type=="label"?"Label":"Button"},{"type",type},{"x",std::max(0.f,(parent->at("w").get<float>()-w)*.5f)},{"y",std::max(0.f,(parent->at("h").get<float>()-h)*.5f)},{"w",w},{"h",h},{"color",type=="button"?"#4F75A7":"#34373D"},{"textColor","#E2E5EB"}};
        if(type=="image") {node["name"]="Image";node["w"]=240;node["h"]=160;node["source"]="";node["fit"]="contain";node["tint"]="#FFFFFF";node["opacity"]=1;}
        else if(type!="panel") node["text"]=type=="button"?"按钮":"文本";
        applyEdit({{"op","add"},{"parent",parentId},{"node",node}}); selected_=id;
        return {{"id",id}};
    } else if(op=="duplicate") {
        const auto id=request.at("id").get<std::string>();require(id!=data_["root"]["id"].get<std::string>(),"Cannot duplicate root");
        auto* source=findNode(data_["root"],id);require(source!=nullptr,"Node not found");Json copy=*source;
        std::map<std::string,std::string> remap;int suffix=1;
        std::function<void(Json&)> rename=[&](Json& node) {auto original=node["id"].get<std::string>();std::string next;do {next=original.substr(0,65)+"_copy"+std::to_string(suffix++);} while(findNode(data_["root"],next));remap[original]=next;node["id"]=next;if(node.contains("children")) for(auto& child:node["children"]) rename(child);};
        rename(copy);
        std::function<void(Json&)> fix=[&](Json& node) {if(node.contains("action") && node["action"].contains("id") && node["action"]["id"].is_string()) {auto key=node["action"]["id"].get<std::string>();if(remap.count(key)) node["action"]["id"]=remap[key];}if(node.contains("children")) for(auto& child:node["children"]) fix(child);};fix(copy);
        copy["x"]=copy["x"].get<float>()+16;copy["y"]=copy["y"].get<float>()+16;
        std::function<bool(Json&)> insert=[&](Json& parent) {if(!parent.contains("children")) return false;for(auto it=parent["children"].begin();it!=parent["children"].end();++it) {if(it->at("id")==id) {parent["children"].insert(it+1,copy);return true;}if(insert(*it)) return true;}return false;};
        require(insert(data_["root"]),"Parent not found");selected_=copy["id"].get<std::string>();return {{"id",selected_}};
    } else if (op=="add") {
        auto* parent=findNode(data_["root"],request.at("parent").get<std::string>());
        require(parent!=nullptr,"Parent not found");
        if (!parent->contains("children")) (*parent)["children"]=Json::array();
        (*parent)["children"].push_back(request.at("node"));
    } else if (op=="remove") {
        const auto id=request.at("id").get<std::string>();
        require(id!=data_["root"]["id"].get<std::string>(),"Cannot remove root");
        require(removeNode(data_["root"],id),"Node not found: " + id);
    } else throw std::runtime_error("Not an editable operation: " + op);
    return {{"op",op}};
}
Json Document::execute(const Json& request) {
    try {
        require(request.is_object(),"Command must be an object");
        const auto op=request.at("op").get<std::string>();
        if (request.contains("expected_revision")) require(request["expected_revision"]==revision_,"Revision conflict; query again");
        Json result;
        if (op=="describe") result=isScene(data_)?sceneSchema():commandSchema();
        else if(op=="system.list"&&isScene(data_)) result=systemSchema(data_);
        else if (op=="query") {
            if (request.contains("id")) { const auto* n=isScene(data_)?findEntity(data_,request["id"].get<std::string>()):findNode(data_["root"],request["id"].get<std::string>()); require(n!=nullptr,"Object not found"); result=*n; }
            else result=data_;
        } else if (op=="select") {
            const auto id=request.at("id").get<std::string>(); require((isScene(data_)?findEntity(data_,id):findNode(data_["root"],id))!=nullptr,"Object not found"); selected_=id;
        } else if (op=="set" || op=="add" || op=="remove" || op=="create" || op=="duplicate" || op=="batch" || (isScene(data_) && (op.starts_with("entity.")||op.starts_with("component.")||op=="system.set"||op.starts_with("binding.")))) {
            Snapshot previous{data_,selected_};
            try {
                if(op=="batch") {
                    const auto& commands=request.at("commands");
                    require(commands.is_array() && commands.size()<=512,"Batch must have at most 512 commands");
                    for (const auto& cmd : commands) { if(!isScene(data_)) require(cmd.at("op")=="set" || cmd.at("op")=="add" || cmd.at("op")=="remove","Batch supports set/add/remove"); applyEdit(cmd); }
                } else result=applyEdit(request);
                validateDocument(data_);
            } catch (...) { data_=std::move(previous.data); selected_=previous.selected; throw; }
            if (data_!=previous.data) {
                if(isScene(data_)) {previous.data=Json::diff(data_,previous.data);previous.patch=true;}
                undo_.push_back(std::move(previous)); if(undo_.size()>128) undo_.erase(undo_.begin());
                if(isScene(data_)) {size_t bytes=0;for(auto it=undo_.rbegin();it!=undo_.rend();++it){bytes+=it->data.dump().size();if(bytes>16*1024*1024){undo_.erase(undo_.begin(),it.base());break;}}}
                redo_.clear(); ++revision_; ensureSelection(); record(op + (request.contains("id") ? " " + request["id"].get<std::string>() : ""));
            }
        } else if (op=="undo" || op=="redo") {
            auto& source=op=="undo"?undo_:redo_; auto& dest=op=="undo"?redo_:undo_;
            require(!source.empty(),"Nothing to " + op);
            auto snapshot=std::move(source.back()); source.pop_back();
            auto target=snapshot.patch?data_.patch(snapshot.data):std::move(snapshot.data);
            dest.push_back({snapshot.patch?Json::diff(target,data_):data_,selected_,snapshot.patch});
            data_=std::move(target); selected_=std::move(snapshot.selected); ++revision_; record(op);
        } else if (op=="save") {
            require(std::filesystem::exists(path_) && readJson(path_)==saved_,"File changed externally; reload before saving");
            validateDocument(data_); writeJsonAtomic(path_,data_); saved_=data_; record("Saved " + pathText(path_.filename()));
        } else if (op=="reload") {
            require(!dirty() || request.value("discard",false),"Unsaved edits; use discard:true to reload");
            auto next=readJson(path_); validateDocument(next); data_=std::move(next); saved_=data_; undo_.clear(); redo_.clear(); ++revision_; ensureSelection(); record("Reloaded from disk");
        } else throw std::runtime_error("Unknown command: " + op);
        auto response=state(); response["ok"]=true; response["result"]=result; return response;
    } catch (const std::exception& e) { auto response=state(); response["ok"]=false; response["error"]=e.what(); return response; }
}
}
