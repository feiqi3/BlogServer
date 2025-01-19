#include <iostream>
#include <re2/re2.h>
#include <string>
#include <sstream>
#include "Http/FPathMatcher.h"
#include "FDef.h"
uint64_t matchAt(const absl::string_view& piece, const std::string& pieceFrom) {
	return piece.data() - pieceFrom.data();
}

int main() {
    std::string srcText = "*foo?bar*test{1,2}baz ? {abc} *";
	std::string newText = "assfoobbartexttest{123}baz c {4656} aaabbbb";
    re2::RE2 AntStyleMatchPattern("(\\?)|(\\*)|\\{([^/{}\\\\]+|\\\\[{}])+\\}");  // 正则表达式
	std::string VarMatchPattern = "(.*)";
	int end = 0;
	absl::string_view text = srcText;
	re2::StringPiece matchGroup[3]{};
	std::stringstream ss;
	while (RE2::FindAndConsume(&text, AntStyleMatchPattern, &matchGroup[0], &matchGroup[1], &matchGroup[2]))
	{
		auto begin = 0;
		//Match '?'
		if (!matchGroup[0].empty()) {
			begin = matchAt(matchGroup[0], srcText);
			ss << std::string_view(srcText.begin() + end, srcText.begin() + begin) << ".";
			end = begin + matchGroup[0].size();
		}
		//Match '*'
		else if (!matchGroup[1].empty()) {
			begin = matchAt(matchGroup[1], srcText);
			ss << std::string_view(srcText.begin() + end, srcText.begin() + begin) << ".*";
			end = begin + matchGroup[1].size();
		}
		//Match {} variable
		else if (!matchGroup[2].empty()) {
			begin = matchAt(matchGroup[2], srcText);
			ss << std::string_view(srcText.begin() + end, srcText.begin() + begin) << std::string_view(VarMatchPattern);
			end = begin + matchGroup[2].size();
		}
	}
	std::cout<<"New pattern " << ss.str()<<"\n";
	text = newText;
	RE2  nePattern(ss.str());
	while (RE2::Consume(&text, nePattern))
	{
		std::cout << "matched!";
	}
    return 0;
}