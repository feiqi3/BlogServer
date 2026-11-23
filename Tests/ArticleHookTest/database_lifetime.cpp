#include "DAO/DataBaseOperation.h"
#include "Server/server.h"
#include "Async/FJobPool.h"
#include "FEventLoop.h"
#include "FEPollListener.h"
#include "FSocket.h"
#include <sqlite3.h>
#include <atomic>
#include <future>
#include <thread>
#include <iostream>
namespace {
std::atomic_int opened=0,closed=0,finalized=0,privateOpened=0;
}
extern "C" int __real_sqlite3_open_v2(const char*,sqlite3**,int,const char*);
extern "C" int __wrap_sqlite3_open_v2(const char* file,sqlite3** db,int flags,const char* vfs){
    int result=__real_sqlite3_open_v2(file,db,flags,vfs);if(result==SQLITE_OK){++opened;if(flags&SQLITE_OPEN_PRIVATECACHE)++privateOpened;}return result;
}
extern "C" int __real_sqlite3_close_v2(sqlite3*);
extern "C" int __wrap_sqlite3_close_v2(sqlite3* db){int result=__real_sqlite3_close_v2(db);if(db&&result==SQLITE_OK)++closed;return result;}
extern "C" int __real_sqlite3_finalize(sqlite3_stmt*);
extern "C" int __wrap_sqlite3_finalize(sqlite3_stmt* statement){if(statement)++finalized;return __real_sqlite3_finalize(statement);}
void Blog::Server::CurThreadCleanCallback(std::function<void()> callback){
    if(auto* loop=Fei::FEventLoop::getCurrentLoop())loop->addLoopCloseCallback(std::move(callback));
}
int query(int value){
    auto* db=Blog::DatabaseOperation::instance();
    thread_local auto statement=[] {
        auto result=Blog::DatabaseOperation::instance()->Prepare("SELECT ?");
        Blog::DatabaseOperation::addThreadCleanDBData(result);return result;
    }();
    db->ReExec(statement,value);
    if(!statement->step())throw std::runtime_error("No database row");
    auto result=statement->getInteger(0);
    // Consume the result to release SQLite read locks before returning to the pool.
    if(statement->step())throw std::runtime_error("Unexpected extra row");
    return result;
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    Fei::FeiInit();Fei::LoggerConfig config;config.showLvl=Fei::lvl::err;config.saveLvl=Fei::lvl::off;new Fei::Logger(config);
    int result=0;
    {
        Blog::DatabaseOperation database;database.LoadDB(argv[1]);
        const int initialOpens=opened;
        Fei::FEventLoop loop(std::make_unique<Fei::FEPollListener>());
        std::thread owner([&]{loop.Loop();});
        std::promise<void> finished;auto done=finished.get_future();std::atomic_int errors=0;int count=0;
        {
            Fei::Async::JobPool workers({4,64});
            for(int index=0;index<40;++index)workers.submit([index](auto)->std::any{
                Blog::DatabaseOperation::instance()->usePrivateCacheForCurrentThread();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));return query(index);
            },[&,index](auto value){
                if(!value.ok()||std::any_cast<int>(*value.value)!=index)++errors;
                if(++count==40)finished.set_value();
            },&loop);
            if(done.wait_for(std::chrono::seconds(10))!=std::future_status::ready)std::terminate();
            workers.shutdown();
            if(opened!=closed){std::cerr<<"Worker connections leaked: "<<opened<<"/"<<closed<<'\n';result=1;}
        }
        std::promise<void> queried;auto future=queried.get_future();
        loop.AddTask([&]{if(query(77)!=77)++errors;queried.set_value();});future.get();
        loop.Quit();owner.join();
        if(errors||opened!=closed||finalized<5||privateOpened!=opened-initialOpens-1){std::cerr<<"ORM lifetime mismatch "<<opened<<'/'<<closed<<'/'<<finalized<<" private="<<privateOpened<<'\n';result=1;}
    }
    delete Fei::Logger::instance();Fei::FeiUnInit();
    if(!result)std::cout<<"PASS ORM: 40 worker jobs, native loop cleanup, balanced SQLite opens/closes and statement finalization\n";
    return result;
}
