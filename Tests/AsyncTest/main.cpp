#include "FLogger.h"
#include "FSocket.h"
#include <exception>
#include <iostream>
#include <string>

void runInfrastructureTests();
void runResponseParserTests();
void runJobGraphTests();
void runHttpClientTests();
void runEndToEndTests();

int main(int argc, char** argv) {
    Fei::FeiInit();
    Fei::LoggerConfig config;
    config.showLvl = Fei::lvl::warn;
    config.saveLvl = Fei::lvl::warn;
    config.saveFileName = "async-tests.log";
    new Fei::Logger(config);
    int status = 0;
    try {
        std::string group = argc > 1 ? argv[1] : "all";
        if (group == "all" || group == "infra") { runInfrastructureTests(); std::cout << "PASS infrastructure\n"; }
        if (group == "all" || group == "parser") { runResponseParserTests(); std::cout << "PASS response parser\n"; }
        if (group == "all" || group == "jobs") { runJobGraphTests(); std::cout << "PASS jobs and graph\n"; }
        if (group == "all" || group == "http") { runHttpClientTests(); std::cout << "PASS HTTP client\n"; }
        if (group == "all" || group == "integration") { runEndToEndTests(); std::cout << "PASS end-to-end\n"; }
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n'; status = 1;
    }
    delete Fei::Logger::instance();
    Fei::FeiUnInit();
    return status;
}
