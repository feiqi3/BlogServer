#include "Profiling/FProfiler.h"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <new>
#include <thread>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#include <malloc.h>
#endif
using namespace std::chrono_literals;
thread_local bool allocationsTracked=false;
thread_local unsigned allocations=0;
void* operator new(std::size_t size){if(allocationsTracked)++allocations;if(auto p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
void* operator new(std::size_t n,std::align_val_t alignment){
    if(allocationsTracked)++allocations;
    void* pointer=nullptr;
#ifdef _WIN32
    pointer=_aligned_malloc(n,static_cast<std::size_t>(alignment));
#else
    if(posix_memalign(&pointer,static_cast<std::size_t>(alignment),n))pointer=nullptr;
#endif
    if(!pointer)throw std::bad_alloc();return pointer;
}
void operator delete(void* p,std::align_val_t)noexcept{
#ifdef _WIN32
    _aligned_free(p);
#else
    std::free(p);
#endif
}
void operator delete(void* p,std::size_t,std::align_val_t a)noexcept{::operator delete(p,a);}
void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
void scope(){FeiScope("Static.Scope");}
void nested(){FeiScope("Outer");{FeiScope("Inner");}}
void rejectedScope(){FeiScope("After.Capacity");}
void throws(){FeiScope("Exception");throw std::runtime_error("expected");}
Fei::FProfileRow row(const char* name){for(auto& row:Fei::FProfiler::snapshot().rows)if(row.label.name==name)return row;throw std::runtime_error(name);}
int main(){
    using namespace Fei;
    try{
        allocationsTracked=true;auto begin=std::chrono::steady_clock::now();
        for(int i=0;i<1000000;++i)scope();
        auto off=std::chrono::steady_clock::now()-begin;allocationsTracked=false;
        require(!allocations&&!FProfiler::enabled(),"Disabled scopes allocated");
#if !FEILIB_ENABLE_PROFILING
        require(!FProfiler::start(),"Compiled-out profiler started");
        int evaluated=0;FeiScopeId(++evaluated);FeiScope(++evaluated);require(!evaluated,"Compiled-out macro evaluated its argument");
        std::cout<<"PASS compiled-out profiling: no collector, allocations or argument evaluation\n";return 0;
#else
        require(FProfiler::start(),"Cannot start");require(!FProfiler::start(),"Double start accepted");
        scope();FProfiler::flush();
        allocations=0;allocationsTracked=true;begin=std::chrono::steady_clock::now();
        for(int i=0;i<1000000;++i)scope();
        auto on=std::chrono::steady_clock::now()-begin;allocationsTracked=false;
        require(!allocations,"Warm scope allocated");FProfiler::flush();
        require(row("Static.Scope").calls+FProfiler::snapshot().droppedSamples==1000001,"Overflow accounting incorrect");
        std::chrono::nanoseconds accepted{};auto droppedBefore=FProfiler::snapshot().droppedSamples;
        for(int batch=0;batch<100;++batch){
            allocationsTracked=true;begin=std::chrono::steady_clock::now();
            for(int i=0;i<500;++i)scope();
            accepted+=std::chrono::steady_clock::now()-begin;allocationsTracked=false;FProfiler::flush();
        }
        require(!allocations&&FProfiler::snapshot().droppedSamples==droppedBefore,"Accepted scopes allocated or overflowed");
        nested();try{throws();}catch(const std::runtime_error&){}
        FProfileLabel label;label.name="Known";label.category="Test";auto id=FProfiler::registerLabel(label);
        auto asyncId=FProfiler::registerLabel({"Async"});
        {auto span=FProfileSpan::begin(asyncId);std::thread other([span=std::move(span)]()mutable{std::this_thread::sleep_for(3ms);span.finish(true);span.finish();});other.join();}
        std::vector<std::thread> workers;
        for(int n=0;n<20;++n)workers.emplace_back([id]{for(int i=0;i<100;++i)FProfiler::record(id,std::chrono::microseconds(i+1),i==99);});
        for(auto& worker:workers)worker.join();FProfiler::flush();
        auto known=row("Known");require(known.calls==2000&&known.errors==20&&known.totalNs==101000000&&known.maxNs==100000,"Concurrent aggregates incorrect");
        require(row("Inner").calls==1&&row("Outer").calls==1&&row("Exception").errors==1,"RAII scopes incorrect");
        require(row("Async").calls==1&&row("Async").errors==1&&row("Async").maxNs>=3000000,"Moved asynchronous span incorrect");
        auto late=FProfileSpan::begin(id);FProfiler::stop();
        require(FProfiler::start(),"Restart failed");late.finish();FProfiler::flush();require(FProfiler::snapshot().rows.empty(),"Old span contaminated a new session");
        scope();FProfiler::stop();require(row("Static.Scope").calls==1,"Cached ID failed after restart");
        std::atomic_bool recording=true;
        require(FProfiler::start({1ms,32}),"Cannot start stop-race test");
        workers.clear();for(int i=0;i<20;++i)workers.emplace_back([&]{while(recording)FProfiler::record(id,1us);});
        std::this_thread::sleep_for(10ms);FProfiler::stop();recording=false;for(auto& worker:workers)worker.join();
        auto stopped=FProfiler::snapshot();std::this_thread::sleep_for(2ms);require(FProfiler::snapshot().rows.size()==stopped.rows.size(),"Samples arrived after stop");
        require(FProfiler::start({100ms,1}),"Cannot start capacity test");
        FProfiler::record(id,1us);std::thread rejected([&]{FProfiler::record(id,1us);});rejected.join();FProfiler::stop();
        require(FProfiler::snapshot().droppedThreads==1&&row("Known").calls==1,"Thread capacity not bounded");
        require(FProfiler::start({100ms,1}),"Cannot start reclamation test");
        for(int i=0;i<3;++i){std::thread shortLived([&]{FProfiler::record(id,1us);});shortLived.join();FProfiler::flush();}
        FProfiler::stop();require(row("Known").calls==3&&!FProfiler::snapshot().droppedThreads,"Exited buffers were not reclaimed");
        FProfileLabel invalid;invalid.name=std::string(257,'x');
        require(FProfiler::start({},[](const auto&){throw std::runtime_error("sink rejected");}),"Cannot start failed sink");
        require(FProfiler::registerLabel(invalid)==0,"Oversized label accepted");FProfiler::record(id,1us);FProfiler::flush();FProfiler::stop();
        require(FProfiler::snapshot().sinkError=="sink rejected"&&FProfiler::snapshot().failedSamples==1,"Sink failure missing");
        require(FProfiler::start(),"Cannot start label capacity test");
        for(int i=0;i<5000;++i){FProfileLabel next;next.name="Capacity-"+std::to_string(i);if(!FProfiler::registerLabel(std::move(next)))break;}
        rejectedScope();allocations=0;allocationsTracked=true;
        for(int i=0;i<1000;++i)rejectedScope();allocationsTracked=false;
        require(!allocations&&FProfiler::snapshot().rejectedLabels>=2,"Rejected sites retried string construction");FProfiler::stop();
        std::cout<<"PASS scopes, exceptions, async moves, 20 threads, exact aggregates, bounded overflow, restart/stop, sink failures; warm allocations=0\n";
        std::cout<<"BENCH ns/scope runtime-off="<<std::chrono::duration<double,std::nano>(off).count()/1000000<<" enabled="<<std::chrono::duration<double,std::nano>(on).count()/1000000<<" (1M calls; enabled includes buffer overflow)\n";
        std::cout<<"BENCH enabled accepted-only ns/scope="<<std::chrono::duration<double,std::nano>(accepted).count()/50000<<" (50k calls, flushing excluded; allocations=0)\n";
        return 0;
#endif
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';Fei::FProfiler::stop();return 1;}
}
