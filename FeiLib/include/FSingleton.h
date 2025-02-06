#ifndef SINGLETON_H_
#define SINGLETON_H_
#pragma once
#include "FDef.h"
#include "cassert"

namespace Fei {

//Why use F_API here?
// --> indicate the inline static member sInstance is come from another dll/exported out of this dll  
// --> or you might get multi instance.   
// --> BTW It's a Win32 problem only -- because C++ Standard doesnt make rule about dylink, so its impl defined.
// --> And for client thee shall not use this class as base for who will get a link error as punishment
// --> Magic OwO 
template <typename T> class F_API FSingleton {
public:
  FSingleton() {
    assert(sInstance == 0 && "Double new singleton.");
    sInstance = (T *)this;
  }

  static T *instance() {
    assert(sInstance != 0 && "No exsit instance.");
    return sInstance;
  }

  static bool valid() {
      return sInstance != nullptr;
  }

  ~FSingleton() { sInstance = 0; }

private:
  inline static T *sInstance = 0;
};
} // namespace Fei
#endif // !SINGLETON_H_
