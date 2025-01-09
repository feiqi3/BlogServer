#ifndef FBUFFER_H
#define FBUFFER_H
#include "FDef.h"
#include <cstddef>
#include <mutex>
#include <vector>
namespace Fei {
using Byte = uint8;

class FBuffer {
public:
  FBuffer(uint32 size);

  FBuffer(FBuffer &&rhs) {
    m_buffer = std::move(rhs.m_buffer);
    readIdx = rhs.readIdx;
    writeIdx = rhs.writeIdx;
  }

  void Append(const char* data,uint32 len);

  size_t Read(Socket fd, Errno_t &errSaved);
  size_t Write(Socket fd, int size, Errno_t &errSaved);

  int Peek(int wantLen, char *ret); // sizeof ret >= wantlen
  void Pop(int len);
  void PopAll();

  uint32 getWriteableSize() const { return m_buffer.size() - writeIdx; }
  uint32 getReadableSize() const { return writeIdx - readIdx; }

private:
  uint32 readIdx = 0;
  uint32 writeIdx = 0;
  std::vector<Byte> m_buffer;
};
} // namespace Fei

#endif