#include "Model/TestMessages.h"
#include <cstdint>
#include <optional>
#include <string>
#include <tuple>
#include <vector>
namespace Blog::DAO {
    class TestMessageQuery{
        public:
        static std::optional<std::string> InsertMessage(const TestMessage& message);
        static std::vector<std::tuple<std::string,std::string>> QueryMessageByPage(uint32_t pageNum,uint32_t perPage);
        static uint32_t QueryMessagePageNum();
    };
}