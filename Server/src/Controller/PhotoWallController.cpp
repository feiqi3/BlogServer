#include "PhotoWallController.h"
#include "Core/ApiChangeDataDef.h"
#include "Core/JsonTool.h"
#include "DAO/AlbumQuery.h"
#include "DAO/PhotoQuery.h"
#include "Model/Albums.h"
#include "Model/Photos.h"
#include "Service/AdminLogin.h"
#include "Service/QuickRedirect.h"
#include "Utils/Digital.h"
#include "Utils/ImageDimReader.h"
#include "Utils/ThreadPool.h"
#include "Utils/TimeHelper.h"
#include "Http/FReflect.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace Blog {

Fei::Http::FHttpResponse
PhotoWallController::GetAllAlbums(const Fei::Http::FHttpRequest &req,
                                  const Fei::Http::FPathVar &var) {
  Fei::Http::FHttpResponse res;
  auto albums = DAO::AlbumQuery::QueryAllAlbums();
  nlohmann::json j;
  j["result"] = ApiOk;
  nlohmann::json arr = nlohmann::json::array();
  for (auto &a : albums) {
    nlohmann::json item = Fei::Http::FReflect::fromClass(a);
    item["photoCount"] = DAO::AlbumQuery::QueryPhotoCountInAlbum(a.id);
    arr.push_back(item);
  }
  j["albums"] = arr;
  res.setBody(JsonTool::ToString(j));
  return res;
}

Fei::Http::FHttpResponse
PhotoWallController::GetAlbum(const Fei::Http::FHttpRequest &req,
                              const Fei::Http::FPathVar &var) {
  Fei::Http::FHttpResponse res;
  auto idStr = var.get("id");
  if (idStr.empty() || !Digital::isNumber(idStr)) {
    res.setBody(JsonTool::ToString(getErrorJson("Id Error")));
    return res;
  }
  uint64_t id = std::stoul(idStr);
  auto albumOpt = DAO::AlbumQuery::QueryAlbumById(id);
  if (!albumOpt.has_value()) {
    res.setBody(JsonTool::ToString(getErrorJson("Album not exist")));
    return res;
  }
  auto j = Fei::Http::FReflect::fromClass(albumOpt.value());
  j["result"] = ApiOk;
  res.setBody(JsonTool::ToString(j));
  return res;
}

Fei::Http::FHttpResponse
PhotoWallController::GetPhotosByAlbum(const Fei::Http::FHttpRequest &req,
                                      const Fei::Http::FPathVar &var) {
  Fei::Http::FHttpResponse res;
  auto idStr = var.get("id");
  if (idStr.empty() || !Digital::isNumber(idStr)) {
    res.setBody(JsonTool::ToString(getErrorJson("Id Error")));
    return res;
  }
  uint64_t albumId = std::stoul(idStr);
  auto pageStr = var.get("page");
  int page = pageStr.empty() || !Digital::isNumber(pageStr) ? 1 : std::max(1, std::stoi(pageStr));
  int perPage = 12;
  auto photos = DAO::PhotoQuery::QueryPhotosByAlbumId(albumId, page - 1, perPage);
  int totalCount = DAO::PhotoQuery::QueryPhotoCountByAlbumId(albumId);
  int totalPages = std::max(1, (int)std::ceil((float)totalCount / perPage));
  nlohmann::json j;
  j["result"] = ApiOk;
  nlohmann::json arr = nlohmann::json::array();
  for (auto &p : photos) {
    arr.push_back(Fei::Http::FReflect::fromClass(p));
  }
  j["photos"] = arr;
  j["pageNums"] = totalPages;
  j["currentPage"] = page;
  res.setBody(JsonTool::ToString(j));
  return res;
}

Fei::Http::FHttpResponse
PhotoWallController::GetAllPhotos(const Fei::Http::FHttpRequest &req,
                                  const Fei::Http::FPathVar &var) {
  Fei::Http::FHttpResponse res;
  auto pageStr = var.get("page");
  int page = pageStr.empty() || !Digital::isNumber(pageStr) ? 1 : std::max(1, std::stoi(pageStr));
  int perPage = 12;
  auto photos = DAO::PhotoQuery::QueryAllPhotos(page - 1, perPage);
  int totalCount = DAO::PhotoQuery::QueryAllPhotoCount();
  int totalPages = std::max(1, (int)std::ceil((float)totalCount / perPage));
  nlohmann::json j;
  j["result"] = ApiOk;
  nlohmann::json arr = nlohmann::json::array();
  for (auto &p : photos) {
    arr.push_back(Fei::Http::FReflect::fromClass(p));
  }
  j["photos"] = arr;
  j["pageNums"] = totalPages;
  j["currentPage"] = page;
  res.setBody(JsonTool::ToString(j));
  return res;
}

