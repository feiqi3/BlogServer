#include "FConfigReader.h"
#include "FLogger.h"
#include<sstream>
#include <fstream>
#include <algorithm>
#include <iostream>
#define MODULE_NAME "[Config]"
namespace {
    std::string trim(const std::string& str) {
        // 找到第一个非空白字符的位置
        size_t start = 0;
        while (start < str.size() && std::isspace(static_cast<unsigned char>(str[start]))) {
            ++start;
        }

        if (start == str.size()) {
            return "";
        }

        size_t end = str.size() - 1;
        while (end > start && std::isspace(static_cast<unsigned char>(str[end]))) {
            --end;
        }

        return str.substr(start, end - start + 1);
    }
   
    Fei::FConfigReader::Env toEnv(const std::string& str) {
        if (str == "test") {
            return Fei::FConfigReader::Env::Test;
        }
        if (str == "prod") {
            return Fei::FConfigReader::Env::Prod;
        }
        return Fei::FConfigReader::Env::Invalid;
    }


    bool isBlank(const std::string& line) {
        for (char ch : line) {
            if (!std::isspace(static_cast<unsigned char>(ch))) {
                return false;
            }
        }
        return true;
    }

    //In: a no leading/ending space line.
    std::optional<std::pair<std::string, std::string>> readCfgTerm(const std::string& line) {
        auto itor = line.find(':');
        if (itor == line.npos)return std::nullopt;
        auto name = line.substr(0, itor);
        if (itor + 1 >= line.size())return std::pair{name,""};
        auto val = line.substr(itor + 1, line.size() - itor - 1);
        return std::pair{ trim(name),trim(val) };
    }

    std::optional<std::string> findCfgEnv(const std::string& line) {
        if (line.size() < 4) {
            return std::nullopt;
        }
        return line.substr(2, line.size() - 2 - 2);
    }
}
namespace Fei {
    FConfigReader::FConfigReader(const std::string& cfgPath)
    {
        std::ifstream in(cfgPath);
        if (!in) {
            std::cout << MODULE_NAME << "No Config File Found." << std::endl;
        }
        std::string env;

        std::string curReadingEnv = "test";
        
        bool envSettingFound = false;
        bool curEnvFound = false;
        std::string line;
        std::map<std::string, std::map<std::string, std::string>> mEnvCfgMap;
        while (std::getline(in, line)) {
            if (isBlank(line)) {
                continue;
            }
            auto noSpaceLine = trim(line);
            auto term = readCfgTerm(noSpaceLine);
            if (term.has_value()) {
                if (!envSettingFound) {
                    //Env setting
                    if (term.value().first == "env") {
                        auto env_ = toEnv(term.value().second);
                        if (env_ != Env::Invalid) {
                            envSettingFound = true;
                            env = term.value().second;
                            mCurEnv = env_;
                            std::cout << MODULE_NAME << "Using Config Setting: "<< env << std::endl;
                        }
                        else {
                            std::cout << MODULE_NAME << "Invalid env term: " << term.value().second << std::endl;
                        }
                    }
                    continue;
                }
                mEnvCfgMap[curReadingEnv][term.value().first] = term.value().second;
            }
            else {
                auto env_ = findCfgEnv(noSpaceLine);
                if (env_.has_value() && (toEnv(env_.value()) != Env::Invalid)) {
                    curEnvFound = true;
                    curReadingEnv = env_.value();
                }
                else {
                    std::cout << MODULE_NAME << "Invalid config term: " << line << std::endl;
                }
            }
        }

        if (envSettingFound) {
            mCfgMap = mEnvCfgMap[env];
        }

    }
}
