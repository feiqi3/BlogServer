// A simple wrapper of FBuffer
// use this to read instead of use FBuffer

#ifndef FBUFFERREADER_H
#define FBUFFERREADER_H
#include "FDef.h"
#include <cassert>

namespace Fei {
class FBuffer;

// Reader is a wrapper of buffer to keep buffer's function out off user's
// visibility
class F_API FBufferReader {
public:
  FBufferReader(FBuffer &buffer) : mBuffer(buffer) {}
  // Return readable length if  buffer = nullptr or bufLen = 0
  // Fill buffer if not null
  int readTo(void *buffer, int bufLen);

  int getBufferReadableSize() const;

  char readNext();

  const unsigned char* peekAll(int& size);
  void expireSize(int size);

private:
  FBuffer &mBuffer;
};
} // namespace Fei
#endif