Fei::Http::FHttpResponse
PhotoWallController::GetPhoto(const Fei::Http::FHttpRequest &req,
                              const Fei::Http::FPathVar &var) {
  Fei::Http::FHttpResponse res;
  auto idStr = var.get("id");
  if (idStr.empty() || !Digital::isNumber(idStr)) {
    res.setBody(JsonTool::ToString(getErrorJson("Id Error")));
    return res;
  }
  uint64_t id = std::stoul(idStr);
  auto photoOpt = DAO::PhotoQuery::QueryPhotoById(id);
  if (!photoOpt.has_value()) {
    res.setBody(JsonTool::ToString(getErrorJson("Photo not exist")));
    return res;
  }
  auto j = Fei::Http::FReflect::fromClass(photoOpt.value());
  j["result"] = ApiOk;
  res.setBody(JsonTool::ToString(j));
  return res;
}

Fei::Http::FHttpResponse
PhotoWallController::ViewPhoto(const Fei::Http::FHttpRequest &req,
                               const Fei::Http::FPathVar &var) {
  Fei::Http::FHttpResponse res;
  auto idStr = var.get("id");
  if (idStr.empty() || !Digital::isNumber(idStr)) {
    res.setBody(JsonTool::ToString(getErrorJson("Id Error")));
    return res;
  }
  uint64_t id = std::stoul(idStr);
  DAO::PhotoQuery::incrementPhotoView(id);
  res.setBody(JsonTool::ToString(getSucc()));
  return res;
}

Fei::Http::FHttpResponse
PhotoWallController::PostAlbum(const Fei::Http::FHttpRequest &req,
                               const Fei::Http::FPathVar &var) {
  Fei::Http::FHttpResponse res;
  if (!AdminLogin::instance()->isLogin(req)) {
    res.setBody(JsonTool::ToString(getErrorJson("Not login")));
    return res;
  }
  nlohmann::json json = JsonTool::ToJson(req.getRequestBody());
  uint64_t id = 0;
  auto idItor = json.find("id");
  if (idItor != json.end()) {
    if (idItor->is_string()) {
      auto idStr = idItor->get<std::string>();
      if (!Digital::isNumber(idStr)) {
        res.setBody(JsonTool::ToString(getErrorJson("Id Error")));
        return res;
      }
      id = std::stoull(idStr);
    } else {
      id = idItor->get<uint64_t>();
    }
  }
  Model::Album album;
  album.id = id;
  album.name = JsonTool::get(json, "name", std::string());
  album.description = JsonTool::get(json, "description", std::string());
  album.cover_url = JsonTool::get(json, "cover_url", std::string());
  album.sort_order = JsonTool::get(json, "sort_order", 0);
  std::optional<std::string> err;
  if (id == 0) {
    album.created_at = TimeHelper::getCurrentTimeFromEpochMills();
    err = DAO::AlbumQuery::InsertAlbum(album);
  } else {
    err = DAO::AlbumQuery::UpdateAlbum(album);
  }
  if (err.has_value()) {
    res.setBody(JsonTool::ToString(getErrorJson(err.value())));
    return res;
  }
  res.setBody(JsonTool::ToString(getSucc()));
  return res;
}

Fei::Http::FHttpResponse
PhotoWallController::DeleteAlbum(const Fei::Http::FHttpRequest &req,
                                 const Fei::Http::FPathVar &var) {
  Fei::Http::FHttpResponse res;
  if (!AdminLogin::instance()->isLogin(req)) {
    res.setBody(JsonTool::ToString(getErrorJson("Not login")));
    return res;
  }
  auto idStr = var.get("id");
  if (idStr.empty()) {
    res.setBody(JsonTool::ToString(getErrorJson("No id error!")));
    return res;
  }
  if (!Digital::isNumber(idStr)) {
    res.setBody(JsonTool::ToString(getErrorJson("Id Error!")));
    return res;
  }
  uint64_t id = std::stoul(idStr);
  auto err = DAO::AlbumQuery::DeleteAlbum(id);
  if (err.has_value()) {
    res.setBody(JsonTool::ToString(getErrorJson(err.value())));
    return res;
  }
  res.setBody(JsonTool::ToString(getSucc()));
  return res;
}

