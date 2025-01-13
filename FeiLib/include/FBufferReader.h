// A simple wrapper of FBuffer
// use this to read instead of use FBuffer

#ifndef FBUFFERREADER_H
#define FBUFFERREADER_H
#include "FDef.h"
#include <cassert>

namespace Fei {
class FBuffer;

//A light-weight buffer reader, do not control buffer's lifetime
class FBufferView {
public:
  FBufferView(FBuffer &inBuffer, uint32 _beg, uint32 _end);
  const Byte *get() const { return &((*this)[0]); }
  const Byte &operator[](uint32 pos) const;
  uint32 size()const {return end - beg;}
private:
  FBuffer &buffer;
  uint32 beg, end;
};

//Reader is a wrapper of buffer to keep buffer's function out off user's visibility
class F_API FBufferReader {
public:
  FBufferReader(FBuffer &buffer) : mBuffer(buffer) {}
  // Return readable length if  buffer = nullptr or bufLen = 0
  // Fill buffer if not null
  int readTo(void *buffer, int bufLen);

  enum class LineBreaker {
    CRLF = 3, // Line feed carriage return, also known as /r/n
    LF = 0,   // \n
    CR = 1,   // \r
  };

  std::string
  readLine(LineBreaker linebreaker = FBufferReader::LineBreaker::CRLF);
  void popLine();
  FBufferView readLineNoPop(LineBreaker linebreaker = FBufferReader::LineBreaker::CRLF)const;

  //Pop view's range
  void expireView(const FBufferView& view);

  char readNext();

private:
  FBuffer &mBuffer;
};
} // namespace Fei
#endif