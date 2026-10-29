#include "scene_authoring.hpp"
#include <algorithm>
#include <bit>
#include <cstdio>
#include <cmath>
#include <functional>
#include <random>
#include <set>
#include <unordered_map>
#include <stdexcept>
namespace gm {
namespace {
void check(bool ok,const std::string& error) {if(!ok) throw std::runtime_error(error);}
bool colorValid(const std::string& s) {return s.size()==7 && s[0]=='#' && std::all_of(s.begin()+1,s.end(),[](unsigned char c){return std::isxdigit(c)!=0;});}
}
const std::vector<ComponentDefinition>& sceneComponents() {
    static std::weak_ptr<GameRegistry> owner;static std::vector<ComponentDefinition> definitions;auto registry=gameRegistry();
    if(owner.lock()!=registry){definitions.clear();for(const auto& c:registry->components()){const auto& d=c.descriptor;ComponentDefinition def{c.id,d.name,d.label,{},{},d.version};for(uint32_t i=0;i<d.fieldCount;++i){const auto& f=d.fields[i];Json initial=f.initial;double maximum=f.maximum;if(f.kind==sdk::Kind::Bool)initial=f.initial!=0;else if(f.kind==sdk::Kind::Int)initial=int32_t(f.initial);else if(f.kind==sdk::Kind::Image){initial="";maximum=2048;}else if(f.kind==sdk::Kind::Color){char color[8];uint32_t v=uint32_t(f.initial);std::snprintf(color,sizeof(color),"#%02X%02X%02X",v&255,(v>>8)&255,(v>>16)&255);initial=color;maximum=7;}def.fields.push_back({f.name,f.label,initial,f.minimum,maximum,f.kind});}for(uint32_t i=0;i<d.requiresCount;++i)def.dependencies.push_back(d.requiresTypes[i]);definitions.push_back(std::move(def));}owner=registry;}return definitions;
}
std::vector<uint32_t> componentWords(const std::string& type,const Json& fields){const auto& d=sceneComponent(type);check(fields.is_object()&&fields.size()==d.fields.size(),"Missing / unknown fields in "+type);std::vector<uint32_t> out;
    for(const auto& f:d.fields){const auto& v=fields.at(f.key);uint32_t word=0;if(f.kind==sdk::Kind::Image){check(v.is_string()&&v.get<std::string>().size()<=2048&&v.get<std::string>().find('\0')==std::string::npos,"Invalid image path");word=UINT32_MAX;}else if(f.kind==sdk::Kind::Color){check(v.is_string()&&colorValid(v.get<std::string>()),"Color must be #RRGGBB");auto n=uint32_t(std::stoul(v.get<std::string>().substr(1),nullptr,16));word=0xff000000|((n&255)<<16)|(n&0xff00)|(n>>16);}else if(f.kind==sdk::Kind::Bool){check(v.is_boolean(),"Expected boolean: "+type+"."+f.key);word=v.get<bool>()?1:0;}else{check(v.is_number(),"Expected number: "+type+"."+f.key);auto n=v.get<double>();check(std::isfinite(n)&&n>=f.minimum&&n<=f.maximum,"Field out of range: "+type+"."+f.key);if(f.kind==sdk::Kind::Int){check(std::floor(n)==n,"Expected integer");word=std::bit_cast<uint32_t>(int32_t(n));}else word=std::bit_cast<uint32_t>(float(n));}out.push_back(word);}gameRegistry()->validate(d.id,out);return out;}
Json componentJson(sdk::TypeId type,std::span<const uint32_t> fields){const auto& d=gameRegistry()->component(type).descriptor;check(fields.size()==d.fieldCount,"Invalid component fields");Json result=Json::object();for(uint32_t i=0;i<d.fieldCount;++i){auto& f=d.fields[i];if(f.kind==sdk::Kind::Float)result[f.name]=std::bit_cast<float>(fields[i]);else if(f.kind==sdk::Kind::Int)result[f.name]=std::bit_cast<int32_t>(fields[i]);else if(f.kind==sdk::Kind::Bool)result[f.name]=fields[i]!=0;else if(f.kind==sdk::Kind::Color){char c[8];std::snprintf(c,sizeof(c),"#%02X%02X%02X",fields[i]&255,(fields[i]>>8)&255,(fields[i]>>16)&255);result[f.name]=c;}else result[f.name]=fields[i];}return result;}
const ComponentDefinition& sceneComponent(const std::string& type) {for(const auto& d:sceneComponents()) if(d.name==type) return d;throw std::runtime_error("Unknown component: "+type);}
bool isScene(const Json& data) {return data.is_object() && data.value("format",std::string())=="gm.scene";}
Json* findEntity(Json& data,const std::string& id) {for(auto& e:data.at("entities")) if(e.at("id")==id) return &e;return nullptr;}
const Json* findEntity(const Json& data,const std::string& id) {for(const auto& e:data.at("entities")) if(e.at("id")==id) return &e;return nullptr;}
Json* findComponent(Json& entity,const std::string& type) {for(auto& c:entity.at("components")) if(c.at("type")==type) return &c;return nullptr;}
const Json* findComponent(const Json& entity,const std::string& type) {for(const auto& c:entity.at("components")) if(c.at("type")==type) return &c;return nullptr;}
std::string newEntityId() {static std::mt19937_64 random(std::random_device{}());const char* hex="0123456789abcdef";std::string id;for(int i=0;i<32;++i) {if(i==8||i==12||i==16||i==20) id+='-';id+=hex[random()&15];}id[14]='4';id[19]=hex[8+(random()&3)];return id;}
Json newScene() {return {{"format","gm.scene"},{"version",1},{"name","Untitled"},{"width",1280},{"height",720},{"background","#18212D"},{"entities",Json::array()}};}
void validateScene(const Json& data) {
    check(isScene(data) && data.at("version")==1,"Expected gm.scene version 1");
    for(auto key:{"width","height"}) {check(data.at(key).is_number(),"Scene size must be numeric");double n=data[key].get<double>();check(std::isfinite(n)&&n>=320&&n<=8192,"Scene size must be 320..8192");}
    check(colorValid(data.at("background").get<std::string>()),"Scene background must be #RRGGBB");
    check(data.at("entities").is_array() && data["entities"].size()<=100000,"Scene entity limit is 100000");
    std::unordered_map<std::string,const Json*> ids;
    for(const auto& e:data["entities"]) {
        const auto id=e.at("id").get<std::string>();check(!id.empty()&&id.size()<=80&&id.find('\0')==std::string::npos&&ids.emplace(id,&e).second,"Invalid / duplicate entity ID");
        check(e.at("name").is_string()&&e["name"].get<std::string>().size()<=256,"Invalid entity name");
        check(e.at("parent").is_null()||e["parent"].is_string(),"parent must be an entity ID or null");
        check(e.at("components").is_array()&&e["components"].size()<=sceneComponents().size(),"Invalid component list");std::set<std::string> types;
        for(const auto& c:e["components"]) {
            auto type=c.at("type").get<std::string>();const auto& def=sceneComponent(type);check(types.insert(type).second,"Duplicate component: "+type);check(c.at("version")==def.version,"Unsupported component version: "+type);componentWords(type,c.at("fields"));
        }
        for(const auto& type:types) for(const auto& dep:sceneComponent(type).dependencies) check(types.count(dep)!=0,type+" requires "+dep);
    }
    std::unordered_map<std::string,int> state,depths;
    std::function<int(const std::string&,int)> visit=[&](const std::string& id,int recursion){check(recursion<=128,"Scene hierarchy exceeds 128 levels");if(state[id]==2)return depths[id];check(state[id]!=1,"Hierarchy cycle");state[id]=1;int depth=1;const auto& parent=ids.at(id)->at("parent");if(!parent.is_null()){auto p=parent.get<std::string>();check(ids.count(p)!=0,"Parent not found: "+p);depth=visit(p,recursion+1)+1;}check(depth<=128,"Scene hierarchy exceeds 128 levels");state[id]=2;return depths[id]=depth;};
    for(const auto& [id,e]:ids) visit(id,0);
    if(data.contains("disabledSystems")){check(data["disabledSystems"].is_array(),"disabledSystems must be an array");std::set<std::string> names;for(const auto& s:data["disabledSystems"]){auto name=s.get<std::string>();gameRegistry()->system(name);check(names.insert(name).second,"Duplicate disabled System");}}
    if(data.contains("ui")) {const auto& ui=data["ui"];check(ui.at("source").is_string()&&!ui["source"].get<std::string>().empty(),"UI source is required");check(ui.at("bindings").is_array()&&ui["bindings"].size()<=512,"Invalid UI bindings");std::set<std::string> nodes;
        for(const auto& b:ui["bindings"]) {auto entity=b.at("entity").get<std::string>();auto type=b.at("component").get<std::string>();check(ids.count(entity)&&findComponent(*ids.at(entity),type),"UI binding component is missing");const auto& d=sceneComponent(type);auto numeric=[&](std::string key){for(const auto& f:d.fields)if(f.key==key)return f.kind==sdk::Kind::Float||f.kind==sdk::Kind::Int;return false;};check(numeric(b.at("field").get<std::string>()),"UI binding needs numeric field");if(b.contains("maximum"))check(numeric(b["maximum"].get<std::string>()),"UI maximum needs numeric field");auto kind=b.at("kind").get<std::string>();check(kind=="number.text"||kind=="number.bar"||kind=="number.change","Unknown UI binding");check(kind!="number.bar"||b.contains("maximum"),"Bar needs maximum field");check(b.at("node").is_string()&&nodes.insert(b["node"].get<std::string>()).second,"Duplicate UI binding node");if(kind=="number.change") check(b.at("amount").is_number()&&std::isfinite(b["amount"].get<double>())&&std::abs(b["amount"].get<double>())<=1000000,"Invalid numeric amount");}}
}
Json systemSchema(const Json& scene){Json out=Json::array();auto disabled=scene.value("disabledSystems",Json::array());for(const auto& s:gameRegistry()->systems()){const auto& d=s.descriptor;Json after=Json::array();for(uint32_t i=0;i<d.afterCount;++i)after.push_back(d.after[i]);out.push_back({{"name",d.name},{"label",d.label},{"phase",phaseName(d.phase)},{"after",after},{"owner",s.engine?"engine":"project"},{"enabled",std::find(disabled.begin(),disabled.end(),Json(d.name))==disabled.end()}});}return out;}
Json sceneSchema() {
    Json defs=Json::array();for(const auto& d:sceneComponents()) {Json fields=Json::array();for(const auto& f:d.fields) fields.push_back({{"key",f.key},{"label",f.label},{"kind",uint32_t(f.kind)},{"default",f.initial},{"minimum",f.minimum},{"maximum",f.maximum}});defs.push_back({{"id",d.id},{"type",d.name},{"label",d.label},{"version",d.version},{"requires",d.dependencies},{"fields",fields}});}
    return {{"protocol","gm.scene/1"},{"components",defs},{"commands",{{"describe","Component definitions and commands"},{"query","Optional entity id"},{"select","Entity id"},{"entity.create","Optional name, parent and id"},{"entity.delete","id; removes subtree and its UI bindings"},{"entity.duplicate","id; duplicates subtree with new IDs"},{"entity.reparent","id and parent (null for root); preserves local position"},{"component.add","id, type; adds required components"},{"component.remove","id, type; rejects dependency / UI binding conflicts"},{"component.set","id, type, field, value"},{"set","id, property: name or Component.field, value"},{"batch","Atomic array of scene edits"},{"undo","Undo"},{"redo","Redo"},{"save","Atomic save with external change check"},{"reload","Requires discard:true for unsaved changes"}}}};
}
Json editScene(Json& data,std::string& selected,const Json& req) {
    auto op=req.at("op").get<std::string>();if(op=="create")op="entity.create";if(op=="remove")op="entity.delete";if(op=="duplicate")op="entity.duplicate";
    if(op=="system.set"){auto name=req.at("name").get<std::string>();gameRegistry()->system(name);check(req.at("enabled").is_boolean(),"enabled must be boolean");if(!data.contains("disabledSystems"))data["disabledSystems"]=Json::array();auto& disabled=data["disabledSystems"];disabled.erase(std::remove(disabled.begin(),disabled.end(),Json(name)),disabled.end());if(!req["enabled"].get<bool>())disabled.push_back(name);return {{"systems",systemSchema(data)}};}
    if(op=="binding.set"||op=="binding.remove"){check(data.contains("ui"),"Scene has no UI resource");auto node=req.at("node").get<std::string>();auto& bindings=data["ui"]["bindings"];bindings.erase(std::remove_if(bindings.begin(),bindings.end(),[&](const Json& b){return b["node"]==node;}),bindings.end());if(op=="binding.set"){auto b=req.at("binding");b["node"]=node;bindings.push_back(b);}return {{"node",node}};}
    if(op=="entity.create") {auto id=req.value("id",newEntityId());data["entities"].push_back({{"id",id},{"name",req.value("name",std::string("Entity"))},{"parent",req.value("parent",Json())},{"components",Json::array()}});selected=id;return {{"id",id}};}
    auto id=req.at("id").get<std::string>();auto* e=findEntity(data,id);check(e!=nullptr,"Entity not found: "+id);
    if(op=="entity.delete"||op=="entity.duplicate") {
        std::set<std::string> subtree{id};bool changed=true;while(changed){changed=false;for(const auto& node:data["entities"])if(!node["parent"].is_null()&&subtree.count(node["parent"].get<std::string>()))changed|=subtree.insert(node["id"].get<std::string>()).second;}
        if(op=="entity.delete") {auto& entities=data["entities"];entities.erase(std::remove_if(entities.begin(),entities.end(),[&](const auto& n){return subtree.count(n.at("id").template get<std::string>())!=0;}),entities.end());if(data.contains("ui")){auto& b=data["ui"]["bindings"];b.erase(std::remove_if(b.begin(),b.end(),[&](const auto& n){return subtree.count(n.at("entity").template get<std::string>())!=0;}),b.end());}}
        else {std::unordered_map<std::string,std::string> map;for(const auto& old:subtree)map[old]=newEntityId();Json copies=Json::array();for(const auto& node:data["entities"])if(subtree.count(node["id"].get<std::string>())){auto copy=node;copy["id"]=map[node["id"].get<std::string>()];if(!copy["parent"].is_null()&&map.count(copy["parent"].get<std::string>()))copy["parent"]=map[copy["parent"].get<std::string>()];if(node["id"]==id) {copy["name"]=node["name"].get<std::string>()+" Copy";if(auto* t=findComponent(copy,"Transform2D")){t->at("fields")["x"]=t->at("fields")["x"].get<float>()+24;}}copies.push_back(copy);}for(auto& copy:copies)data["entities"].push_back(std::move(copy));selected=map[id];return {{"id",selected}};}
    } else if(op=="entity.reparent") e->at("parent")=req.at("parent");
    else if(op=="component.add") {std::function<void(const std::string&)> add=[&](const std::string& type){const auto& def=sceneComponent(type);if(findComponent(*e,type))return;for(const auto& dep:def.dependencies)add(dep);Json fields=Json::object();for(const auto& f:def.fields)fields[f.key]=f.initial;e->at("components").push_back({{"type",type},{"version",def.version},{"fields",fields}});};auto type=req.at("type").get<std::string>();check(!findComponent(*e,type),"Component already present");add(type);}
    else if(op=="component.remove") {auto type=req.at("type").get<std::string>();check(findComponent(*e,type)!=nullptr,"Component not found");auto& comps=e->at("components");comps.erase(std::remove_if(comps.begin(),comps.end(),[&](const auto& c){return c.at("type")==type;}),comps.end());}
    else if(op=="set"||op=="component.set") {auto property=req.value("property",std::string());if(op=="set"&&property=="name")e->at("name")=req.at("value");else {auto dot=property.rfind('.');auto type=op=="component.set"?req.at("type").get<std::string>():property.substr(0,dot);auto field=op=="component.set"?req.at("field").get<std::string>():dot==std::string::npos?std::string():property.substr(dot+1);auto* c=findComponent(*e,type);check(c&&c->at("fields").contains(field),"Unknown component field");c->at("fields")[field]=req.at("value");}}
    else throw std::runtime_error("Unknown scene edit: "+op);
    return {{"id",id}};
}
}
