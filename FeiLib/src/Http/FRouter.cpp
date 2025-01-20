#include "Http/FRouter.h"
#include "Http/FPathMatcher.h"
namespace Fei::Http {
	uint64 FRouter::calcPathPatternPriority(FPathMatcher* matcher)
	{
		uint64 priority = MaxPathLengthMatcherSupport;

		priority = priority - matcher->getOriginPattern().size();

		priority = priority + matcher->getUndecidedCharNums() * 100;

		priority = priority + matcher->getWildCardsNums() * 10000;

		return priority;
	}
}