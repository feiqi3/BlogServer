#include "Core/stableInclude.h"
#include "Server/server.h"

class Blog::Server* g_Server = 0;


int main(int args, char** argvs) {
	Blog::Server server;
	g_Server = &server;
	server.init();
	server.run();
};