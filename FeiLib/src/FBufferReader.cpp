#include "FBufferReader.h"
#include "FBuffer.h"
#include <algorithm>
#include <cstring>
namespace Fei {
int FBufferReader::readTo(void *buffer, int bufLen) {
  if (buffer == nullptr || bufLen == 0) {
    return mBuffer.getReadableSize();
  } else {
    int toReadSize = std::min(mBuffer.getReadableSize(), bufLen);
    mBuffer.Peek(toReadSize, (char *)buffer);
    mBuffer.Pop(toReadSize);
    return toReadSize;
  }
}

} // namespace Fei