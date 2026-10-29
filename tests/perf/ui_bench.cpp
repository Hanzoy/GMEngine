#include "legacy_runtime.hpp"
#include "ui_compiler.hpp"
#include "runtime_layout.hpp"
#include <chrono>
#include <iostream>
#include <iomanip>
#include <algorithm>

using Clock=std::chrono::steady_clock;
template<class F> double timeUs(int iterations,F fn) {
    const auto start=Clock::now();for(int i=0;i<iterations;++i) fn();
    return std::chrono::duration<double,std::micro>(Clock::now()-start).count()/iterations;
}
template<class F> double median(F fn) {std::vector<double> v;for(int r=0;r<5;++r) v.push_back(fn());std::sort(v.begin(),v.end());return v[2];}
int main() {
    try {
        auto dir=std::filesystem::path(GM_BINARY_DIR)/"perf-fixtures";std::filesystem::create_directories(dir);
        std::cout<<"nodes,json_bytes,binary_bytes,legacy_load_us,binary_load_us,legacy_action_us,native_action_us\n"<<std::fixed<<std::setprecision(3);
        for(int count:{9,128,512}) {
            auto source=gm::readJson(std::filesystem::path(GM_SOURCE_DIR)/"tests/fixtures/ui/counter.ui.json");
            auto& children=source["root"]["children"][0]["children"];
            for(int n=9;n<count;++n) children.push_back({{"id","filler_"+std::to_string(n)},{"type","label"},{"x",0},{"y",0},{"w",100},{"h",20},{"text","static"}});
            auto json=dir/(std::to_string(count)+".ui.json"),binary=dir/(std::to_string(count)+".gmui");
            gm::writeJsonAtomic(json,source);gm::saveUi(binary,gm::compileUi(source));
            const auto oldLoad=median([&]{return timeUs(30,[&]{gm::LegacyRuntimeDocument state(json);if(state.data()["root"].empty()) throw std::runtime_error("Invalid legacy state");});});
            const auto newLoad=median([&]{return timeUs(30,[&]{gm::RuntimeDocument state(binary);if(state.nodes().empty()) throw std::runtime_error("Invalid compiled state");});});
            gm::LegacyRuntimeDocument old(json);gm::RuntimeDocument current(binary);
            old.activate("add");current.activate(5);
            const auto oldAction=median([&]{return timeUs(1000,[&]{if(!old.activate("add")["ok"].get<bool>()) throw std::runtime_error("Legacy action failed");});});
            const auto newAction=median([&]{return timeUs(100000,[&]{if(current.activate(5)!=gm::ActionResult::Applied) throw std::runtime_error("Native action failed");});});
            if(current.nodes()[4].text!="500001") throw std::runtime_error("Benchmark result mismatch");
            std::cout<<count<<','<<std::filesystem::file_size(json)<<','<<std::filesystem::file_size(binary)<<','<<oldLoad<<','<<newLoad<<','<<oldAction<<','<<newAction<<'\n';
        }
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
