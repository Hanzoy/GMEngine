#include "document.hpp"
#include "editor_layout.hpp"
#include "editor_documents.hpp"
#include <iostream>
#include <stdexcept>
#include <chrono>
using gm::Json;
void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
int main() {
    auto dir=std::filesystem::temp_directory_path()/("gm-tests-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir); auto file=dir/"test.ui.json";
    try {
        for(const auto size:std::vector<gm::Rect>{{0,0,942,561},{0,0,1100,680},{0,0,1440,900},{0,0,1920,1080}}) {
            for(bool expanded:{false,true}) for(float side:{180.f,340.f}) {
                gm::WorkspaceLayout layout;layout.left=side;layout.right=400;layout.outputOpen=expanded;layout.arrange(size.w,size.h);
                for(const auto& [name,r]:layout.regions) {
                    check(r.w>0 && r.h>0 && r.x>=0 && r.y>=0 && r.x+r.w<=size.w+.01f && r.y+r.h<=size.h+.01f,"Dock panel escaped client bounds");
                    for(const auto& [other,b]:layout.regions) if(name!=other)
                        check(r.x+r.w<=b.x || b.x+b.w<=r.x || r.y+r.h<=b.y || b.y+b.h<=r.y,"Dock panels overlap");
                }
                check(layout.regions.at("center").w>=300,"Viewport became unusably narrow");
            }
        }
        const auto original=gm::readJson(std::filesystem::path(GM_SOURCE_DIR)/"tests/fixtures/editor-v0.1.ui.json"); gm::writeJsonAtomic(file,original);
        gm::Document doc(file);
        auto set=[&](const char* id,const char* property,Json value) { return doc.execute({{"op","set"},{"id",id},{"property",property},{"value",value}}); };
        check(set("brand","text","我的编辑器")["ok"],"Unicode edit failed");
        check(doc.dirty(),"Edit did not mark dirty");
        check(doc.execute({{"op","undo"}})["ok"] && doc.data()==original && !doc.dirty(),"Undo did not restore exact document");
        check(doc.execute({{"op","redo"}})["ok"],"Redo failed");
        const auto before=doc.data(); const auto rev=doc.revision();
        check(!set("brand","w",-2)["ok"].get<bool>() && doc.data()==before && doc.revision()==rev,"Invalid edit was not atomic");
        Json batch={{"op","batch"},{"commands",Json::array({{{"op","set"},{"id","brand"},{"property","text"},{"value","bad batch"}},{{"op","set"},{"id","missing"},{"property","x"},{"value",1}}})}};
        check(!doc.execute(batch)["ok"].get<bool>() && doc.data()==before,"Failed batch leaked an edit");
        auto conflict=set("brand","color","#xyzxyz"); check(!conflict["ok"].get<bool>(),"Invalid color accepted");
        check(!doc.execute({{"op","set"},{"id","brand"},{"property","x"},{"value",3},{"expected_revision",0}})["ok"].get<bool>(),"Stale revision accepted");
        check(!doc.execute({{"op","reload"}})["ok"].get<bool>(),"Reload discarded unsaved edits");
        check(doc.execute({{"op","save"}})["ok"] && !doc.dirty(),"Save failed");
        gm::Document reopened(file); check(reopened.data()==doc.data(),"Save/reopen mismatch");
        auto external=gm::readJson(file); external["root"]["color"]="#112233"; gm::writeJsonAtomic(file,external);
        check(set("brand","text","new text")["ok"],"Edit failed");
        check(!doc.execute({{"op","save"}})["ok"].get<bool>() && gm::readJson(file)==external,"External changes overwritten");
        check(doc.execute({{"op","reload"},{"discard",true}})["ok"] && doc.data()==external,"Explicit reload failed");
        check(!doc.execute({{"op","remove"},{"id","editor"}})["ok"].get<bool>(),"Root deletion allowed");
        auto node=Json{{"id","test_button"},{"type","button"},{"x",100},{"y",100},{"w",80},{"h",30},{"text","test"}};
        check(doc.execute({{"op","add"},{"parent","editor"},{"node",node}})["ok"],"Add failed");
        check(!doc.execute({{"op","add"},{"parent","editor"},{"node",node}})["ok"].get<bool>(),"Duplicate id accepted");
        check(doc.execute({{"op","select"},{"id","test_button"}})["ok"],"Select failed");
        check(doc.execute({{"op","remove"},{"id","test_button"}})["ok"] && doc.selected()=="editor","Selection was left dangling");
        check(doc.execute({{"op","undo"}})["ok"] && doc.selected()=="test_button","Undo failed to restore selection");
        auto created=doc.execute({{"op","create"},{"type","button"}});
        check(created["ok"] && gm::findNode(doc.data()["root"],doc.selected()),"Headless create failed");
        check(doc.execute({{"op","undo"}})["ok"] && doc.selected()=="test_button","Create undo lost selection");
        auto subtree=Json{{"id","group"},{"type","panel"},{"x",0},{"y",0},{"w",200},{"h",100},{"children",Json::array({
            {{"id","target"},{"type","label"},{"x",0},{"y",0},{"w",60},{"h",30},{"text","0"}},
            {{"id","trigger"},{"type","button"},{"x",0},{"y",40},{"w",60},{"h",30},{"action",{{"op","set"},{"id","target"},{"property","text"},{"value","1"}}}}
        })}};
        check(doc.execute({{"op","add"},{"parent","editor"},{"node",subtree}})["ok"],"Subtree setup failed");
        const auto originalTree=doc.data();
        check(doc.execute({{"op","duplicate"},{"id","group"}})["ok"],"Duplicate failed");
        const auto* copy=gm::findNode(doc.data()["root"],doc.selected());
        check(copy && copy->at("children")[1]["action"]["id"]==copy->at("children")[0]["id"] && copy->at("id")!="group","Duplicate lost internal action references");
        check(doc.execute({{"op","undo"}})["ok"] && doc.data()==originalTree,"Duplicate undo was not atomic");
        gm::writeJsonAtomic(file,original);gm::writeJsonAtomic(dir/"second.ui.json",original);
        gm::EditorDocuments tabs(file);tabs.execute({{"op","set"},{"id","brand"},{"property","text"},{"value","unsaved"}});
        tabs.open(dir/"second.ui.json");check(!tabs.dirty() && tabs.tabs().size()==2,"Second tab inherited dirty state");
        tabs.open(file);check(tabs.tabs().size()==2 && tabs.dirty(),"Reopening a tab discarded state");
        bool rejected=false;try {tabs.close();} catch(...) {rejected=true;}check(rejected && tabs.tabs().size()==2,"Close discarded unsaved tab");
        check(tabs.execute({{"op","undo"}})["ok"] && !tabs.dirty(),"Tab switch lost undo history");tabs.close();check(tabs.tabs().size()==1,"Tab close failed");
        std::cout<<"PASS: responsive panel bounds, document validation, atomic edits/batches, undo/redo, UTF-8 persistence, revision and disk conflicts, selection integrity\n";
        std::filesystem::remove_all(dir); return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<"\nTest artifacts: "<<dir<<'\n'; return 1; }
}
