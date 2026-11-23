#pragma once
#include "Http/FHttpResponder.h"
#include <chrono>
#include <functional>
namespace Fei::Http {
struct DeferredResponse {
    std::function<void(FHttpResponder)> start;
    std::chrono::milliseconds timeout;
};
}
