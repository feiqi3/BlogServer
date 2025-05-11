#ifndef SERVER_H
#define SERVER_H

#include "Http/FHttpServer.h"
#include <functional>

namespace Blog {
	class Server {
	public:
		Server();
		void run();
		void init();
		void shutdown();
		static void CurThreadCleanCallback(std::function<void()> callback);
		~Server();
	private:
		bool shouldClose = false;
		Fei::Http::FHttpServer* server;
	};
}
#endif