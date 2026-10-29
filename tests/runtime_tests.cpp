#include "ui_compiler.hpp"
#include "runtime_layout.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <fstream>

void check(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
template<class F> void rejects(F fn,const char* message) {bool rejected=false;try {fn();}catch(const std::exception&) {rejected=true;}check(rejected,message);}
int main() {
    const auto dir=std::filesystem::temp_directory_path()/("gm-binary-tests-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto file=dir/"counter.gmui";
    try {
        auto source=gm::readJson(std::filesystem::path(GM_SOURCE_DIR)/"tests/fixtures/ui/counter.ui.json");
        const auto asset=gm::compileUi(source);const auto bytes=gm::encodeUi(asset);
        check(bytes==gm::encodeUi(asset),"Compiler is not deterministic");
        check(gm::encodeUi(gm::decodeUi(bytes))==bytes,"Binary roundtrip changed data");
        check(std::string(bytes.begin(),bytes.end()).find("Counter Card")==std::string::npos,"Editor name leaked into runtime resource");
        for(size_t length:{size_t(0),size_t(23),bytes.size()-1}) rejects([&]{gm::decodeUi(std::span(bytes).first(length));},"Truncated resource accepted");
        auto corrupt=bytes;corrupt.back()^=0x80;rejects([&]{gm::decodeUi(corrupt);},"Checksum corruption accepted");
        corrupt=bytes;corrupt[4]=99;rejects([&]{gm::decodeUi(corrupt);},"Unknown version accepted");
        auto bad=asset;bad.nodes[1].parent=1;rejects([&]{gm::encodeUi(bad);},"Parent cycle accepted");
        bad=asset;bad.nodes[1].subtreeEnd=2;rejects([&]{gm::encodeUi(bad);},"Broken subtree accepted");
        bad=asset;bad.actions[0].target=999;rejects([&]{gm::encodeUi(bad);},"Bad target index accepted");
        auto invalid=source;gm::findNode(invalid["root"],"add")->at("action")["id"]="missing";
        rejects([&]{gm::compileUi(invalid);},"Missing source reference accepted");
        invalid=source;gm::findNode(invalid["root"],"add")->at("action")["op"]="save";
        rejects([&]{gm::compileUi(invalid);},"Editor command accepted at runtime");
        gm::saveUi(file,asset);gm::RuntimeDocument runtime(file),other(file);
        auto count=[&]{return runtime.nodes()[4].text;};
        gm::RuntimeLayout layout;layout.update(runtime,960,600);
        check(layout.hit(300,360)==5,"Button hit test failed");const auto builds=layout.rebuilds();
        check(runtime.activate(5)==gm::ActionResult::Applied && runtime.activate(5)==gm::ActionResult::Applied && count()=="2","Increment failed");
        layout.update(runtime,960,600);check(layout.rebuilds()==builds,"Text change rebuilt layout");
        check(other.nodes()[4].text=="0" && gm::encodeUi(gm::loadUi(file))==bytes,"Runtime state leaked to file or other instance");
        check(runtime.activate(6)==gm::ActionResult::Applied && count()=="0","Reset failed");
        check(runtime.activate(999)==gm::ActionResult::NoAction,"Invalid index activated");
        {std::ofstream out(file,std::ios::binary);out<<"invalid";}
        rejects([&]{runtime.reload();},"Invalid reload accepted");check(count()=="0","Failed reload lost state");
        gm::saveUi(file,asset);runtime.reload();
        bad=asset;bad.actions[1].op=gm::UiOp::SetVisible;bad.actions[1].target=1;bad.actions[1].packed=0;
        gm::RuntimeDocument hidden(bad);check(hidden.activate(6)==gm::ActionResult::Applied && hidden.activate(5)==gm::ActionResult::Hidden,"Parent visibility did not propagate");
        layout.update(hidden,960,600);check(layout.hit(300,360)==gm::NoIndex,"Hidden button remains hittable");
        bad=asset;bad.actions[1].op=gm::UiOp::SetX;bad.actions[1].target=1;bad.actions[1].number=320;
        gm::RuntimeDocument moved(bad);gm::RuntimeLayout movedLayout;movedLayout.update(moved,960,600);moved.activate(6);movedLayout.update(moved,960,600);
        check(movedLayout.boxes()[5].x==356 && movedLayout.rebuilds()==2,"Parent move failed to rebuild descendant geometry");
        bad=asset;bad.nodes[1].bounds.w=100;gm::RuntimeDocument clipped(bad);gm::RuntimeLayout clipLayout;clipLayout.update(clipped,960,600);
        check(clipLayout.hit(300,360)==5 && clipLayout.hit(400,360)==gm::NoIndex,"Button clip bounds were ignored");
        bad=asset;bad.nodes[4].text="999999999";bad.nodes[4].integerValue=999999999;gm::RuntimeDocument limit(bad);
        check(limit.activate(5)==gm::ActionResult::Applied && limit.activate(5)==gm::ActionResult::OutOfRange && limit.nodes()[4].text=="1000000000","Overflow was not atomic");
        bad=asset;bad.actions[1].text="not numeric";bad.actions[1].packed=0;gm::RuntimeDocument text(bad);text.activate(6);
        check(text.activate(5)==gm::ActionResult::InvalidCounter && text.nodes()[4].text=="not numeric","Non-numeric transition failed");
        layout.update(runtime,480,300);check(layout.hit(150,180)==5,"Scaled hit test failed");
        std::filesystem::remove_all(dir);std::cout<<"PASS: deterministic compilation, binary validation, index actions, atomic reload, independent state, visibility, clipping, cached layout and overflow\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<"\nTest files: "<<dir<<'\n';return 1;}
}