Fei::Http::FHttpResponse
PhotoWallController::PostPhoto(const Fei::Http::FHttpRequest &req,
                               const Fei::Http::FPathVar &var) {
  Fei::Http::FHttpResponse res;
  if (!AdminLogin::instance()->isLogin(req)) {
    res.setBody(JsonTool::ToString(getErrorJson("Not login")));
    return res;
  }
  nlohmann::json json = JsonTool::ToJson(req.getRequestBody());
  uint64_t id = 0;
  auto idItor = json.find("id");
  if (idItor != json.end()) {
    if (idItor->is_string()) {
      auto idStr = idItor->get<std::string>();
      if (!Digital::isNumber(idStr)) {
        res.setBody(JsonTool::ToString(getErrorJson("Id Error")));
        return res;
      }
      id = std::stoull(idStr);
    } else {
      id = idItor->get<uint64_t>();
    }
  }
  Model::Photo photo;
  photo.id = id;
  photo.album_id = JsonTool::get(json, "album_id", (uint64_t)0);
  photo.url = JsonTool::get(json, "url", std::string());
  photo.caption = JsonTool::get(json, "caption", std::string());
  photo.sort_order = JsonTool::get(json, "sort_order", 0);
  photo.title = JsonTool::get(json, "title", std::string());
  photo.description = JsonTool::get(json, "description", std::string());
  std::optional<std::string> err;
  uint64_t refreshId = 0;
  const std::string refreshUrl = photo.url;
  if (id == 0) {
    photo.created_at = TimeHelper::getCurrentTimeFromEpochMills();
    photo.view_times = 0;
    err = DAO::PhotoQuery::InsertPhoto(photo);
    if (!err.has_value()) {
      // InsertPhoto doesn't return the new id; read it back.
      auto newPhoto = DAO::PhotoQuery::QueryPhotoByAlbumAndUrl(photo.album_id,
                                                               photo.url);
      if (newPhoto.has_value())
        refreshId = newPhoto->id;
    }
  } else {
    auto existing = DAO::PhotoQuery::QueryPhotoById(id);
    if (existing.has_value() && existing->url != photo.url)
      refreshId = id; // url changed -> re-detect dimensions
    err = DAO::PhotoQuery::UpdatePhoto(photo);
  }
  if (err.has_value()) {
    res.setBody(JsonTool::ToString(getErrorJson(err.value())));
    return res;
  }
  if (refreshId != 0 && !refreshUrl.empty()) {
    Utils::imagePool().submit([refreshId, refreshUrl]() {
      int w = 0, h = 0;
      if (Utils::readImageDims(refreshUrl, w, h)) {
        DAO::PhotoQuery::UpdatePhotoDims(refreshId, w, h);
      }
    });
  }
  res.setBody(JsonTool::ToString(getSucc()));
  return res;
}

Fei::Http::FHttpResponse
PhotoWallController::DeletePhoto(const Fei::Http::FHttpRequest &req,
                                 const Fei::Http::FPathVar &var) {
  Fei::Http::FHttpResponse res;
  if (!AdminLogin::instance()->isLogin(req)) {
    res.setBody(JsonTool::ToString(getErrorJson("Not login")));
    return res;
  }
  auto idStr = var.get("id");
  if (idStr.empty()) {
    res.setBody(JsonTool::ToString(getErrorJson("No id error!")));
    return res;
  }
  if (!Digital::isNumber(idStr)) {
    res.setBody(JsonTool::ToString(getErrorJson("Id Error!")));
    return res;
  }
  uint64_t id = std::stoul(idStr);
  auto err = DAO::PhotoQuery::DeletePhoto(id);
  if (err.has_value()) {
    res.setBody(JsonTool::ToString(getErrorJson(err.value())));
    return res;
  }
  res.setBody(JsonTool::ToString(getSucc()));
  return res;
}

} // namespace Blog
