#include "ServerConfig.h"
#include <fstream>
#include "FLogger.h"

#define MODULE_NAME "ServerConfig"

Blog::ServerConfig::ServerConfig(const std::string& configDir)
{
	using namespace nlohmann;
	std::fstream fs(configDir);
	try {
		mConfigJson = json::parse(fs);
	}
	catch(json::parse_error& ex){
		Fei::Logger::instance()->log(MODULE_NAME, Fei::lvl::err, "Parse config file \"{}\" error, reason: {}", configDir, ex.what());
	}
}

