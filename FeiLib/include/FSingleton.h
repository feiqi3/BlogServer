#ifndef SINGLETON_H_
#define SINGLETON_H_
#pragma once
#include "FDef.h"
#include "cassert"

namespace Fei {

//Why use F_API here?
// --> indicate the inline static member sInstance is come from another dll/exported out of this dll  
// --> so there is only one single instance memory exists in multi-dlls   
// --> BTW It's a Win32 problem only
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
