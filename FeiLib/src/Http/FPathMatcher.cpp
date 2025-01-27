#include <sstream>
#include "Http/FPathMatcher.h"
#include "FDef.h"
// DOOOO Not Use std::regex!!!
#include "re2/re2.h"
#include "Http/FPathVar.h"
namespace {

	uint64_t matchAt(const absl::string_view& piece, const std::string& pieceFrom) {
		return piece.data() - pieceFrom.data();
	}

	std::string preFilterPattern(const std::string& pattern) {
		uint32_t stars = 0;
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

}


//using namespace re2;
namespace Fei::Http {

RE2 AntStyleMatchPattern = ("(@)|(\\?)|(\\*)|\\{([^/{}\\\\]+|\\\\[{}])+\\}");
FPathMatcher::FPathMatcher(const std::string& pattern,bool caseSensitive):mOriginPattern(pattern) {
	if (pattern.size() > MaxPathLengthMatcherSupport) {
		throw std::exception("Invalid path pattern");
	}
	int end = 0;
	auto filteredPattern = preFilterPattern(pattern);
	absl::string_view text = filteredPattern;
	re2::StringPiece matchGroup[4]{};
	std::stringstream ss;
	while (RE2::FindAndConsume(&text, AntStyleMatchPattern, &matchGroup[0], &matchGroup[1], &matchGroup[2], &matchGroup[3]))
	{
		auto begin = 0ull;
		//Match '**' 
		if (!matchGroup[0].empty()) {
			begin = matchAt(matchGroup[0], filteredPattern);
			ss << std::string_view(filteredPattern.begin() + end, filteredPattern.begin() + begin) << ".*";
			end = begin + matchGroup[0].size();
			mWildCardsNums++;
		}
		//Match '?'
		if (!matchGroup[1].empty()) {
			begin = matchAt(matchGroup[1], filteredPattern);
			ss << std::string_view(filteredPattern.begin() + end, filteredPattern.begin() + begin)<<".";
			end = begin + matchGroup[1].size();
			mUndecidedChars++;
		}
		//Match '*'
		else if (!matchGroup[2].empty()) {
			begin = matchAt(matchGroup[2], filteredPattern);
			ss << std::string_view(filteredPattern.begin() + end, filteredPattern.begin() + begin) << "[^/]*";
			end = begin + matchGroup[2].size();
			mWildCardsNums++;
		}
		//Match {} variable
		else if (!matchGroup[3].empty()) {
			begin = matchAt(matchGroup[3], filteredPattern);
			std::string_view view(filteredPattern.begin() + end, filteredPattern.begin() + begin - 1);
			ss << view <<"([^/]*)";
			varNames.push_back(std::string(matchGroup[3]));
			end = begin + matchGroup[3].size() + 1;
		}
	}
	ss << std::string_view(pattern.begin() + end, pattern.begin() + pattern.size());
	RE2::Options opts;
	if (caseSensitive) {
		opts.set_case_sensitive(true);
	}
	mPattern = std::make_unique<RE2>(ss.str(), opts);
	assert(mPattern->error_code() == RE2::NoError);
}

bool FPathMatcher::isMatch(const std::string& str, FPathVar& vars)
{
	absl::string_view view(str);
	std::vector<absl::string_view> varViews(this->varNames.size());
	std::vector<RE2::Arg*> args(this->varNames.size());
	for (auto i = 0; i < args.size(); ++i) {
		args[i] = new RE2::Arg(&varViews[i]);
	}

	bool isMatch = RE2::FullMatchN(view, *mPattern, args.data(), int(this->varNames.size()));
	for (auto i = 0; i < args.size(); ++i) {
		if (!varViews[i].empty()) {
			vars.setVar( varNames[i],std::string(varViews[i]) );
		}
		delete args[i];
	}
	return isMatch;
}
FPathMatcher::~FPathMatcher()
{
}
}; // namespace Fei::Http
