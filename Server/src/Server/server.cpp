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
#include "Service/TrafficGuard.h"

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
#include "Service/ImageService.h"
#include "Service/ProfilerService.h"
#ifdef FEILIB_ENABLE_SCRIPT
#include "Script/FScriptEngine.h"
#include "Service/ScriptAdminService.h"
#include "Service/ArticleHookService.h"
#include "ScriptRuntime/AvatarCache.h"
#include <future>
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
	server->jobLoop(); // Configure the shared application Loop before run().
}

void Blog::Server::run()
{
    auto profileCfg=Fei::FConfigReader::instance();
    if(profileCfg->getCfg("ProfilingEnabled").value_or("false")=="true"){
        try{profiler=ProfilerService::create(profileCfg->getCfg("ProfilingDirectory").value_or(ResourceDir+"profiling"));}
        catch(const std::exception& error){Fei::Logger::instance()->log(Fei::lvl::err,"[Profiling] Could not start: {}",error.what());}
    }
#ifdef FEILIB_ENABLE_SCRIPT
    bool adminScripts = false;
    auto scriptCfg = Fei::FConfigReader::instance();
    try{avatars=Script::AvatarCache::create(server->jobLoop(),scriptCfg->getCfg("AvatarCacheDirectory").value_or(ResourceDir+"cache/avatars"));}
    catch(const std::exception& error){Fei::Logger::instance()->log(Fei::lvl::err,"[AvatarCache] Could not start: {}",error.what());}
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
            if(auto configured=scriptCfg->getCfg("ScriptMemoryLimitMiB")){
                std::size_t memoryMiB=0;
                if(!FCfgUtils::toNumber(*configured,memoryMiB)||memoryMiB<1||memoryMiB>1024)
                    throw std::runtime_error("ScriptMemoryLimitMiB must be an integer in 1..1024");
                options.memoryLimitBytes=memoryMiB*1024*1024;
            }
            options.maxJsonBytes=256*1024;
            options.profileInvocations=static_cast<bool>(profiler);
            options.report = [](const Fei::FScriptExecution& result) {
                Fei::Logger::instance()->log(Fei::lvl::info, "[Scripts] phase={} duration_ms={} success={}", result.phase, result.duration.count(), result.error.empty());
                if (auto admin = Script::ScriptAdminService::current()) admin->record(result);
            };
            auto& engine = server->scriptEngine(options);
            if(avatars)avatars->bind(engine);
            articleHooks=Script::ArticleHookService::create(server->jobLoop(),engine);
            adminScripts = scriptCfg->getCfg("ScriptAdminEnabled").value_or("false") == "true";
            if (!adminScripts) engine.addScript("blog", scriptPath, scriptInterval);
        } catch (const std::exception& error) {
            Fei::Logger::instance()->log(Fei::lvl::err, "[Scripts] Script service could not start: {}",error.what());
        }
    }
#endif
	server->run();
#ifdef FEILIB_ENABLE_SCRIPT
    if (adminScripts) {
        try {
            scripts = Script::ScriptAdminService::create(server->jobLoop(), server->scriptEngine(),
                scriptCfg->getCfg("ScriptStoreDirectory").value_or(ResourceDir + "scripts/versions"), scriptInterval, scriptPath,
                [weak=std::weak_ptr<Script::ArticleHookService>(articleHooks)](const auto& info){if(auto hooks=weak.lock())hooks->functions(info.value("functions",nlohmann::json::array()));});
        } catch (const std::exception&) {
            Fei::Logger::instance()->log(Fei::lvl::err, "[Scripts] Administration could not start");
        }
    }
#endif
	images = std::make_unique<ImageService>(server->jobLoop());


	images->backfillDimensions();
	while (!shouldClose) {
		using namespace std::chrono_literals;
		std::this_thread::sleep_for(20s);
		server->tickAppEvents();
	}
	images->shutdown();
#ifdef FEILIB_ENABLE_SCRIPT
    stopScriptServices();
    if(avatars)avatars->shutdown();
#endif
    trafficGuard->stopDatabase();
	server->stop();
    if(profiler)profiler->shutdown();
}

#ifdef FEILIB_ENABLE_SCRIPT
void Blog::Server::stopScriptServices(){
    if(articleHooks)articleHooks->stopAccepting();
    if(scripts){scripts->shutdown();scripts.reset();}
    if(articleHooks){
        // Stop scripts while their model workers and callback Loop are alive.
        std::promise<void> drained;auto ready=drained.get_future();
        server->scriptEngine().shutdown([&drained]{drained.set_value();});ready.get();
        articleHooks->shutdown();articleHooks.reset();
    }
}
#endif

void Blog::Server::init()
{
	server->addAppTickEvent(std::bind(&FileCache::checkOverdue,FileCache::instance(),std::placeholders::_1));
	server->addAppTickEvent(std::bind(&SessionManager::checkOverdue,SessionManager::instance(),std::placeholders::_1));
	server->addAppTickEvent(std::bind(&BlogData::syncData,BlogData::instance(),std::placeholders::_1));
	server->addAppTickEvent(std::bind(&TemplateRender::checkOverdue,TemplateRender::instance(),std::placeholders::_1));
	//server->setConnFilterCB(&filterAll);
	trafficGuard = TrafficGuard::install(*server, ResourceDir);
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
    stopScriptServices();
    if(avatars)avatars->shutdown();
#endif
	if (images) images->shutdown();
    if (trafficGuard) trafficGuard->stopDatabase();
	server->stop();
    if(profiler)profiler->shutdown();
    profiler.reset();
	images.reset();
    trafficGuard.reset();
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
