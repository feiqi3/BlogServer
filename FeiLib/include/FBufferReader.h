// A simple wrapper of FBuffer
// use this to read instead of use FBuffer

#ifndef FBUFFERREADER_H
#define FBUFFERREADER_H
#include "FDef.h"
namespace Fei {
class FBuffer;
class F_API FBufferReader {
public:
  FBufferReader(FBuffer &buffer):mBuffer(buffer) {}
  //Return readable length if  buffer = nullptr or bufLen = 0
  //Fill buffer if not null
  int readTo(void* buffer,int bufLen);

  enum class LineBreaker{
    CRLF = 3, //Line feed carriage return, also known as /r/n
    LF   = 0 ,// \n
    CR   = 1 ,// \r
  };

  std::string readLine(LineBreaker linebreaker = FBufferReader::LineBreaker::CRLF);

  char readNext();
private:
    FBuffer& mBuffer;
};
} // namespace Fei
#endif