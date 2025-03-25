#ifndef CATEGORIESQUERY_H_
#define CATEGORIESQUERY_H_
#include <vector>
#include <string>
#include "Model/Categories.h"
namespace Blog::DAO {
    class CategoriesQuery {
    public:
        static std::vector<std::string> queryAllCategroyNames();
        static std::vector<Model::Category> queryAllCategrories();
    };
};
#endif