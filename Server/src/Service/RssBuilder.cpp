#include "RssBuilder.h"
#include "Core/TemplateRender.h"
#include "Core/ServerBasicDef.h"
#include "DAO/CategoryQuery.h"
#include "DAO/QueryPosts.h"
#include "Utils/HtmlHelper.h"
#include "Utils/TimeHelper.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Blog {

namespace {
const char *sSiteTitle = "feiqi3.cn";
const char *sSiteLink = "https://feiqi3.cn";
const char *sSiteDescription = "\xE9\xA3\x9E\xE8\xB5\xB7\xE7\x9A\x84\xE5\x8D\x9A\xE5\xAE\xA2";

struct RssItem {
  std::string title;
  std::string link;
  std::string guid;
  std::string pubDate;
  std::string description;
  std::string categoryName;
};
} // namespace

RssBuilder::RssBuilder() { rebuild(); }

void RssBuilder::setDirty() { mDirty = true; }

void RssBuilder::rebuild() {
  auto postVec = DAO::PostQuery::QueryPostsForRss();
  auto cates = DAO::CategoryQuery::QueryAllCategoryBasicInfo();
  std::map<uint64_t, std::string> catesMap;
  for (auto &&c : cates) {
    catesMap.insert({std::get<0>(c), std::move(std::get<1>(c))});
  }

  std::vector<RssItem> items;
  items.reserve(postVec.size());
  for (auto &&p : postVec) {
    RssItem item;
    item.title = html_unescape(std::get<1>(p));
    item.link = std::string(sSiteLink) + "/post/" + std::to_string(std::get<0>(p));
    item.guid = item.link;
    item.pubDate = TimeHelper::toRfc822(std::get<3>(p));
    item.description = html_unescape(std::get<2>(p));
    auto it = catesMap.find(std::get<4>(p));
    if (it != catesMap.end()) {
      item.categoryName = it->second;
    }
    items.push_back(std::move(item));
  }

  TemplateRenderData data;
  data.setData("channelTitle", std::string(sSiteTitle));
  data.setData("channelLink", std::string(sSiteLink));
  data.setData("channelDescription", std::string(sSiteDescription));
  data.setData("items", std::move(items));

  std::string out;
  TemplateRender::instance()->render(BlogWebPagePath + "rss.xml", data, out);
  mLastModified = TimeHelper::toRfc822(TimeHelper::getCurrentTimeFromEpochMills());
  mXml = std::move(out);
  mDirty = false;
}

const std::string &RssBuilder::getRssXml(std::string &lastModifiedOut) {
  if (mDirty) {
    rebuild();
  }
  lastModifiedOut = mLastModified;
  return mXml;
}

} // namespace Blog
