#include "FBufferReader.h"
#include "FBuffer.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <string>
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

int FBufferReader::getBufferReadableSize() const{
  return mBuffer.getReadableSize();
}

char FBufferReader::readNext(){
  if (mBuffer.getReadableSize() > 0) {
    char c = mBuffer.Get(0);
    mBuffer.readIdx++;
    return c;
  }
  return '\0';
}

const unsigned char* FBufferReader::peekAll(int& size){
  size = mBuffer.getReadableSize();
  if(size == 0){
    return nullptr;
  }
  return &mBuffer.GetDirect(mBuffer.readIdx);
}

void FBufferReader::expireSize(int size){
  mBuffer.Pop(size);
}

} // namespace Fei