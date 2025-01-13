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

std::string FBufferReader::readLine(LineBreaker linebreaker) {
  int end = 0;
  while (true) {
    char nxt;
    if ((nxt = mBuffer.Get(end)) == '\0')
      break;
    end++;
    
    // LF
    if (nxt == 'n' && linebreaker == LineBreaker::LF) {
      break;
    } else if (nxt == '\r' && (int)linebreaker & (int)LineBreaker::CR) {
      // CRLF
      if (linebreaker == LineBreaker::CRLF) {
        char lookahead = mBuffer.Get(end + 1);
        if (lookahead == '\n') {
          end++;
          break;
        }
        // CR
      } else {
        break;
      }
    }
  }
  std::string ret;
  ret.reserve(end + 1);
  auto sizeGet = mBuffer.Peek(end + 1, ret.data());
  assert(sizeGet == end);
  mBuffer.Pop(sizeGet);
  return ret;
}

char FBufferReader::readNext() {
  char buf;
  mBuffer.Peek(1, &buf);
  mBuffer.Pop(1);
  return buf;
}

} // namespace Fei