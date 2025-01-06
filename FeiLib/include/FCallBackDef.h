#pragma once

#include <functional>

namespace Fei {
typedef std::function<void()> EventCallback;
typedef std::function<void()> ReadEventCallback;
typedef std::function<void()> WriteEventCallback;
typedef std::function<void()> WriteCompleteCallback;
} // namespace Fei