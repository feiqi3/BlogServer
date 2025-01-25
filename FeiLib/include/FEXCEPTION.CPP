#include "FException.h"
#include <sstream>
#include "absl/debugging/stacktrace.h"

#define FRAME_STACK_MAX_BACK_TRACE 64
namespace Fei {
    FException::FException()
    {
        void* result[FRAME_STACK_MAX_BACK_TRACE];
        int sizes[FRAME_STACK_MAX_BACK_TRACE];
        int depth = absl::GetStackFrames(result, sizes, FRAME_STACK_MAX_BACK_TRACE,1);

        std::stringstream ss;
        ss << "Call Stack:\n";
        for (auto i = 0; i < depth; ++i) {
            std::string temp = std::string((char*)result[i], sizes[i]);
            ss << temp << "\n";
        }
        this->mStackTace = ss.str();
    }
    const char* FException::what() const
    {
        return mStackTace.c_str();
    }
}