#pragma once
#include "Utils/Singleton.h"
#include <atomic>
#include <string>

namespace Blog {
class RssBuilder : public Singleton<RssBuilder> {
public:
  RssBuilder();
  // Returns cached RSS XML; rebuilds first if dirty.
  const std::string &getRssXml(std::string &lastModifiedOut);
  void rebuild();
  void setDirty();

private:
  std::string mXml;
  std::string mLastModified;
  std::atomic<bool> mDirty = true;
};
} // namespace Blog
