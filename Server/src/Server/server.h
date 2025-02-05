#ifndef SERVER_H
#define SERVER_H

#include "Http/FHttpServer.h"

namespace Blog {
	class Server {
	public:
		Server();
		void run();
		void init();
		~Server();
	private:
		Fei::Http::FHttpServer* server;
	};
}
#endif