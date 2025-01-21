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
  const auto &reader = readLineNoPop(linebreaker);
  auto sizeParsed = reader.size();
  std::string ret;
  ret.reserve(sizeParsed + 1);
  auto sizeGet = mBuffer.Peek(sizeParsed + 1, ret.data());
  assert((uint32)sizeGet == sizeParsed);
  mBuffer.Pop(sizeGet);
  return ret;
}

void FBufferReader::popLine()
{
    auto view = readLineNoPop();
    expireView(view);
}

FBufferView FBufferReader::readLineNoPop(LineBreaker linebreaker) const {
  int end = 0;
  while (true) {
    char nxt;
    if ((nxt = mBuffer.Get(end)) == '\0')
      break;
    end++;

    // LF
    if (nxt == '\n' && linebreaker == LineBreaker::LF) {
      break;
    } else if (nxt == '\r' && (int)linebreaker & (int)LineBreaker::CR) {
      // CRLF
      if (linebreaker == LineBreaker::CRLF) {
        char lookahead = mBuffer.Get(end);
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
  auto view = FBufferView(mBuffer, 0, end);
  return view;
}
void FBufferReader::expireView(const FBufferView &view) {
  mBuffer.Pop(view.size());
}

char FBufferReader::readNext() {
  char buf;
  mBuffer.Peek(1, &buf);
  mBuffer.Pop(1);
  return buf;
}

FBufferView::FBufferView(FBuffer &inBuffer, uint32 _beg, uint32 _end)
    : buffer(&inBuffer), beg(_beg + buffer->readIdx), end(_end + buffer->readIdx) {
}
  bool FBufferView::isEOF()const{
    return buffer->Get(beg)== '\0';
  }

const Byte &FBufferView::operator[](uint32 pos) const {
  assert(pos + beg <= end);
  return buffer->GetDirect(beg + pos);
}

} // namespace Fei