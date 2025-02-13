#ifndef FSOCKWRAPPER_H
#define FSOCKWRAPPER_H
#include "FDef.h"
#include "FNoCopyable.h"
#include "FSocket.h"

namespace Fei{
class F_API FSock : public FNoCopyable{

public:
FSock():valid(false){};
explicit FSock(Socket fd);
FSock(FSock&& sock);
FSock& operator=(FSock&& rhs);
~FSock();

Socket getFd()const;

//Mainly work on unix
void setReuseport(bool on);
void setReuseAddr(bool on);
void setNoneBlock(bool on);
void setExitOnExec(bool on);
void setKeepAlive(bool on);
// in seconds
void setKeepIdle(int time);
void setKeepInterval(int time);

private:
    Socket m_fd;
    bool valid = true;
};
}
#endif