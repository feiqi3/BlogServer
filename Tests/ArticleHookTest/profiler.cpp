#include "Service/ProfilerService.h"
#include "FEventLoop.h"
#include "FEPollListener.h"
#include "FSocket.h"
#include "FLogger.h"
#include <filesystem>
#include <fstream>
#include <thread>
#include <iostream>
using namespace std::chrono_literals;
int main(int argc,char** argv){
    if(argc!=2)return 2;
    Fei::FeiInit();Fei::LoggerConfig config;config.showLvl=Fei::lvl::err;config.saveLvl=Fei::lvl::off;new Fei::Logger(config);
    int status=0;
    {
        auto profiler=Blog::ProfilerService::create(argv[1]);
        Fei::FProfileLabel label;label.name="article.OnRequest";label.category="Script";label.script="blog";label.generation=1;
        auto first=Fei::FProfiler::registerLabel(label);label.generation=2;auto second=Fei::FProfiler::registerLabel(label);
        profiler->version("blog",1,"source-one");
        for(int i=1;i<=10;++i)Fei::FProfiler::record(first,std::chrono::microseconds(i*100),i==10);
        Fei::FProfiler::record(second,2000us);Fei::FProfiler::flush();
        auto data=profiler->status();
        if(data["rows"].size()!=2)status=1;
        for(const auto& row:data["rows"])if(row["generation"]==1){
            if(row["calls"]!=10||row["errors"]!=1||row["averageUs"]!=550||row["maxUs"]!=1000||row["sourceVersion"]!="source-one")status=1;
        }
        profiler->shutdown();
        std::filesystem::path directory=data["directory"].get<std::string>();
        nlohmann::json summary;std::ifstream(directory/"summary.json")>>summary;
        if(summary["rows"].size()!=2||!profiler->status()["writeError"].get<std::string>().empty())status=1;
        std::ifstream samples(directory/"samples.jsonl");std::string line;int count=0;
        while(std::getline(samples,line)){auto sample=nlohmann::json::parse(line);if(!sample.contains("elapsedUs"))status=1;++count;}
        if(count!=11)status=1;
        // A write failure must be observable and must not deadlock shutdown.
        auto failed=Blog::ProfilerService::create(argv[1]);
        auto failedPath=std::filesystem::path(failed->status()["directory"].get<std::string>());
        std::filesystem::create_directory(failedPath/"summary.tmp");
        Fei::FProfiler::record(first,1000us);failed->shutdown();
        if(failed->status()["writeError"].get<std::string>().empty())status=1;
    }
    delete Fei::Logger::instance();Fei::FeiUnInit();
    if(!status)std::cout<<"PASS profiling: per-version counts, average/max microseconds, errors, samples/summary persistence, final flush and I/O failure reporting\n";
    return status;
}
