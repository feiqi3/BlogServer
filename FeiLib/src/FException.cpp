#include <string>
#include <sstream>
#include <vector>

#include "FDef.h"
#include "FException.h"
#include "absl/debugging/stacktrace.h"
#include "absl/debugging/symbolize.h"

#define MAX_STACK_DEPTH 64

std::vector<std::string> F_API Fei::getStackTrace(uint32 skip)
{
	void* stack[MAX_STACK_DEPTH];
	int sizes[MAX_STACK_DEPTH];

	uint32 realStackDepth = absl::GetStackFrames(stack, sizes, MAX_STACK_DEPTH, skip + 1);
	std::vector<std::string> ret;
	char symbol[2048];
	for (auto i = 0; i < realStackDepth; ++i) {
		if(absl::Symbolize(stack[i],symbol , 2048)){
			ret.emplace_back(symbol);
		}else{
			ret.emplace_back("<unknown>");			
		}
	}
	return ret;
}
