#include "scene_compiler.hpp"
#include "ui_compiler.hpp"
#include "image_import.hpp"
#include "render_compiler.hpp"
#include <unordered_map>
#include <functional>
namespace gm {
namespace {uint32_t packed(const std::string& c){auto n=uint32_t(std::stoul(c.substr(1),nullptr,16));return 0xff000000|((n&255)<<16)|(n&0xff00)|(n>>16);}std::filesystem::path utfpath(const std::string& p){return std::filesystem::path(std::u8string(p.begin(),p.end()));}}
CompiledScene compileScene(const Json& source,const std::filesystem::path& directory) {
    validateScene(source);auto registry=gameRegistry();CompiledScene s;s.width=source["width"].get<float>();s.height=source["height"].get<float>();s.background=packed(source["background"].get<std::string>());s.schema=registry->fingerprint();s.module=registry->name();s.disabledSystems=source.value("disabledSystems",std::vector<std::string>{});
    UiNode root;root.bounds={0,0,s.width,s.height};root.parent=NoIndex;root.subtreeEnd=1;s.imageBank.nodes.push_back(root);s.hud.nodes.push_back(root);
    RenderCompiler rendering(s.rendering);
    std::unordered_map<std::string,uint32_t> ids;std::unordered_map<std::filesystem::path,uint32_t> images;uint64_t imageBytes=0;
    for(const auto& e:source["entities"]){ids[e["id"].get<std::string>()]=uint32_t(s.entities.size());s.entities.push_back({e["id"].get<std::string>()});}
    for(size_t i=0;i<s.entities.size();++i){auto& out=s.entities[i];const auto& e=source["entities"][i];if(!e["parent"].is_null())out.parent=ids.at(e["parent"].get<std::string>());
        for(const auto& c:e["components"]){auto name=c["type"].get<std::string>();const auto& d=registry->component(name);auto words=componentWords(name,c["fields"]);
            for(uint32_t f=0;f<d.descriptor.fieldCount;++f)if(d.descriptor.fields[f].kind==sdk::Kind::Image){auto image=c["fields"][d.descriptor.fields[f].name].get<std::string>();if(image.empty())continue;auto path=std::filesystem::canonical(resolveResource(directory,image));auto it=images.find(path);if(it==images.end()){auto id=uint32_t(s.imageBank.images.size());if(id>=512)throw std::runtime_error("Too many scene images");auto decoded=importImage(path);imageBytes+=decoded.rgba.size();if(imageBytes>MaxImageBytes)throw std::runtime_error("Scene images exceed 64 MiB");s.imageBank.images.push_back(std::move(decoded));images[path]=id;words[f]=id;}else words[f]=it->second;}
            for(uint32_t f=0;f<d.descriptor.fieldCount;++f){auto& field=d.descriptor.fields[f];if(field.kind==sdk::Kind::Mesh)words[f]=rendering.mesh(directory,c["fields"][field.name].get<std::string>());if(field.kind==sdk::Kind::Material)words[f]=rendering.material(directory,c["fields"][field.name].get<std::string>());if(field.kind==sdk::Kind::Texture)words[f]=rendering.texture(directory,c["fields"][field.name].get<std::string>());}
            out.components.push_back({d.id,d.descriptor.version,std::move(words)});
        }
    }
    if(source.contains("ui")){const auto& ui=source["ui"];auto path=resolveResource(directory,ui["source"].get<std::string>());auto doc=readJson(path);s.hud=compileUi(doc,path.parent_path());std::unordered_map<std::string,uint32_t> nodes;uint32_t index=0;std::function<void(const Json&)> walk=[&](const Json& n){nodes[n["id"].get<std::string>()]=index++;if(n.contains("children"))for(const auto& c:n["children"])walk(c);};walk(doc["root"]);
        for(const auto& b:ui["bindings"]){auto node=b["node"].get<std::string>();if(!nodes.count(node))throw std::runtime_error("UI binding node not found: "+node);auto kind=b["kind"].get<std::string>();const auto& d=registry->component(b["component"].get<std::string>());auto field=[&](std::string name){for(uint32_t f=0;f<d.descriptor.fieldCount;++f)if(name==d.descriptor.fields[f].name)return f;throw std::runtime_error("Unknown binding field");};s.bindings.push_back({ids.at(b["entity"].get<std::string>()),nodes.at(node),kind=="number.text"?BindingKind::NumberText:kind=="number.bar"?BindingKind::NumberBar:BindingKind::NumberChange,b.value("amount",0.f),d.id,field(b["field"].get<std::string>()),b.contains("maximum")?field(b["maximum"].get<std::string>()):NoIndex});}
    }
    validateSceneBinary(s);return s;
}
}
