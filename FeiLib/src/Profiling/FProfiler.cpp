#include "Profiling/FProfiler.h"
#include <algorithm>
#include <array>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace Fei {
namespace {
using Clock = std::chrono::steady_clock;
constexpr std::size_t bufferSize = 1024, labelLimit = 4096;
constexpr ProfileId rejectedSite = static_cast<ProfileId>(-1);
std::uint64_t now() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
}
struct Buffer {
    std::array<FProfileSample,bufferSize> data{};
    alignas(64) std::atomic_uint64_t written{0};
    alignas(64) std::atomic_uint64_t read{0};
    std::atomic_uint64_t dropped{0};
    std::atomic_bool busy{false}, exited{false};
    std::uint32_t thread = 0;
};
struct State {
    std::mutex control, mutex;
    std::condition_variable wake, flushed;
    std::thread collector;
    std::atomic_uint64_t session{0};
    std::atomic_uint32_t labelCount{0};
    std::vector<FProfileLabel> labels;
    std::vector<FProfileRow> rows;
    std::vector<std::shared_ptr<Buffer>> buffers;
    std::uint32_t nextThread = 0;
    std::uint64_t origin = 0, dropped = 0, droppedThreads = 0, rejected = 0, failed = 0, requested = 0, completed = 0;
    bool stopping = false;
    FProfilerOptions options;
    FProfiler::Sink sink;
    std::string sinkError;
    ~State() {
        if(collector.joinable()){
            { std::lock_guard lock(mutex); stopping=true; }
            wake.notify_one();collector.join();
        }
    }
    FProfileSnapshot snapshot() const {
        FProfileSnapshot result;
        result.enabled=FProfiler::enabled();result.session=session.load();result.droppedSamples=dropped;
        for(const auto& buffer:buffers)result.droppedSamples+=buffer->dropped.load(std::memory_order_relaxed);
        result.droppedThreads=droppedThreads;result.rejectedLabels=rejected;result.failedSamples=failed;result.sinkError=sinkError;
        for(std::size_t i=0;i<rows.size();++i)if(rows[i].calls){auto row=rows[i];row.label=labels[i];result.rows.push_back(std::move(row));}
        return result;
    }
};
State& state(){static State value;return value;}
struct Local {
    std::uint64_t session=0;
    std::shared_ptr<Buffer> buffer;
    ~Local(){if(buffer)buffer->exited.store(true,std::memory_order_release);}
};
thread_local Local local;

