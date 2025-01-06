#include "FBuffer.h"
#include "FDef.h"
#include "FSocket.h"
#include <cerrno>
#include <cstddef>
#include <cstring>
namespace Fei {
FBuffer::FBuffer(uint32 size) { m_buffer.reserve(size); }
size_t FBuffer::Read(Socket fd, Errno_t &errSaved) {
  char extrabuf[65536];
  iovec vec[2];
  const size_t writealbe = getWriteableSize();
  vec[0].iov_base = this->m_buffer.data() + this->writeIdx;
  vec[0].iov_len = writealbe;
  vec[1].iov_base = extrabuf;
  vec[1].iov_len = sizeof(extrabuf);

  // ???
  const int iovcnt = writealbe < sizeof(extrabuf) ? 2 : 1;
  const size_t n = Readv(fd, vec, iovcnt);
  if (n < 0) {
    errSaved = errno;
  } else if (n < writealbe) {
    writeIdx += n;
  } else {
    m_buffer.resize(writeIdx + n);
    memcpy(m_buffer.data() + writealbe + writeIdx, extrabuf, n - writealbe);
    writeIdx += n;
  }
  return n;
}

size_t FBuffer::Write(Socket fd, int size, Errno_t &errSaved) {
  int writeLen = 0;
  writeLen = std::min(size, int(getReadableSize()));
  auto status = Send(fd, (const char *)this->m_buffer.data() + readIdx,
                     writeLen, writeLen);
  if (status != SocketStatus::Success) {
    errSaved = errno;
  } else {
    this->Pop(writeLen);
  }
}

int FBuffer::Peek(int wantLen, char *ret) {
  uint32 realLen = std::min((uint32)wantLen, writeIdx - readIdx);
  memcpy(ret, m_buffer.data(), realLen);
  return realLen;
}

void FBuffer::Append(const char *data, uint32 len) {
  uint32 newSize = writeIdx + len;
  m_buffer.resize(newSize);
  memcpy(m_buffer.data()+writeIdx, data, len);
}

void FBuffer::Pop(int len) { readIdx += len; }
void FBuffer::PopAll() { readIdx = writeIdx; }

} // namespace Fei