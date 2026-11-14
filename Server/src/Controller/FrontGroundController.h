#pragma once

#include "FDef.h"
#include "Http/FController.h"
#include "Http/FHttpRequest.h"
#include "Http/FHttpResponse.h"
#include "Model/Posts.h"

namespace Blog{
    class FrontGroundController: public Fei::Http::FControllerBase {
        public:
        FrontGroundController():Fei::Http::FControllerBase("FrontGroundController"){}
		Fei::Http::FHttpResponse Articles(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var);
		Fei::Http::FHttpResponse ArticleDetail(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var);
		Fei::Http::FHttpResponse Categories(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var);
		Fei::Http::FHttpResponse CategoriesDetail(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var);
		Fei::Http::FHttpResponse About(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var);
		Fei::Http::FHttpResponse Links(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var);
		static Fei::Http::FHttpResponse RenderArticlePost(const Model::Post& post, const std::string& pageUrl);
		Fei::Http::FHttpResponse Archive(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var);
		Fei::Http::FHttpResponse PhotosPage(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var);
		Fei::Http::FHttpResponse ArchivePage(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var);
		Fei::Http::FHttpResponse RssXml(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var);
		Fei::Http::FHttpResponse RobotsTxt(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var);
		Fei::Http::FHttpResponse SitemapXml(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var);
		Fei::Http::FHttpResponse WebManifest(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var);
		Fei::Http::FHttpResponse FaviconIco(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var);
		REGISTER_MAPPING_BEGIN("")
			REGISTER_MAPPING_FUNC(Fei::Http::Method::GET, "/realIndex={page}", FrontGroundController, Articles);
			REGISTER_MAPPING_FUNC(Fei::Http::Method::GET, "/post/{id}", FrontGroundController, ArticleDetail);
			REGISTER_MAPPING_FUNC(Fei::Http::Method::GET, "/category/id={id}&page={page}", FrontGroundController, CategoriesDetail);
			REGISTER_MAPPING_FUNC(Fei::Http::Method::GET, "/category/id={id}", FrontGroundController, CategoriesDetail);
			REGISTER_MAPPING_FUNC(Fei::Http::Method::GET, "/categories", FrontGroundController, Categories);
			REGISTER_MAPPING_FUNC(Fei::Http::Method::GET, "/about", FrontGroundController, About);
			REGISTER_MAPPING_FUNC(Fei::Http::Method::GET, "/links", FrontGroundController, Links);
			REGISTER_MAPPING_FUNC(Fei::Http::Method::GET, "/api/archive", FrontGroundController, Archive);
			REGISTER_MAPPING_FUNC(Fei::Http::Method::GET, "/photos", FrontGroundController, PhotosPage);
			REGISTER_MAPPING_FUNC(Fei::Http::Method::GET, "/archives", FrontGroundController, ArchivePage);
			REGISTER_MAPPING_FUNC(Fei::Http::Method::GET, "/rss.xml", FrontGroundController, RssXml);
			REGISTER_MAPPING_FUNC(Fei::Http::Method::GET, "/robots.txt", FrontGroundController, RobotsTxt);
			REGISTER_MAPPING_FUNC(Fei::Http::Method::GET, "/sitemap.xml", FrontGroundController, SitemapXml);
			REGISTER_MAPPING_FUNC(Fei::Http::Method::GET, "/site.webmanifest", FrontGroundController, WebManifest);
			REGISTER_MAPPING_FUNC(Fei::Http::Method::GET, "/favicon.ico", FrontGroundController, FaviconIco);
			REGISTER_MAPPING_END
    };
	REGISTER_CONTROLLER_CLASS(FrontGroundController);
};
