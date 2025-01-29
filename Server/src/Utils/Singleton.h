#ifndef BLOG_SINGLETON_H_
#define BLOG_SINGLETON_H_
#pragma once
#include "cassert"

namespace Blog {
    template <typename T> class Singleton {
    public:
        Singleton() {
            assert(sInstance == 0 && "Double new singleton.");
            sInstance = (T*)this;
        }

        static T* instance() {
            assert(sInstance != 0 && "No exsit instance.");
            return sInstance;
        }

        static bool valid() {
            return sInstance != nullptr;
        }

        ~Singleton() { sInstance = 0; }

    private:
        inline static T* sInstance = 0;
    };
}
#endif // !SINGLETON_H_
