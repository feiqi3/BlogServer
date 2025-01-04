#include "FSockWrapper.h"
#include "FDef.h"
#include "FSocket.h"

namespace Fei {

FSock::FSock(Socket sock) : m_fd(sock) {}

FSock::FSock(FSock &&rhs) {
  m_fd = rhs.m_fd;
  rhs.valid = false;
}

FSock::~FSock() {
  if (valid) {
    Close(m_fd);
    valid = false;
  }
}

FSock &FSock::operator=(FSock &&rhs) {
  m_fd = rhs.m_fd;
  rhs.valid = false;
  return *this;
}

Socket FSock::getFd() const { return m_fd; }

void FSock::setReuseAddr(bool on) {
  if (valid)
    SetSockOpt(m_fd, SockOpt::ReuseAddr, on);
}

void FSock::setReuseport(bool on) {
  if (valid)
    SetSockOpt(m_fd, SockOpt::ReusePort, on);
}

void FSock::setNoneBlock(bool on) {
  if (valid) {
    SetSockOpt(m_fd, SockOpt::NoneBlock, on);
  }
}
void FSock::setExitOnExec(bool on) {
  if (valid) {
    SetSockOpt(m_fd, SockOpt::CloseOnExec, on);
  }
}

} // namespace Fei