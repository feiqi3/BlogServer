#include <iostream>
#include <re2/re2.h>
#include <string>
#include <sstream>
#include "Http/FPathMatcher.h"
#include "FDef.h"
uint64_t matchAt(const absl::string_view& piece, const std::string& pieceFrom) {
	return piece.data() - pieceFrom.data();
}

std::string preFilterPattern(const std::string& pattern) {
	uint32_t stars = 0;
	uint32_t end = 0;
	//Is Pattern valid?
	for (auto i = 0; i < pattern.size(); ++i) {
		if (pattern[i] != '*') {
			stars = 0;
		}
		else {
			stars++;
			if (stars > 2) {
				throw std::exception("Invalid path pattern");
			}

		}
	}
	static RE2 _replace("\\*\\*");
	std::string _ret(pattern);
	RE2::GlobalReplace(&_ret, _replace, "@");
	return _ret;
}

int main() {
    std::string srcText = "/test/*/{id}/**/{ijg}";
	std::string newText = "/test/123/9985/asdas/gdsdsfasd/12354";
    re2::RE2 AntStyleMatchPattern("(\\@)|(\\?)|(\\*)|\\{([^/{}\\\\]+|\\\\[{}])+\\}");  // 正则表达式
	std::string VarMatchPattern = "([^/]*)";

	int end = 0;
	auto filteredPattern = preFilterPattern(srcText);
	absl::string_view text = filteredPattern;
	re2::StringPiece matchGroup[4]{};
	std::stringstream ss;
	std::vector<std::string> varNames;
	while (RE2::FindAndConsume(&text, AntStyleMatchPattern, &matchGroup[0], &matchGroup[1], &matchGroup[2], &matchGroup[3]))
	{
		auto begin = 0;
		//Match '**' 
		if (!matchGroup[0].empty()) {
			begin = matchAt(matchGroup[0], filteredPattern);
			ss << std::string_view(filteredPattern.begin() + end, filteredPattern.begin() + begin) << ".*";
			end = begin + matchGroup[0].size();
		}
		//Match '?'
		if (!matchGroup[1].empty()) {
			begin = matchAt(matchGroup[1], filteredPattern);
			ss << std::string_view(filteredPattern.begin() + end, filteredPattern.begin() + begin) << ".";
			end = begin + matchGroup[1].size();
		}
		//Match '*'
		else if (!matchGroup[2].empty()) {
			begin = matchAt(matchGroup[2], filteredPattern);
			ss << std::string_view(filteredPattern.begin() + end, filteredPattern.begin() + begin) << "[^/]*";
			end = begin + matchGroup[2].size();
		}
		//Match {} variable
		else if (!matchGroup[3].empty()) {
			begin = matchAt(matchGroup[3], filteredPattern);
			std::string_view view(filteredPattern.begin() + end, filteredPattern.begin() + begin - 1);
			ss << view << std::string_view(VarMatchPattern);
			std::cout << "Var Name: " << matchGroup[3] << "\n";
			varNames.push_back(std::string(matchGroup[3]));
			end = begin + matchGroup[3].size() + 1;
		}
	}
	ss << std::string_view(filteredPattern.begin() + end, filteredPattern.begin() + filteredPattern.size());
	auto newPattern = ss.str();
	RE2::Options opts;
	opts.set_case_sensitive(true);
	auto pattern = std::make_unique<RE2>(newPattern, opts);
	absl::string_view view(newText);
	std::vector<absl::string_view> varViews(varNames.size());
	std::vector<RE2::Arg*> args(varNames.size());
	for (auto i = 0; i < args.size(); ++i) {
		args[i] = new RE2::Arg(&varViews[i]);
	}

	bool isMatch = RE2::FullMatchN(view, *pattern, args.data(), varNames.size());
	for (auto i = 0; i < args.size(); ++i) {
		if (!varViews[i].empty()) {
			std::cout << "var: " << varViews[i] << " ";
		}
		delete args[i];
	}
}