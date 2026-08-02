#pragma once
#include "Http/FController.h"
#include "Http/FHttpRequest.h"
#include "Http/FHttpResponse.h"
#include "Http/FPathVar.h"

namespace Blog {
    class PhotoWallController : public Fei::Http::FControllerBase {
    public:
        PhotoWallController() : Fei::Http::FControllerBase("PhotoWallController") {}

        // GET - public reads
        Fei::Http::FHttpResponse GetAllAlbums(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var);
        Fei::Http::FHttpResponse GetAlbum(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var);
        Fei::Http::FHttpResponse GetPhotosByAlbum(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var);
        Fei::Http::FHttpResponse GetAllPhotos(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var);
        Fei::Http::FHttpResponse GetPhoto(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var);
        Fei::Http::FHttpResponse ViewPhoto(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var);
        // POST/DELETE - auth required
        Fei::Http::FHttpResponse PostAlbum(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var);
        Fei::Http::FHttpResponse DeleteAlbum(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var);
        Fei::Http::FHttpResponse PostPhoto(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var);
        Fei::Http::FHttpResponse DeletePhoto(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var);

        REGISTER_MAPPING_BEGIN("/api")
            REGISTER_MAPPING_FUNC(Fei::Http::Method::GET, "/albums", PhotoWallController, GetAllAlbums);
            REGISTER_MAPPING_FUNC(Fei::Http::Method::GET, "/album/{id}", PhotoWallController, GetAlbum);
            REGISTER_MAPPING_FUNC(Fei::Http::Method::GET, "/albums/{id}/photos", PhotoWallController, GetPhotosByAlbum);
            REGISTER_MAPPING_FUNC(Fei::Http::Method::GET, "/albums/{id}/photos/page={page}", PhotoWallController, GetPhotosByAlbum);
            REGISTER_MAPPING_FUNC(Fei::Http::Method::GET, "/photos", PhotoWallController, GetAllPhotos);
            REGISTER_MAPPING_FUNC(Fei::Http::Method::GET, "/photos/page={page}", PhotoWallController, GetAllPhotos);
            REGISTER_MAPPING_FUNC(Fei::Http::Method::GET, "/photo/{id}", PhotoWallController, GetPhoto);
            REGISTER_MAPPING_FUNC(Fei::Http::Method::GET, "/photo/{id}/view", PhotoWallController, ViewPhoto);
            REGISTER_MAPPING_FUNC(Fei::Http::Method::POST, "/album", PhotoWallController, PostAlbum);
            REGISTER_MAPPING_FUNC(Fei::Http::Method::DELET, "/album/{id}", PhotoWallController, DeleteAlbum);
            REGISTER_MAPPING_FUNC(Fei::Http::Method::POST, "/photo", PhotoWallController, PostPhoto);
            REGISTER_MAPPING_FUNC(Fei::Http::Method::DELET, "/photo/{id}", PhotoWallController, DeletePhoto);
        REGISTER_MAPPING_END
    };
    REGISTER_CONTROLLER_CLASS(PhotoWallController);
};
