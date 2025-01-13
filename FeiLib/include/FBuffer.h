#ifndef FBUFFER_H
#define FBUFFER_H
#include "FDef.h"
#include <cassert>
#include <cstddef>
#include <vector>
namespace Fei {

class FBuffer {
public:
  FBuffer(uint32 size);

  FBuffer(FBuffer &&rhs) {
    m_buffer = std::move(rhs.m_buffer);
    readIdx = rhs.readIdx;
    writeIdx = rhs.writeIdx;
  }

  void Append(const char *data, uint32 len);

  int Read(Socket fd, Errno_t &errSaved);
  int Write(Socket fd, int size, Errno_t &errSaved);

  int Peek(int wantLen, char *ret); // sizeof ret >= wantlen
  void Pop(int len);
  void PopAll();
  char Get(uint32 offset) const;

  int getWriteableSize() const { return int(m_buffer.size() - writeIdx); }
  int getReadableSize() const { return int(writeIdx - readIdx); }

private:
  friend class FBufferView;
  const Byte &GetDirect(uint32 pos) const { return m_buffer[pos]; }
  uint32 readIdx = 0;
  uint32 writeIdx = 0;
  std::vector<Byte> m_buffer;
};

} // namespace Fei

#endif