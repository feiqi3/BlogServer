#pragma once
#include <fstream>
#include <string>
#include "FException.h"
#include "FLogger.h"
namespace Blog {
    // Mode options: read-only, write-only, read+write.
    enum class Mode {
        ReadOnly,
        WriteOnly,
        ReadWrite
    };

    // Failure semantics (since 2026-09-22): the constructor never throws.
    // On open/map failure the object stays in a well-defined "not open" state:
    // data() == nullptr and size() == 0, with a warn-level log entry.
    // Callers MUST check data() (or isOpen()) before using the mapping.
    class MemoryMappedFile {
    public:
        //size = 0: get file size auto
        MemoryMappedFile(const std::string& filename, Mode mode, size_t size)
            : filename_(filename), mode_(mode), size_(size), data_(nullptr)
        {
            open();
        }

        ~MemoryMappedFile() {
            close();
        }

        void* data()const { return data_; }
        size_t size()const { return size_; }
        bool isOpen()const { return data_ != nullptr; }
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