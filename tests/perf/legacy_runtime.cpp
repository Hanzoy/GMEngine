#include "legacy_runtime.hpp"
#include <charconv>
#include <set>
#include <stdexcept>

namespace gm {
namespace {
void require(bool condition,const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}
void applyAction(Json& document,const Json& action) {
    const auto op=action.at("op").get<std::string>();
    require(op=="set" || op=="increment","Unsupported runtime action: "+op);
    auto* target=findNode(document["root"],action.at("id").get<std::string>());
    require(target!=nullptr,"Runtime action target does not exist");
    const auto property=action.at("property").get<std::string>();
    if(op=="set") {
        static const std::set<std::string> properties={"text","color","textColor","visible","x","y","w","h","fontSize"};
        require(properties.count(property)!=0,"Unsupported runtime property: "+property);
        (*target)[property]=action.at("value");
    } else {
        require(property=="text","increment currently supports numeric text only");
        const auto text=target->at("text").get<std::string>();
        int64_t value=0;
        const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
        require(parsed.ec==std::errc{} && parsed.ptr==text.data()+text.size(),"Counter text must be an integer");
        require(value>=-1000000000 && value<=1000000000,"Counter value is out of range");
        const auto amount=action.value("amount",Json(1));
        require(amount.is_number_integer() && amount.get<double>()>=-1000000 && amount.get<double>()<=1000000,"Increment amount must be an integer within +/-1000000");
        value+=amount.get<int64_t>();
        require(value>=-1000000000 && value<=1000000000,"Counter value is out of range");
        (*target)[property]=std::to_string(value);
    }
    validateDocument(document);
}
void validateRuntimeNodes(const Json& node,const Json& document) {
    const auto type=node.at("type").get<std::string>();
    require(type=="panel" || type=="label" || type=="button","Player supports panel, label and button; unsupported node: "+node.at("id").get<std::string>());
    if(node.contains("action")) {
        require(type=="button","Only buttons may declare runtime actions");
        auto trial=document;
        applyAction(trial,node.at("action"));
    }
    if(node.contains("children")) for(const auto& child:node["children"]) validateRuntimeNodes(child,document);
}
const Json* visibleNode(const Json& node,const std::string& id) {
    if(!node.value("visible",true)) return nullptr;
    if(node.at("id")==id) return &node;
    if(node.contains("children")) for(const auto& child:node["children"]) if(auto* found=visibleNode(child,id)) return found;
    return nullptr;
}
}
LegacyRuntimeDocument::LegacyRuntimeDocument(std::filesystem::path path):path_(std::move(path)) { reload(); }
void LegacyRuntimeDocument::reload() {
    auto next=readJson(path_);
    validateDocument(next);
    validateRuntimeNodes(next["root"],next);
    data_=std::move(next);
}
Json LegacyRuntimeDocument::activate(const std::string& buttonId) {
    try {
        const auto* button=visibleNode(data_["root"],buttonId);
        require(button && button->at("type")=="button","Visible button not found: "+buttonId);
        require(button->contains("action"),"Button has no action: "+buttonId);
        const auto action=button->at("action");
        auto next=data_;
        applyAction(next,action);
        data_=std::move(next);
        return {{"ok",true}};
    } catch(const std::exception& error) {
        return {{"ok",false},{"error",error.what()}};
    }
}
}
