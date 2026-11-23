#pragma once
#include "Model/Posts.h"
#include <nlohmann/json.hpp>
#include <functional>
#include <optional>
namespace Blog::Script {
using ArticleJson=nlohmann::json;
struct ArticleDraft {
    ArticleJson input;
    std::optional<ArticleJson> expected;
    static ArticleDraft prepare(ArticleJson input);
    void apply(const ArticleJson& patch);
    uint64_t save(const std::function<bool()>& cancelled)const;
    ArticleJson event()const;
};
uint64_t modelId(const ArticleJson& value);
ArticleJson modelPost(const Model::Post& post);
Model::Post patchArticleView(Model::Post post,const ArticleJson& patch);
}
