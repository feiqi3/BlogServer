// Test-process diagnostic preload only; never linked into FeiLib or Server.
#include <execinfo.h>
#include <signal.h>
#include <unistd.h>
#include <cstdlib>
namespace {
void crash(int signal){
    const char message[]="Startup diagnostic stack:\n";
    write(STDERR_FILENO,message,sizeof(message)-1);
    void* addresses[64];auto count=backtrace(addresses,64);
    backtrace_symbols_fd(addresses,count,STDERR_FILENO);
    _Exit(128+signal);
}
__attribute__((constructor)) void install(){
    struct sigaction action{};action.sa_handler=crash;action.sa_flags=SA_RESETHAND;
    sigemptyset(&action.sa_mask);sigaction(SIGSEGV,&action,nullptr);
}
}
