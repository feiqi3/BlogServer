#include "Core/stableInclude.h"
#include "Server/server.h"
int main(int args, char** argvs) {
	Blog::Server server;
	server.init();
	server.run();
};