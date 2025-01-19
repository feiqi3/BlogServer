#include <sstream>
#include "Http/FPathMatcher.h"
#include "FDef.h"
// DOOOO Not Use std::regex!!!
#include "re2/re2.h"

uint64_t matchAt(const absl::string_view& piece, const std::string& pieceFrom) {
	return piece.data() - pieceFrom.data();
}

//using namespace re2;
namespace Fei::Http {

RE2 AntStyleMatchPattern = ("(\\?)|(\\*)|\\{([^/{}\\\\]+|\\\\[{}])+\\}");
std::string VarMatchPattern = "(.*)";
FPathMatcher::FPathMatcher(const std::string& pattern,bool caseSensitive) {
	int end = 0;
	absl::string_view text = pattern;
	re2::StringPiece matchGroup[3]{};
	std::stringstream ss;
	while (RE2::FindAndConsume(&text, AntStyleMatchPattern, &matchGroup[0], &matchGroup[1], &matchGroup[2])) 
	{
		auto begin = 0;
		//Match '?'
		if (!matchGroup[0].empty()) {
			begin = matchAt(matchGroup[0],pattern);
			ss << std::string_view(pattern.begin() + end, pattern.begin() + begin)<<".";
			end = begin + matchGroup[0].size();
		}
		//Match '*'
		else if (!matchGroup[1].empty()) {
			begin = matchAt(matchGroup[1], pattern);
			ss << std::string_view(pattern.begin() + end, pattern.begin() + begin) << ".*";
			end = begin + matchGroup[1].size();
		}
		//Match {} variable
		else if (!matchGroup[2].empty()) {
			begin = matchAt(matchGroup[2], pattern);
			std::string_view view(pattern.begin() + end, pattern.begin() + begin);
			ss << view << std::string_view(VarMatchPattern);
			varNames.push_back(std::string(view));
			end = begin + matchGroup[2].size();
		}
	}
	if (caseSensitive)
		mPattern = std::make_unique<RE2>(ss.str(), RE2::Options::case_sensitive);
	else 
		mPattern = std::make_unique<RE2>(ss.str());
	assert(mPattern->error_code() == RE2::NoError);
}

}; // namespace Fei::Http
