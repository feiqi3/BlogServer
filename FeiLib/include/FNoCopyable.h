#ifndef  FNOCOPYABLE_H
#define  FNOCOPYABLE_H

namespace Fei{
class FNoCopyable {
public:
    FNoCopyable() = default;
    ~FNoCopyable() = default;

    FNoCopyable(const FNoCopyable &) = delete;
    FNoCopyable &operator=(const FNoCopyable &) = delete;
};
}
#endif