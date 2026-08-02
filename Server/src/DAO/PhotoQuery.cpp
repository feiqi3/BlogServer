#include "PhotoQuery.h"
#include "DAO/DataBaseOperation.h"
#include "DAO/ORM.h"
#include "Model/Photos.h"
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Blog::DAO {
std::optional<Model::Photo>
PhotoQuery::QueryPhotoById(uint64_t id) {
  auto getQuery = []() {
    Query<Model::Photo> query;
    query.Select().Where(FIELD(Model::Photo, id) == PARAM).setStaticQuery(true);
    return query;
  };
  thread_local auto query = getQuery();
  query.exec(id);
  auto vec = query.getVector();
  if (vec.size() > 0) {
    return vec[0];
  }
  return std::nullopt;
}

std::optional<Model::Photo>
PhotoQuery::QueryPhotoByAlbumAndUrl(uint64_t albumId,
                                    const std::string& url) {
  auto getQuery = []() {
    Query<Model::Photo> query;
    query.Select()
        .Where((FIELD(Model::Photo, album_id) == PARAM) &&
               (FIELD(Model::Photo, url) == PARAM))
        .OrderByDesc(FIELD(Model::Photo, id))
        .setStaticQuery(true);
    return query;
  };
  thread_local auto query = getQuery();
  query.exec(albumId, url);
  auto vec = query.getVector();
  if (vec.size() > 0) {
    return vec[0];
  }
  return std::nullopt;
}

std::vector<Model::Photo>
PhotoQuery::QueryPhotosByAlbumId(uint64_t albumId, uint64_t pageIdx,
                                 int perPageNum) {
  auto getQuery = []() {
    Query<Model::Photo> q;
    q.Select().Where(FIELD(Model::Photo, album_id) == PARAM).skip(PARAM).limit(PARAM).OrderByDesc(FIELD(Model::Photo, created_at)).setStaticQuery(true);
    return q;
  };
  thread_local auto query = getQuery();
  return query.exec(albumId, perPageNum, pageIdx * perPageNum).getVector();
}

int PhotoQuery::QueryPhotoCountByAlbumId(uint64_t albumId) {
  auto getQuery = []() {
    Query<int> q;
    q.Select(SelectCount()).From("Photos").Where(FIELD(Model::Photo, album_id) == PARAM).setStaticQuery(true);
    return q;
  };
  thread_local auto query = getQuery();
  return query.exec(albumId).getVector()[0];
}

std::vector<Model::Photo>
PhotoQuery::QueryAllPhotos(uint64_t pageIdx, int perPageNum) {
  auto getQuery = []() {
    Query<Model::Photo> q;
    q.Select().skip(PARAM).limit(PARAM).OrderByDesc(FIELD(Model::Photo, created_at)).setStaticQuery(true);
    return q;
  };
  thread_local auto query = getQuery();
  return query.exec(perPageNum, pageIdx * perPageNum).getVector();
}

int PhotoQuery::QueryAllPhotoCount() {
  auto getQuery = []() {
    Query<int> q;
    q.Select(SelectCount()).From("Photos").setStaticQuery(true);
    return q;
  };
  thread_local auto query = getQuery();
  return query.exec().getVector()[0];
}

std::optional<std::string>
PhotoQuery::InsertPhoto(const Model::Photo& photo) {
  Query<Model::Photo> query;
  query.InsertWithAutoIncPk(photo);
  auto ptr = query.exec(photo).getResult();
  bool res = ptr->excute();
  if (res)
    return std::nullopt;
  return ptr->getErrMsg();
}

std::optional<std::string>
PhotoQuery::UpdatePhoto(const Model::Photo& photo) {
  auto getQuery = []() {
    Query<Model::Photo> query;
    query
        .update((FIELD(Model::Photo, album_id) == PARAM) -
                (FIELD(Model::Photo, url) == PARAM) -
                (FIELD(Model::Photo, caption) == PARAM) -
                (FIELD(Model::Photo, sort_order) == PARAM) -
                (FIELD(Model::Photo, title) == PARAM) -
                (FIELD(Model::Photo, description) == PARAM))
        .Where(FIELD(Model::Photo, id) == PARAM).setStaticQuery(true);
    return query;
  };
  thread_local auto query = getQuery();
  auto ptr = query.exec(photo.album_id, photo.url, photo.caption,
                        photo.sort_order, photo.title, photo.description,
                        photo.id)
                 .getResult();
  bool res = ptr->excute();
  if (res)
    return std::nullopt;
  return ptr->getErrMsg();
}

std::optional<std::string>
PhotoQuery::UpdatePhotoDims(uint64_t id, int width, int height) {
  auto getQueryStatement = []() {
    auto ret = DatabaseOperation::instance()->Prepare(
        "UPDATE Photos SET width=?, height=? WHERE id=?");
    DatabaseOperation::addThreadCleanDBData(ret);
    return ret;
  };
  thread_local auto q = getQueryStatement();
  DatabaseOperation::instance()->ReExec(q, width, height, id);
  bool res = q->excute();
  if (res)
    return std::nullopt;
  return q->getErrMsg();
}

std::vector<std::pair<uint64_t, std::string>>
PhotoQuery::QueryPhotosMissingDims() {
  auto getQueryStatement = []() {
    auto ret = DatabaseOperation::instance()->Prepare(
        "SELECT id, url FROM Photos WHERE width = 0 OR height = 0");
    DatabaseOperation::addThreadCleanDBData(ret);
    return ret;
  };
  thread_local auto q = getQueryStatement();
  DatabaseOperation::instance()->ReExec(q);
  std::vector<std::pair<uint64_t, std::string>> ret;
  while (q->step()) {
    uint64_t id = (uint64_t)q->getInteger64(0);
    const char* url = q->getString(1);
    ret.emplace_back(id, url ? url : "");
  }
  return ret;
}

std::optional<std::string>
PhotoQuery::DeletePhoto(uint64_t id) {
  auto getQuery = []() {
    Query<Model::Photo> query;
    query.Delete().Where(FIELD(Model::Photo, id) == PARAM).setStaticQuery(true);
    return query;
  };
  thread_local auto query = getQuery();
  auto ptr = query.exec(id).getResult();
  bool res = ptr->excute();
  if (res)
    return std::nullopt;
  return ptr->getErrMsg();
}

void PhotoQuery::incrementPhotoView(uint64_t id) {
  auto getQueryStatement = []() {
    auto ret = DatabaseOperation::instance()->Prepare(
        "UPDATE Photos SET view_times = view_times + 1 where id = ?");
    DatabaseOperation::addThreadCleanDBData(ret);
    return ret;
  };
  thread_local auto q = getQueryStatement();
  DatabaseOperation::instance()->ReExec(q, id);
  q->excute();
}

} // namespace Blog::DAO
