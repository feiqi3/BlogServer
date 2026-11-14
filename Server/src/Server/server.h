#ifndef SERVER_H
#define SERVER_H

#include "Http/FHttpServer.h"
#include <functional>
#include <atomic>
#include <memory>

namespace Blog {
	namespace Script { class ScriptAdminService; }
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
#ifdef FEILIB_ENABLE_SCRIPT
		std::shared_ptr<Script::ScriptAdminService> scripts;
#endif
	};
}
#endif
