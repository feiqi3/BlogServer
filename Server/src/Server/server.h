#ifndef SERVER_H
#define SERVER_H

#include "Http/FHttpServer.h"

namespace Blog {
	class Server {
	public:
		Server();
		void run();
		void init();
		void shutdown();
		~Server();
	private:
		bool shouldClose = false;
		Fei::Http::FHttpServer* server;
	};
}
#endif