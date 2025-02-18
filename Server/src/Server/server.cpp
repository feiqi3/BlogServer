#include "FeiLibIniter.h"
#include "server.h"
#include "Http/FHttpServer.h"
#include "Service/Filter.h"
#include <string>
#include <thread>
const std::string ResourceDir =  SERVER_RESOURCE_DIR;
const std::string ConfigDir = ResourceDir + "config/serverConfig";
const std::string WebDir = ResourceDir + "web/";
const std::string SSLFileDir = ResourceDir + "SSL/";

Blog::Server::Server()
{
	Fei::FeiLibInit();
	Fei::Http::FHttpServer::initSSLenv(SSLFileDir + "cert.pem", SSLFileDir + "private.pem");
	server = new Fei::Http::FHttpServer(10);
	server->addListenPort(80);
	server->addSSLPort(443);
}

void Blog::Server::run()
{
	server->run();
	while (1) {
		using namespace std::chrono_literals;
		std::this_thread::sleep_for(200ms);
		server->tickAppEvents();
	}
}

void Blog::Server::init()
{
	server->setConnFilterCB(&filterAll);
}

Blog::Server::~Server()
{
	delete server;
}
