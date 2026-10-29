#include "ui_compiler.hpp"
#include "image_import.hpp"
#include <cmath>
#include <functional>
#include <unordered_map>
#include <stdexcept>

namespace gm {
namespace {
uint32_t packedColor(const std::string& hex) {auto v=uint32_t(std::stoul(hex.substr(1),nullptr,16));return ((v>>16)&255)|(v&0xff00)|((v&255)<<16)|0xff000000;}
}
CompiledUi compileUi(const Json& source,const std::filesystem::path& sourceDirectory) {
    validateDocument(source);CompiledUi asset;
    std::unordered_map<std::filesystem::path,uint32_t> imageIds;uint64_t imageBytes=0;
    auto imageIndex=[&](const std::string& sourcePath) {
        if(sourcePath.empty()) throw std::runtime_error("Image source is empty");
        auto path=std::filesystem::canonical(sourceDirectory/std::filesystem::path(std::u8string(sourcePath.begin(),sourcePath.end())));
        if(auto it=imageIds.find(path);it!=imageIds.end()) return it->second;
        auto image=importImage(path);imageBytes+=image.rgba.size();
        if(imageBytes>MaxImageBytes) throw std::runtime_error("UI images exceed the 64 MiB decoded budget");
        const auto index=uint32_t(asset.images.size());imageIds.emplace(path,index);asset.images.push_back(std::move(image));return index;
    };
    std::unordered_map<std::string,uint32_t> ids;std::vector<const Json*> sources;
    std::function<void(const Json&,uint32_t)> flatten=[&](const Json& src,uint32_t parent) {
        const auto index=uint32_t(asset.nodes.size());const auto type=src.at("type").get<std::string>();
        UiNode node;
        if(type=="panel") node.kind=UiKind::Panel;else if(type=="label") node.kind=UiKind::Label;else if(type=="button") node.kind=UiKind::Button;
        else if(type=="image") {
            node.kind=UiKind::Image;
            try {node.image=imageIndex(src.value("source",std::string()));}
            catch(const std::exception& error) {throw std::runtime_error("Image "+src.at("id").get<std::string>()+": "+error.what());}
            node.fit=parseImageFit(src.value("fit",std::string("contain")));
            node.tint=(packedColor(src.value("tint",std::string("#FFFFFF")))&0xffffff)|(uint32_t(std::lround(src.value("opacity",1.f)*255))<<24);
        }
        else throw std::runtime_error("Player does not support editor widget: "+src.at("id").get<std::string>());
        node.parent=parent;node.bounds={src["x"].get<float>(),src["y"].get<float>(),src["w"].get<float>(),src["h"].get<float>()};
        node.color=packedColor(src.value("color",std::string("#182538")));node.textColor=packedColor(src.value("textColor",std::string("#DFE7F2")));
        node.fontSize=src.value("fontSize",14.f);node.visible=src.value("visible",true);node.text=src.value("text",std::string());
        node.integerText=parseCounter(node.text,node.integerValue);
        ids.emplace(src["id"].get<std::string>(),index);asset.nodes.push_back(std::move(node));sources.push_back(&src);
        if(src.contains("children")) for(const auto& child:src["children"]) flatten(child,index);
        asset.nodes[index].subtreeEnd=uint32_t(asset.nodes.size());
    };
    flatten(source["root"],NoIndex);
    const std::unordered_map<std::string,UiOp> properties={{"text",UiOp::SetText},{"color",UiOp::SetColor},{"textColor",UiOp::SetTextColor},{"visible",UiOp::SetVisible},{"x",UiOp::SetX},{"y",UiOp::SetY},{"w",UiOp::SetWidth},{"h",UiOp::SetHeight},{"fontSize",UiOp::SetFontSize},{"fit",UiOp::SetImageFit},{"tint",UiOp::SetTint},{"opacity",UiOp::SetOpacity}};
    for(uint32_t i=0;i<sources.size();++i) {
        const auto& src=*sources[i];if(!src.contains("action")) continue;
        try {
            if(asset.nodes[i].kind!=UiKind::Button) throw std::runtime_error("Only buttons may have actions");
            const auto& data=src["action"];const auto op=data.at("op").get<std::string>();
            UiAction action;action.target=ids.at(data.at("id").get<std::string>());
            const auto property=data.at("property").get<std::string>();
            if(op=="increment") {
                if(property!="text") throw std::runtime_error("increment requires numeric text");
                action.op=UiOp::AddInteger;const auto amount=data.value("amount",Json(1));
                if(!amount.is_number_integer() || amount.get<double>()<-1000000 || amount.get<double>()>1000000) throw std::runtime_error("Invalid integer increment");
                action.integer=amount.get<int64_t>();
            } else if(op=="set") {
                action.op=properties.at(property);const auto& value=data.at("value");
                if(action.op==UiOp::SetText) {action.text=value.get<std::string>();action.packed=parseCounter(action.text,action.integer)?1:0;}
                else if(action.op==UiOp::SetColor || action.op==UiOp::SetTextColor || action.op==UiOp::SetTint) {
                    const auto text=value.get<std::string>();
                    if(text.size()!=7 || text[0]!='#' || text.find_first_not_of("0123456789abcdefABCDEF",1)!=std::string::npos) throw std::runtime_error("Invalid action color");
                    action.packed=packedColor(text);
                } else if(action.op==UiOp::SetImageFit) action.packed=uint32_t(parseImageFit(value.get<std::string>()));
                else if(action.op==UiOp::SetVisible) action.packed=value.get<bool>()?1:0;
                else action.number=value.get<float>();
            } else throw std::runtime_error("Unsupported runtime action: "+op);
            asset.nodes[i].action=uint32_t(asset.actions.size());asset.actions.push_back(std::move(action));
        } catch(const std::exception& error) {throw std::runtime_error("Button "+src["id"].get<std::string>()+": "+error.what());}
    }
    validateCompiledUi(asset);return asset;
}
}
