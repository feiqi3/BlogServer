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
private:
    FBuffer& mBuffer;
};
} // namespace Fei
#endif