#include <string>
#include <sstream>

#include "FDef.h"
#include "FException.h"
#include "absl/debugging/stacktrace.h"

#define MAX_STACK_DEPTH 32

Fei::FException::FException()
{
	void* stack[MAX_STACK_DEPTH];
	int sizes[MAX_STACK_DEPTH];

	uint32 realStackDepth = absl::GetStackFrames(stack, sizes, MAX_STACK_DEPTH, 1);
	std::stringstream ss;
	ss << "Calling stack:";
	for (auto i = 0; i < realStackDepth; ++i) {
		ss << "\n" << std::string((char*)stack[i], sizes[i]);
	}
	mStackTace = ss.str();
}

const char* Fei::FException::what() const
{
	mErrorStr = reason() + std::string("\n") + mErrorStr;
	return mErrorStr.c_str();
}
