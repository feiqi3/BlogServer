#include <iostream>
#include <re2/re2.h>
#include <string>
#include <sstream>
#include <functional>
#include "Http/FPathMatcher.h"
#include "Http/FRouter.h"
#include "Http/FController.h"
#include "FDef.h"
#include "FeiLibIniter.h"
#include "Http/FHttpRequest.h"
#include "FBuffer.h"
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

//Test path matcher woring?
void test01();
//Test controller register and so on.
void test02();
int main() {
	test02();
}

void test01() {
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
	if (!isMatch)std::exit(-1);
	assert(isMatch);
}

class ControllerTest : public Fei::Http::FControllerBase {
public:
	ControllerTest():FControllerBase("test") {
	}
	Fei::Http::FHttpResponse map_abc(const Fei::Http::FHttpRequest& inRequest, const Fei::Http::PathVarMap& pathVar) {
		std::cout << "Method Get, /abc \n";
		return Fei::Http::FHttpResponse{};
	}
	Fei::Http::FHttpResponse map_var(const Fei::Http::FHttpRequest& inRequest, const Fei::Http::PathVarMap& pathVar) {
		std::cout << "Method Post, /{id}/hello/path/{uv}"<<" with variable: ";
		for (auto&& [key, val] : pathVar) {
			std::cout << key << " = " << val << " ";
		}
		std::cout << "\n";
		return Fei::Http::FHttpResponse{};
	}

	void registerMapping() override {
		addControllerMappingPath("");
		REGISTER_MAPPING_FUNC(Fei::Http::Method::GET, "/abc", ControllerTest, map_abc);
		REGISTER_MAPPING_FUNC(Fei::Http::Method::POST, "/{id}/hello/path/{uv}", ControllerTest, map_var);
	}
	~ControllerTest() {}
};
void test02()
{
	using namespace Fei;
	FeiLibInit();

	using namespace Fei::Http;
	FBuffer buffer(100);
	FBufferReader reader(buffer);
	FHttpRequest request(reader);
	REGISTER_CONTROLLER_CLASS_INLINE(ControllerTest)
	auto matched = FRouter::instance()->route(Method::GET, "/abc");
	if (matched.isvalid()) {
		matched.controllerFunc(request, matched.pathVariable);
	}
	matched = FRouter::instance()->route(Method::POST, "/abc/hello/path/cde");
	if (matched.isvalid()) {
		matched.controllerFunc(request, matched.pathVariable);
	}

	FRouter::UnRegisterController("test");
	matched = FRouter::instance()->route(Method::POST, "/abc/hello/path/cde");
	if (matched.isvalid()) {
		matched.controllerFunc(request, matched.pathVariable);
	}
}