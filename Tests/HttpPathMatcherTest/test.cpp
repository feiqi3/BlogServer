#include <iostream>
#include <re2/re2.h>
#include <string>

int main() {
    std::string text = "foo?bar*test{1,2}baz ? {abc} * *";
    re2::RE2 pattern("(\\?)|(\\*)|\\{([^/{}\\\\]+|\\\\[{}])+\\}");  // 正则表达式

    re2::StringPiece input(text);
    re2::StringPiece match1, match2, match3;

    // 使用 RE2::PartialMatch 来检查捕获组 
    // 注意: 因为本正则有三个捕获组，对于Re2，他会每次按照正则中的顺序把捕获到的字符放入参数中 
    while (RE2::FindAndConsume(&input, pattern, &match1, &match2, &match3)) {
        if (!match1.empty()) {
            std::cout << "Captured ?: " << match1 << std::endl;  // 捕获问号
        }
        if (!match2.empty()) {
            std::cout << "Captured *: " << match2 << std::endl;  // 捕获星号
        }
        if (!match3.empty()) {
            std::cout << "Captured {} content: " << match3 << std::endl;  // 捕获大括号内容
        }
    }

    return 0;
}