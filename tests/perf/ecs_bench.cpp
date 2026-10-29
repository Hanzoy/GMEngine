#include <flecs.h>
#include <entt-3.16/entt.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#endif
struct Position {float x,y;};
struct Velocity {float x,y;};
struct Parent {uint32_t index;};
struct Marker {};
struct A {};struct B {};struct C {};struct D {};struct E {};
using Clock=std::chrono::steady_clock;
double ms(Clock::time_point t) {return std::chrono::duration<double,std::milli>(Clock::now()-t).count();}
volatile double observed=0;
template<class Fn> void measure(const std::string& backend,size_t n,const char* task,Fn fn) {
    std::vector<double> samples;double check=0;
    for(int i=0;i<110;++i) {auto begin=Clock::now();check+=fn();double elapsed=ms(begin);if(i>=10) samples.push_back(elapsed);}
    observed=check;std::sort(samples.begin(),samples.end());
    if(!std::isfinite(check)) throw std::runtime_error("Invalid checksum");
    std::cout<<backend<<','<<n<<','<<task<<','<<samples[50]<<','<<samples[94]<<','<<samples[98]<<','<<check<<'\n';
}
int main(int argc,char** argv) {
    try {
        const std::string backend=argc>1?argv[1]:"flecs";
        const size_t n=argc>2?std::stoul(argv[2]):10000;
        if(n<100 || n>1000000 || (backend!="flecs" && backend!="entt")) return 2;
        std::cout<<"backend,entities,workload,median_ms,p95_ms,p99_ms,checksum\n";
        auto load=Clock::now();double loadMs=0;
        if(backend=="flecs") {
            flecs::world world;std::vector<flecs::entity> ids;ids.reserve(n);
            for(size_t i=0;i<n;++i) ids.push_back(world.entity().set<Position>({float(i%100),0}).set<Velocity>({1,2}).set<Parent>({i?uint32_t((i-1)/4):0}));
            auto query=world.query_builder<Position,const Velocity>().cached().build();loadMs=ms(load);
            measure(backend,n,"update",[&]{query.each([](Position& p,const Velocity& v){p.x+=v.x*.01f;p.y+=v.y*.01f;});return ids.back().get<Position>().y;});
            if(std::abs(ids.back().get<Position>().y-2.2f)>.0001f) throw std::runtime_error("Movement mismatch");
            measure(backend,n,"random_read",[&]{double sum=0;for(size_t i=0;i<n;++i) sum+=ids[(i*7919)%n].get<Position>().x;return sum;});
            measure(backend,n,"churn_10pct",[&]{world.defer_begin();for(size_t i=0;i<n/10;++i) ids[i].add<Marker>();world.defer_end();world.defer_begin();for(size_t i=0;i<n/10;++i) ids[i].remove<Marker>();world.defer_end();return double(n/10);});
            measure(backend,n,"spawn_destroy_10pct",[&]{std::vector<flecs::entity> extra;extra.reserve(n/10);for(size_t i=0;i<n/10;++i) extra.push_back(world.entity().set<Position>({1,2}).set<Velocity>({3,4}));for(auto e:extra) e.destruct();return double(extra.size());});
            // Same explicit parent-index representation in both libraries isolates ECS access cost.
            measure(backend,n,"hierarchy",[&]{std::vector<float> worldX(n);for(size_t i=0;i<n;++i) worldX[i]=ids[i].get<Position>().x+(i?worldX[ids[i].get<Parent>().index]:0);return double(worldX.back());});
            for(size_t i=0;i<n;++i) {if(i&1) ids[i].add<A>();if(i&2) ids[i].add<B>();if(i&4) ids[i].add<C>();if(i&8) ids[i].add<D>();if(i&16) ids[i].add<E>();}
            measure(backend,n,"update_32_combinations",[&]{query.each([](Position& p,const Velocity& v){p.x+=v.x*.01f;p.y+=v.y*.01f;});return ids.back().get<Position>().y;});
            for(auto e:ids) e.destruct();
            if(query.count()!=0 || ids.back().is_alive()) throw std::runtime_error("Lifecycle mismatch");
        } else {
            entt::registry world;std::vector<entt::entity> ids;ids.reserve(n);
            for(size_t i=0;i<n;++i) {auto e=world.create();ids.push_back(e);world.emplace<Position>(e,float(i%100),0.f);world.emplace<Velocity>(e,1.f,2.f);world.emplace<Parent>(e,i?uint32_t((i-1)/4):0);}
            auto group=world.group<Position,Velocity>();loadMs=ms(load);
            measure(backend,n,"update",[&]{group.each([](Position& p,const Velocity& v){p.x+=v.x*.01f;p.y+=v.y*.01f;});return world.get<Position>(ids.back()).y;});
            if(std::abs(world.get<Position>(ids.back()).y-2.2f)>.0001f) throw std::runtime_error("Movement mismatch");
            measure(backend,n,"random_read",[&]{double sum=0;for(size_t i=0;i<n;++i) sum+=world.get<Position>(ids[(i*7919)%n]).x;return sum;});
            measure(backend,n,"churn_10pct",[&]{for(size_t i=0;i<n/10;++i) world.emplace<Marker>(ids[i]);for(size_t i=0;i<n/10;++i) world.remove<Marker>(ids[i]);return double(n/10);});
            measure(backend,n,"spawn_destroy_10pct",[&]{std::vector<entt::entity> extra;extra.reserve(n/10);for(size_t i=0;i<n/10;++i) {auto e=world.create();extra.push_back(e);world.emplace<Position>(e,1.f,2.f);world.emplace<Velocity>(e,3.f,4.f);}for(auto e:extra) world.destroy(e);return double(extra.size());});
            measure(backend,n,"hierarchy",[&]{std::vector<float> worldX(n);for(size_t i=0;i<n;++i) worldX[i]=world.get<Position>(ids[i]).x+(i?worldX[world.get<Parent>(ids[i]).index]:0);return double(worldX.back());});
            for(size_t i=0;i<n;++i) {if(i&1) world.emplace<A>(ids[i]);if(i&2) world.emplace<B>(ids[i]);if(i&4) world.emplace<C>(ids[i]);if(i&8) world.emplace<D>(ids[i]);if(i&16) world.emplace<E>(ids[i]);}
            measure(backend,n,"update_32_combinations",[&]{group.each([](Position& p,const Velocity& v){p.x+=v.x*.01f;p.y+=v.y*.01f;});return world.get<Position>(ids.back()).y;});
            for(auto e:ids) world.destroy(e);
            if(!group.empty() || world.valid(ids.back())) throw std::runtime_error("Lifecycle mismatch");
        }
        std::cout<<backend<<','<<n<<",initial_load,"<<loadMs<<','<<loadMs<<','<<loadMs<<",0\n";
#ifdef _WIN32
        PROCESS_MEMORY_COUNTERS_EX mem{};GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&mem),sizeof(mem));
        std::cerr<<"peak_working_set_bytes="<<mem.PeakWorkingSetSize<<" peak_commit_bytes="<<mem.PeakPagefileUsage<<'\n';
#endif
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
