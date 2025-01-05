#ifndef  FNOCOPYABLE_H
#define  FNOCOPYABLE_H
#include "FDef.h"
namespace Fei{
class F_API FNoCopyable {
public:
    FNoCopyable() = default;
    ~FNoCopyable() = default;

    FNoCopyable(const FNoCopyable &) = delete;
    FNoCopyable &operator=(const FNoCopyable &) = delete;
};
}
#endif