#pragma once
#include "Model/Albums.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
namespace Blog::DAO {
class AlbumQuery {
public:
  static std::optional<Model::Album> QueryAlbumById(uint64_t id);
  static std::vector<Model::Album> QueryAllAlbums();
  static std::optional<std::string> InsertAlbum(const Model::Album& album);
  static std::optional<std::string> UpdateAlbum(const Model::Album& album);
  static std::optional<std::string> DeleteAlbum(uint64_t id);
  static int QueryPhotoCountInAlbum(uint64_t albumId);
};
} // namespace Blog::DAO
