#ifndef FSOCKWRAPPER_H
#define FSOCKWRAPPER_H
#include "FDef.h"
#include "FNoCopyable.h"
#include "FSocket.h"

namespace Fei{
class FSock : public FNoCopyable{

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
private:
    Socket m_fd;
    bool valid = true;
};
}
#endif