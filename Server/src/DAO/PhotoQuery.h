#pragma once
#include "Model/Photos.h"
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>
namespace Blog::DAO {
class PhotoQuery {
public:
  static std::optional<Model::Photo> QueryPhotoById(uint64_t id);
  // returns the most recent photo matching album_id + url
  static std::optional<Model::Photo> QueryPhotoByAlbumAndUrl(uint64_t albumId, const std::string& url);
  // returns vector of full Photo entities for an album, paginated
  static std::vector<Model::Photo> QueryPhotosByAlbumId(uint64_t albumId, uint64_t pageIdx, int perPageNum = 12);
  static int QueryPhotoCountByAlbumId(uint64_t albumId);
  // returns all photos across all albums, paginated by created_at desc
  static std::vector<Model::Photo> QueryAllPhotos(uint64_t pageIdx, int perPageNum = 12);
  static int QueryAllPhotoCount();
  static std::optional<std::string> InsertPhoto(const Model::Photo& photo);
  static std::optional<std::string> UpdatePhoto(const Model::Photo& photo);
  static std::optional<std::string> UpdatePhotoDims(uint64_t id, int width, int height);
  // returns {id, url} pairs for photos whose dimensions were never detected
  static std::vector<std::pair<uint64_t, std::string>> QueryPhotosMissingDims();
  static std::optional<std::string> DeletePhoto(uint64_t id);
  static void incrementPhotoView(uint64_t id);
};
} // namespace Blog::DAO
