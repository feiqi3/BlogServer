#pragma once
#include "Async/FAsyncTypes.h"
#include <functional>
#include <string>
namespace Fei::Script {
struct InvocationResult {
    std::string json="null";
    Async::Error error;
    bool ok() const { return error.code==Async::ErrorCode::None; }
};
using InvocationCallback=std::function<void(InvocationResult)>;
}
