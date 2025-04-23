#include "FConfigReader.h"
#include "FDef.h"
#include "FeiLibIniter.h"
#include "server.h"
#include "Http/FHttpServer.h"
#include "Core/Session.h"

#include "Service/AdminLogin.h"

#include "Service/Filter.h"

#include "Service/BlogData.h"

#include <functional>
#include <string>
#include <thread>
#include "Core/ServerBasicDef.h"
#include "DAO/DataBaseOperation.h"
#include "Utils/Digital.h"
#include "Utils/FileCache.h"

const std::string ResourceDir =  SERVER_RESOURCE_DIR;
const std::string WebDir = ResourceDir + "web/";
const std::string SSLFileDir = ResourceDir + "SSL/";

Blog::Server::Server()
{
	Fei::FeiLibInit();
	Fei::Http::FHttpServer::initSSLenv(SSLFileDir + "cert.pem", SSLFileDir + "private.pem");

	//init database
	std::string database;
	
	auto dataBasePathOpt = Fei::FConfigReader::instance()->getCfg("Database");
	if(dataBasePathOpt.has_value()){
		database = dataBasePathOpt.value();
	}else{
		database = BlogDataBasePath;
	}
	new DatabaseOperation();
	DatabaseOperation::instance()->LoadDB(database);
	auto fileCacheHoldTime = Fei::FConfigReader::instance()->getCfg("FileCacheHoldTime");
	Fei::uint32 fileCacheHoldTimeMs = 1000 * 60 * 60;
	if(fileCacheHoldTime.has_value()){
		if(Digital::isNumber(fileCacheHoldTime.value())){
			fileCacheHoldTimeMs = std::stoul(fileCacheHoldTime.value());
		}
	}
	new BlogData;
	new FileCache(fileCacheHoldTimeMs);
	new SessionManager();
	new AdminLogin();
	server = new Fei::Http::FHttpServer(10);
	server->addListenPort(80);
	server->addSSLPort(443);
}

void Blog::Server::run()
{
	server->run();
	while (1) {
		using namespace std::chrono_literals;
		std::this_thread::sleep_for(20s);
		server->tickAppEvents();
	}
}

void Blog::Server::init()
{
	server->addAppTickEvent(std::bind(&FileCache::checkOverdue,FileCache::instance(),std::placeholders::_1));
	server->addAppTickEvent(std::bind(&SessionManager::checkOverdue,SessionManager::instance(),std::placeholders::_1));
	server->addAppTickEvent(std::bind(&BlogData::syncData,BlogData::instance(),std::placeholders::_1));
	//server->setConnFilterCB(&filterAll);
}

Blog::Server::~Server()
{
	delete server;
	delete DatabaseOperation::instance();
	delete AdminLogin::instance();
	delete SessionManager::instance();
	delete BlogData::instance();
	
}
