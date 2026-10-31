#include "scene.hpp"
#include <bit>
#include <fstream>
#include <cmath>
#include <functional>
#include <set>
#include <chrono>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
namespace gm {
namespace {
void require(bool ok,const char* error){if(!ok)throw std::runtime_error(error);}
void number(float v,float lo,float hi){require(std::isfinite(v)&&v>=lo&&v<=hi,"Scene numeric field out of range");}
uint32_t checksum(std::span<const uint8_t> b){uint32_t h=2166136261u;for(auto v:b){h^=v;h*=16777619u;}return h;}
void put(std::vector<uint8_t>& b,uint32_t v){for(int i=0;i<4;++i)b.push_back(uint8_t(v>>(i*8)));}
void put64(std::vector<uint8_t>& b,uint64_t v){put(b,uint32_t(v));put(b,uint32_t(v>>32));}
void putFloat(std::vector<uint8_t>& b,float v){put(b,std::bit_cast<uint32_t>(v));}
void putString(std::vector<uint8_t>& b,const std::string& s){put(b,uint32_t(s.size()));b.insert(b.end(),s.begin(),s.end());}
struct Reader {std::span<const uint8_t> b;size_t offset=0;uint32_t u32(){require(offset+4<=b.size(),"Truncated scene binary");uint32_t v=0;for(int i=0;i<4;++i)v|=uint32_t(b[offset++])<<(i*8);return v;}uint64_t u64(){uint64_t lo=u32();return lo|(uint64_t(u32())<<32);}float f32(){return std::bit_cast<float>(u32());}std::span<const uint8_t> block(size_t n){require(n<=b.size()-offset,"Invalid scene section size");auto part=b.subspan(offset,n);offset+=n;return part;}std::string string(){auto n=u32();require(n>0&&n<=128,"Invalid scene string size");auto p=block(n);std::string s(reinterpret_cast<const char*>(p.data()),p.size());require(s.find('\0')==std::string::npos,"Invalid scene string");return s;}};
}
const ComponentValue* findValue(const SceneEntity& entity,sdk::TypeId type){for(const auto& c:entity.components)if(c.type==type)return &c;return nullptr;}
void validateSceneBinary(const CompiledScene& s){
    auto registry=gameRegistry();require(registry->compatible(s.schema)&&s.module==registry->name(),"Scene/module schema mismatch; load the correct module and recompile the scene");
    number(s.width,320,8192);number(s.height,320,8192);require((s.background>>24)==255,"Invalid scene color");require(s.entities.size()<=100000,"Too many entities");validateCompiledUi(s.imageBank);validateCompiledUi(s.hud);require(s.imageBank.nodes.size()==1&&s.imageBank.actions.empty(),"Invalid scene image bank");require(s.hud.nodes[0].bounds.w==s.width&&s.hud.nodes[0].bounds.h==s.height,"HUD and scene sizes differ");
    validateRenderAssets(s.rendering);
    std::set<std::string> ids;std::vector<uint8_t> state(s.entities.size());size_t totalFields=0;
    for(const auto& e:s.entities){require(!e.id.empty()&&e.id.size()<=80&&e.id.find('\0')==std::string::npos&&ids.insert(e.id).second,"Invalid entity identity");require(e.parent==NoIndex||e.parent<s.entities.size(),"Invalid parent index");require(e.components.size()<=256,"Too many components");std::set<sdk::TypeId> types;
        for(const auto& c:e.components){const auto& d=registry->component(c.type).descriptor;require(types.insert(c.type).second&&c.version==d.version,"Duplicate / unsupported component version");registry->validate(c.type,c.fields);totalFields+=c.fields.size();require(totalFields<=2000000,"Too many component fields");for(uint32_t f=0;f<d.fieldCount;++f)if(d.fields[f].kind==sdk::Kind::Image)require(c.fields[f]==NoIndex||c.fields[f]<s.imageBank.images.size(),"Invalid image index");for(uint32_t f=0;f<d.fieldCount;++f){if(d.fields[f].kind==sdk::Kind::Mesh)require(c.fields[f]==NoIndex||c.fields[f]<s.rendering.meshes.size(),"Invalid mesh index");if(d.fields[f].kind==sdk::Kind::Material)require(c.fields[f]==NoIndex||c.fields[f]<s.rendering.materials.size(),"Invalid material index");}}
        for(const auto& c:e.components){const auto& d=registry->component(c.type).descriptor;for(uint32_t i=0;i<d.requiresCount;++i)require(types.count(sdk::id(d.requiresTypes[i]))!=0,"Missing component dependency");}
    }
    std::vector<int> depths(s.entities.size());std::function<int(uint32_t,int)> visit=[&](uint32_t i,int recursion){require(recursion<=128,"Hierarchy too deep");if(state[i]==2)return depths[i];require(state[i]!=1,"Hierarchy cycle");state[i]=1;int depth=1;if(s.entities[i].parent!=NoIndex)depth=visit(s.entities[i].parent,recursion+1)+1;require(depth<=128,"Hierarchy too deep");state[i]=2;return depths[i]=depth;};for(uint32_t i=0;i<s.entities.size();++i)visit(i,0);
    std::set<std::string> disabled;for(const auto& name:s.disabledSystems){registry->system(name);require(disabled.insert(name).second,"Duplicate disabled System");}
    require(s.bindings.size()<=512,"Too many bindings");std::set<uint32_t> bound;for(const auto& b:s.bindings){require(b.entity<s.entities.size()&&findValue(s.entities[b.entity],b.component)&&b.node<s.hud.nodes.size()&&b.node!=0,"Invalid numeric binding");require(bound.insert(b.node).second,"Duplicate bound node");number(b.amount,-1000000,1000000);require(b.kind>=BindingKind::NumberText&&b.kind<=BindingKind::NumberChange,"Unknown binding kind");const auto& d=registry->component(b.component).descriptor;auto numeric=[&](uint32_t f){return f<d.fieldCount&&(d.fields[f].kind==sdk::Kind::Float||d.fields[f].kind==sdk::Kind::Int);};require(numeric(b.field)&&(b.maximum==NoIndex||numeric(b.maximum)),"Invalid numeric binding field");require(b.kind!=BindingKind::NumberBar||b.maximum!=NoIndex,"Bar needs maximum field");const auto type=s.hud.nodes[b.node].kind;require(b.kind!=BindingKind::NumberText||type==UiKind::Label,"Text binding needs label");require(b.kind!=BindingKind::NumberBar||type==UiKind::Panel,"Bar binding needs panel");require(b.kind!=BindingKind::NumberChange||type==UiKind::Button,"Change binding needs button");}
}
std::vector<uint8_t> encodeScene(const CompiledScene& s){
    validateSceneBinary(s);std::vector<uint8_t> p;putFloat(p,s.width);putFloat(p,s.height);put(p,s.background);put64(p,s.schema);putString(p,s.module);put(p,uint32_t(s.entities.size()));
    for(const auto& e:s.entities){putString(p,e.id);put(p,e.parent);put(p,uint32_t(e.components.size()));for(const auto& c:e.components){put64(p,c.type);put(p,c.version);put(p,uint32_t(c.fields.size()));for(auto f:c.fields)put(p,f);}}
    put(p,uint32_t(s.disabledSystems.size()));for(const auto& name:s.disabledSystems)putString(p,name);
    put(p,uint32_t(s.bindings.size()));for(const auto& b:s.bindings){put(p,b.entity);put(p,b.node);put(p,uint32_t(b.kind));putFloat(p,b.amount);put64(p,b.component);put(p,b.field);put(p,b.maximum);}
    for(const auto* ui:{&s.imageBank,&s.hud}){auto bytes=encodeUi(*ui);put(p,uint32_t(bytes.size()));p.insert(p.end(),bytes.begin(),bytes.end());}
    auto render=encodeRenderAssets(s.rendering);put(p,uint32_t(render.size()));p.insert(p.end(),render.begin(),render.end());
    require(p.size()<=192*1024*1024,"Scene binary exceeds limit");std::vector<uint8_t> result={'G','M','E','C'};put(result,3);put(result,checksum(p));result.insert(result.end(),p.begin(),p.end());return result;
}
CompiledScene decodeScene(std::span<const uint8_t> bytes){
    require(bytes.size()>=40&&bytes.size()<=192*1024*1024+12,"Invalid scene size");require(bytes[0]=='G'&&bytes[1]=='M'&&bytes[2]=='E'&&bytes[3]=='C',"Expected compiled .gmscene");Reader r{bytes,4};auto version=r.u32();require(version==2||version==3,"Expected scene binary v2/v3; rebuild source assets");auto hash=r.u32();require(hash==checksum(bytes.subspan(12)),"Scene checksum mismatch");CompiledScene s;s.width=r.f32();s.height=r.f32();s.background=r.u32();s.schema=r.u64();s.module=r.string();require(gameRegistry()->compatible(s.schema)&&s.module==gameRegistry()->name(),"Scene/module schema mismatch; rebuild project assets");auto count=r.u32();require(count<=100000,"Invalid entity count");s.entities.resize(count);size_t total=0;
    for(auto& e:s.entities){e.id=r.string();e.parent=r.u32();auto n=r.u32();require(n<=256,"Invalid component count");for(uint32_t i=0;i<n;++i){ComponentValue c;c.type=r.u64();c.version=r.u32();auto fields=r.u32();require(fields<=128&&(total+=fields)<=2000000,"Invalid field count");for(uint32_t f=0;f<fields;++f)c.fields.push_back(r.u32());e.components.push_back(std::move(c));}}
    auto disabled=r.u32();require(disabled<=gameRegistry()->systems().size(),"Invalid System count");for(uint32_t i=0;i<disabled;++i)s.disabledSystems.push_back(r.string());auto binds=r.u32();require(binds<=512,"Invalid binding count");for(uint32_t i=0;i<binds;++i){SceneBinding b;b.entity=r.u32();b.node=r.u32();b.kind=BindingKind(r.u32());b.amount=r.f32();b.component=r.u64();b.field=r.u32();b.maximum=r.u32();s.bindings.push_back(b);}auto images=r.u32();s.imageBank=decodeUi(r.block(images));auto hud=r.u32();s.hud=decodeUi(r.block(hud));if(version>=3){auto size=r.u32();s.rendering=decodeRenderAssets(r.block(size));}require(r.offset==bytes.size(),"Unexpected scene trailing data");validateSceneBinary(s);return s;
}
CompiledScene loadScene(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary|std::ios::ate);require(bool(in),"Cannot read scene");auto size=in.tellg();require(size>=0&&size<=192*1024*1024+12,"Scene file too large");in.seekg(0);std::vector<uint8_t> bytes(size_t(size),uint8_t{});in.read(reinterpret_cast<char*>(bytes.data()),size);require(bool(in),"Scene read failed");return decodeScene(bytes);}
void saveScene(const std::filesystem::path& path,const CompiledScene& scene){auto bytes=encodeScene(scene);if(!path.parent_path().empty())std::filesystem::create_directories(path.parent_path());auto temp=path;temp+="."+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".tmp";try{std::ofstream out(temp,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));out.flush();require(bool(out),"Scene write failed");out.close();
#ifdef _WIN32
    require(MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0,"Scene replacement failed");
#else
    std::filesystem::rename(temp,path);
#endif
    }catch(...){std::error_code ec;std::filesystem::remove(temp,ec);throw;}}
}
