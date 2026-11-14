#include "Core/TemplateRender.h"
#include "FConfigReader.h"
#include "FDef.h"
#include "FLogger.h"
#include "FeiLibIniter.h"
#include "server.h"
#include "Http/FHttpServer.h"
#include "Core/Session.h"

#include "FEventLoop.h"

#include "Service/AdminLogin.h"

#include "Service/Filter.h"

#include "Service/BlogData.h"

#include "Service/RssBuilder.h"

#include <functional>
#include <string>
#include <thread>
#include "Core/ServerBasicDef.h"
#include "DAO/DataBaseOperation.h"
#include "DAO/PhotoQuery.h"
#include "Utils/Digital.h"
#include "Utils/FileCache.h"
#include "Utils/ImageDimReader.h"
#include "Utils/ThreadPool.h"
#ifdef FEILIB_ENABLE_SCRIPT
#include "Script/FScriptEngine.h"
#include "Service/ScriptAdminService.h"
#endif

const std::string ResourceDir =  SERVER_RESOURCE_DIR;
const std::string WebDir = ResourceDir + "web/";
const std::string SSLFileDir = ResourceDir + "SSL/";
const std::string PushPromiseCfgPath = ResourceDir + "config/h2PushPromise.cfg";
Blog::Server::Server()
{
	Fei::FeiLibInit(ResourceDir + "config/" + "server.cfg" );
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
	new TemplateRender;
	new RssBuilder;
	server = new Fei::Http::FHttpServer(10);
	server->loadPushPromiseList(PushPromiseCfgPath);
	server->addListenPort(80);
	server->addSSLPort(443);
	server->jobLoop(); // Configure the application Loop before run().

	// Backfill dimensions for photos created before width/height tracking.
	auto missingDims = DAO::PhotoQuery::QueryPhotosMissingDims();
	if (!missingDims.empty()) {
		Fei::Logger::instance()->log(
			Fei::lvl::info,
			"[Server] Backfilling image dimensions for {} photos",
			missingDims.size());
		for (const auto& [id, url] : missingDims) {
			Utils::imagePool().submit([id, url]() {
				int w = 0, h = 0;
				if (Utils::readImageDims(url, w, h))
					DAO::PhotoQuery::UpdatePhotoDims(id, w, h);
			});
		}
	}
}

void Blog::Server::run()
{
#ifdef FEILIB_ENABLE_SCRIPT
    bool adminScripts = false;
    auto scriptCfg = Fei::FConfigReader::instance();
    auto scriptPath = scriptCfg->getCfg("ScriptFile").value_or("");
    auto scriptInterval = std::chrono::milliseconds(1200000);
    if (!scriptPath.empty() || scriptCfg->getCfg("ScriptAdminEnabled").value_or("false") == "true") {
        try {
            auto interval = scriptCfg->getCfg("ScriptIntervalMs");
            if (interval) {
                if (!Digital::isNumber(*interval)) throw std::runtime_error("Invalid ScriptIntervalMs");
                scriptInterval = std::chrono::milliseconds(std::stoll(*interval));
            }
            if (scriptInterval > std::chrono::hours(24) || (scriptInterval.count() > 0 && scriptInterval < std::chrono::milliseconds(10)))
                throw std::runtime_error("ScriptIntervalMs must be 0 or 10..86400000");
            Fei::FScriptOptions options;
            options.report = [](const Fei::FScriptExecution& result) {
                Fei::Logger::instance()->log(Fei::lvl::info, "[Scripts] phase={} duration_ms={} success={}", result.phase, result.duration.count(), result.error.empty());
                if (auto admin = Script::ScriptAdminService::current()) admin->record(result);
            };
            auto& engine = server->scriptEngine(options);
            adminScripts = scriptCfg->getCfg("ScriptAdminEnabled").value_or("false") == "true";
            if (!adminScripts) engine.addScript("blog", scriptPath, scriptInterval);
        } catch (const std::exception&) {
            Fei::Logger::instance()->log(Fei::lvl::err, "[Scripts] Script service could not start");
        }
    }
#endif
	server->run();
#ifdef FEILIB_ENABLE_SCRIPT
    if (adminScripts) {
        try {
            scripts = Script::ScriptAdminService::create(server->jobLoop(), server->scriptEngine(),
                scriptCfg->getCfg("ScriptStoreDirectory").value_or(ResourceDir + "scripts/versions"), scriptInterval, scriptPath);
        } catch (const std::exception&) {
            Fei::Logger::instance()->log(Fei::lvl::err, "[Scripts] Administration could not start");
        }
    }
#endif
	while (!shouldClose) {
		using namespace std::chrono_literals;
		std::this_thread::sleep_for(20s);
		server->tickAppEvents();
	}
#ifdef FEILIB_ENABLE_SCRIPT
    if (scripts) scripts->shutdown();
#endif
	server->stop();
}

void Blog::Server::init()
{
	server->addAppTickEvent(std::bind(&FileCache::checkOverdue,FileCache::instance(),std::placeholders::_1));
	server->addAppTickEvent(std::bind(&SessionManager::checkOverdue,SessionManager::instance(),std::placeholders::_1));
	server->addAppTickEvent(std::bind(&BlogData::syncData,BlogData::instance(),std::placeholders::_1));
	server->addAppTickEvent(std::bind(&TemplateRender::checkOverdue,TemplateRender::instance(),std::placeholders::_1));
	//server->setConnFilterCB(&filterAll);
}

void Blog::Server::shutdown(){
	shouldClose = true;
}

void Blog::Server::CurThreadCleanCallback(std::function<void()> callback){
	if(!Fei::FEventLoop::getCurrentLoop())return;
	Fei::FEventLoop::getCurrentLoop()->addLoopCloseCallback(callback);
}

Blog::Server::~Server()
{
#ifdef FEILIB_ENABLE_SCRIPT
    if (scripts) scripts->shutdown();
    scripts.reset();
#endif
	server->stop();
	delete server;
	delete TemplateRender::instance();
	delete DatabaseOperation::instance();
	delete AdminLogin::instance();
	delete SessionManager::instance();
	delete BlogData::instance();
	Fei::Logger::instance()->log(Fei::lvl::info, "Server Shutdown.");
	Fei::Http::FHttpServer::deinitSSLenv();
	Fei::FeiLibUnInit();
}
