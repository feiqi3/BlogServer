#pragma once
#include <fstream>
#include <string>
#include "FException.h"
#include "FLogger.h"
namespace Blog {
    // 模式选项：只读、只写、可读可写
    enum class Mode {
        ReadOnly,
        WriteOnly,
        ReadWrite
    };

    class MemoryMappedFile {
    public:
        //size = 0: get file size auto
        MemoryMappedFile(const std::string& filename, Mode mode, size_t size)
            : filename_(filename), mode_(mode), size_(size), data_(nullptr)
        {
            {
                if (size == 0 && mode == Mode::ReadOnly) {
                    std::fstream fs(filename);
                    fs.seekg(0, std::ios::end);
                    size_ = fs.tellg();
                }
            }
            open();
        }

        ~MemoryMappedFile() {
            close();
        }

        void* data()const { return data_; }
        size_t size()const { return size_; }
    private:
        void open();
        void close();
    private:
        std::string filename_;
        size_t size_;
        Mode mode_;
        void* data_;

#ifdef _WIN32
        void* fileHandle_ = nullptr;
        void* mapHandle_ = nullptr;
#else
        int fileDescriptor_ = -1;
#endif

    };
    using MemMapedFilePtr = std::shared_ptr<MemoryMappedFile>;
}