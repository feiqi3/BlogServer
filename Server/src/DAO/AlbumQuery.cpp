#include "AlbumQuery.h"
#include "DAO/ORM.h"
#include "Model/Albums.h"
#include "Model/Photos.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Blog::DAO {
std::optional<Model::Album>
AlbumQuery::QueryAlbumById(uint64_t id) {
  auto getQuery = []() {
    Query<Model::Album> query;
    query.Select().Where(FIELD(Model::Album, id) == PARAM).setStaticQuery(true);
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

std::vector<Model::Album> AlbumQuery::QueryAllAlbums() {
  auto getQuery = []() {
    Query<Model::Album> q;
    q.Select().OrderBy(FIELD(Model::Album, sort_order)).setStaticQuery(true);
    return q;
  };
  thread_local auto q = getQuery();
  return q.exec().getVector();
}

std::optional<std::string>
AlbumQuery::InsertAlbum(const Model::Album& album) {
  Query<Model::Album> query;
  query.InsertWithAutoIncPk(album);
  auto ptr = query.exec(album).getResult();
  bool res = ptr->excute();
  if (res)
    return std::nullopt;
  return ptr->getErrMsg();
}

std::optional<std::string>
AlbumQuery::UpdateAlbum(const Model::Album& album) {
  auto getQuery = []() {
    Query<Model::Album> query;
    query
        .update((FIELD(Model::Album, name) == PARAM) -
                (FIELD(Model::Album, description) == PARAM) -
                (FIELD(Model::Album, cover_url) == PARAM) -
                (FIELD(Model::Album, sort_order) == PARAM))
        .Where(FIELD(Model::Album, id) == PARAM).setStaticQuery(true);
    return query;
  };
  thread_local auto query = getQuery();
  auto ptr = query.exec(album.name, album.description, album.cover_url,
                        album.sort_order, album.id)
                 .getResult();
  bool res = ptr->excute();
  if (res)
    return std::nullopt;
  return ptr->getErrMsg();
}

std::optional<std::string>
AlbumQuery::DeleteAlbum(uint64_t id) {
  auto getQuery = []() {
    Query<Model::Album> query;
    query.Delete().Where(FIELD(Model::Album, id) == PARAM).setStaticQuery(true);
    return query;
  };
  thread_local auto query = getQuery();
  auto ptr = query.exec(id).getResult();
  bool res = ptr->excute();
  if (res)
    return std::nullopt;
  return ptr->getErrMsg();
}

int AlbumQuery::QueryPhotoCountInAlbum(uint64_t albumId) {
  auto getQuery = []() {
    Query<int> q;
    q.Select(SelectCount()).From("Photos").Where(FIELD(Model::Photo, album_id) == PARAM).setStaticQuery(true);
    return q;
  };
  thread_local auto query = getQuery();
  return query.exec(albumId).getVector()[0];
}

} // namespace Blog::DAO