Buffer* producer(State& value,std::uint64_t session){
    if(local.session==session)return local.buffer.get();
    if(local.buffer)local.buffer->exited.store(true,std::memory_order_release);
    local.buffer.reset();local.session=session;
    std::lock_guard lock(value.mutex);
    if(!FProfiler::enabled()||value.session.load()!=session)return nullptr;
    if(value.buffers.size()>=value.options.maxThreads){++value.droppedThreads;return nullptr;}
    auto buffer=std::make_shared<Buffer>();buffer->thread=++value.nextThread;
    value.buffers.push_back(buffer);local.buffer=std::move(buffer);return local.buffer.get();
}
void append(State& value,ProfileId id,std::uint64_t session,std::uint64_t elapsed,std::uint64_t end,bool failed){
    auto* buffer=producer(value,session);if(!buffer)return;
    // Per-thread handshake makes stop's final drain safe without a global hot-path lock.
    buffer->busy.store(true,std::memory_order_seq_cst);
    if(!FProfiler::enabled()||value.session.load(std::memory_order_seq_cst)!=session){buffer->busy.store(false,std::memory_order_release);return;}
    auto write=buffer->written.load(std::memory_order_relaxed);
    if(write-buffer->read.load(std::memory_order_acquire)<bufferSize){
        buffer->data[write%bufferSize]={id,buffer->thread,elapsed,end>=value.origin?end-value.origin:0,failed};
        buffer->written.store(write+1,std::memory_order_release);
    }else buffer->dropped.fetch_add(1,std::memory_order_relaxed);
    buffer->busy.store(false,std::memory_order_release);
}
void collect(State& value,bool final){
    FProfileBatch batch;
    {
        std::lock_guard lock(value.mutex);
        value.rows.resize(value.labels.size());
        for(auto it=value.buffers.begin();it!=value.buffers.end();){
            auto& buffer=**it;
            if(final)while(buffer.busy.load(std::memory_order_seq_cst))std::this_thread::yield();
            auto read=buffer.read.load(std::memory_order_relaxed),end=buffer.written.load(std::memory_order_acquire);
            while(read<end){
                auto sample=buffer.data[read++%bufferSize];
                if(sample.id&&sample.id<=value.rows.size()){
                    auto& row=value.rows[sample.id-1];row.id=sample.id;++row.calls;if(sample.failed)++row.errors;
                    row.totalNs+=sample.elapsedNs;row.maxNs=std::max(row.maxNs,sample.elapsedNs);batch.samples.push_back(sample);
                }
            }
            buffer.read.store(read,std::memory_order_release);
            if((final||buffer.exited.load(std::memory_order_acquire))&&read==buffer.written.load(std::memory_order_acquire)){
                value.dropped+=buffer.dropped.load();it=value.buffers.erase(it);
            }else ++it;
        }
        batch.summary=value.snapshot();
    }
    if(value.sink){
        try{value.sink(batch);std::lock_guard lock(value.mutex);value.sinkError.clear();}
        catch(const std::exception& error){std::lock_guard lock(value.mutex);value.sinkError=error.what();value.failed+=batch.samples.size();}
        catch(...){std::lock_guard lock(value.mutex);value.sinkError="Profiling sink failed";value.failed+=batch.samples.size();}
    }
}
void run(State& value){
    for(;;){
        std::uint64_t target;bool final;
        {
            std::unique_lock lock(value.mutex);
            value.wake.wait_for(lock,value.options.interval,[&]{return value.stopping||value.requested>value.completed;});
            target=value.requested;final=value.stopping;
        }
        collect(value,final);
        {std::lock_guard lock(value.mutex);value.completed=target;value.flushed.notify_all();}
        if(final)return;
    }
}
}
std::atomic_bool FProfiler::enabled_{false};
bool FProfiler::start(FProfilerOptions options,Sink sink){
#if !FEILIB_ENABLE_PROFILING
    (void)options;(void)sink;return false;
#else
    if(options.interval<std::chrono::milliseconds(1)||options.interval>std::chrono::seconds(10)||!options.maxThreads||options.maxThreads>1024)throw std::invalid_argument("Invalid profiling options");
    auto& value=state();std::lock_guard control(value.control);std::lock_guard lock(value.mutex);
    if(value.collector.joinable())return false;
    value.options=options;value.sink=std::move(sink);value.buffers.clear();value.rows.clear();
    value.origin=now();value.dropped=value.droppedThreads=value.failed=value.requested=value.completed=0;value.nextThread=0;value.sinkError.clear();value.stopping=false;
    value.session.fetch_add(1,std::memory_order_seq_cst);
    value.collector=std::thread([&value]{run(value);});
    enabled_.store(true,std::memory_order_seq_cst);return true;
#endif
}
void FProfiler::stop(){
    auto& value=state();std::lock_guard control(value.control);
    if(!value.collector.joinable())return;
    if(value.collector.get_id()==std::this_thread::get_id())throw std::logic_error("Stop profiler from its control thread");
    enabled_.store(false,std::memory_order_seq_cst);
    {std::lock_guard lock(value.mutex);value.stopping=true;}
    value.wake.notify_one();value.collector.join();
    {std::lock_guard lock(value.mutex);value.sink={};}
}
void FProfiler::flush(){
    auto& value=state();std::lock_guard control(value.control);if(!value.collector.joinable())return;
    if(value.collector.get_id()==std::this_thread::get_id())throw std::logic_error("Flush profiler from its control thread");
    std::unique_lock lock(value.mutex);auto target=++value.requested;value.wake.notify_one();
    value.flushed.wait(lock,[&]{return value.completed>=target;});
}
ProfileId FProfiler::registerLabel(FProfileLabel label){
    if(!enabled())return 0;
    auto& value=state();std::lock_guard lock(value.mutex);
    if(label.name.empty()||label.name.size()>256||label.file.size()>1024||label.category.size()>128||label.script.size()>128||label.sourceVersion.size()>256){++value.rejected;return 0;}
    for(std::size_t i=0;i<value.labels.size();++i){const auto& old=value.labels[i];
        if(old.name==label.name&&old.category==label.category&&old.file==label.file&&old.line==label.line&&old.script==label.script&&old.generation==label.generation)return static_cast<ProfileId>(i+1);
    }
    if(value.labels.size()>=labelLimit){++value.rejected;return 0;}
    value.labels.push_back(std::move(label));auto id=static_cast<ProfileId>(value.labels.size());value.labelCount.store(id,std::memory_order_release);return id;
}
ProfileId FProfiler::siteId(FProfileSite& site)noexcept{
    if(!enabled())return 0;
    auto id=site.cached.load(std::memory_order_relaxed);if(id)return id==rejectedSite?0:id;
    try{FProfileLabel label;label.name=site.name;label.file=site.file;label.line=site.line;
        id=registerLabel(std::move(label));site.cached.store(id?id:rejectedSite,std::memory_order_relaxed);return id;
    }catch(...){return 0;}
}
void FProfiler::sourceVersion(const std::string& script,std::uint64_t generation,const std::string& version){
    auto& value=state();std::lock_guard lock(value.mutex);
    if(version.size()>256)return;
    for(auto& label:value.labels)if(label.script==script&&label.generation==generation)label.sourceVersion=version;
}
FProfileSnapshot FProfiler::snapshot(){auto& value=state();std::lock_guard lock(value.mutex);return value.snapshot();}
void FProfiler::record(ProfileId id,std::chrono::nanoseconds elapsed,bool failed)noexcept{
    if(!enabled()||!id)return;
    try{auto& value=state();if(id>value.labelCount.load(std::memory_order_acquire))return;
        auto session=value.session.load();auto end=now();append(value,id,session,std::max<int64_t>(0,elapsed.count()),end,failed);
    }catch(...){}
}
void FProfiler::recordSession(ProfileId id,std::uint64_t session,std::uint64_t begin,bool failed)noexcept{
    if(!enabled()||!id)return;
    try{auto& value=state();if(value.session.load()!=session)return;auto end=now();append(value,id,session,end-begin,end,failed);}catch(...){}
}
FProfileSpan FProfileSpan::begin(ProfileId id)noexcept{
    FProfileSpan result;
    if(id&&FProfiler::enabled()){result.session_=state().session.load();result.begin_=now();result.id_=id;}
    return result;
}
FProfileSpan FProfileSpan::begin(FProfileSite& site)noexcept{return begin(FProfiler::siteId(site));}
FProfileSpan::FProfileSpan(FProfileSpan&& other)noexcept:id_(std::exchange(other.id_,0)),session_(other.session_),begin_(other.begin_){}
FProfileSpan& FProfileSpan::operator=(FProfileSpan&& other)noexcept{
    if(this!=&other){finish();id_=std::exchange(other.id_,0);session_=other.session_;begin_=other.begin_;}return *this;
}
void FProfileSpan::finish(bool failed)noexcept{auto id=std::exchange(id_,0);if(id)FProfiler::recordSession(id,session_,begin_,failed);}
}
