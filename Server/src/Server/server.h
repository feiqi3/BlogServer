#ifndef SERVER_H
#define SERVER_H

#include "Http/FHttpServer.h"
#include <functional>
#include <atomic>
#include <memory>

namespace Blog {
	class ImageService;
    class ProfilerService;
	namespace Script { class ScriptAdminService; class ArticleHookService; }
	class Server {
	public:
		Server();
		void run();
		void init();
		void shutdown();
		static void CurThreadCleanCallback(std::function<void()> callback);
		~Server();
	private:
		std::atomic_bool shouldClose{false};
		Fei::Http::FHttpServer* server;
		std::unique_ptr<ImageService> images;
        std::shared_ptr<ProfilerService> profiler;
#ifdef FEILIB_ENABLE_SCRIPT
        void stopScriptServices();
		std::shared_ptr<Script::ScriptAdminService> scripts;
        std::shared_ptr<Script::ArticleHookService> articleHooks;
#endif
	};
}
#endif
