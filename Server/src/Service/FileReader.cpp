#include <string>
#include <cstring>
#include "Utils/BlogRuntimeError.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#endif
#include <fstream>
#include "FileReader.h"

using namespace Blog;


namespace Blog{


        void MemoryMappedFile::open() {
#ifdef _WIN32
            if (mode_ == Mode::ReadOnly) {
                // 只读模式：仅打开读权限，要求文件已存在
                fileHandle_ = CreateFileA(
                    filename_.c_str(),
                    GENERIC_READ,
                    FILE_SHARE_READ,
                    nullptr,
                    OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL,
                    nullptr
                );
                if (fileHandle_ == INVALID_HANDLE_VALUE) {
                    throw RuntimeError("Failed to open file (ReadOnly)");
                }

                mapHandle_ = CreateFileMapping(
                    fileHandle_,
                    nullptr,
                    PAGE_READONLY,
                    0,
                    static_cast<DWORD>(size_),
                    nullptr
                );
                if (!mapHandle_) {
                    CloseHandle(fileHandle_);
                    throw RuntimeError("Failed to create file mapping (ReadOnly)");
                }

                data_ = MapViewOfFile(mapHandle_, FILE_MAP_READ, 0, 0, size_);
                if (!data_) {
                    CloseHandle(mapHandle_);
                    CloseHandle(fileHandle_);
                    throw RuntimeError("Failed to map view of file (ReadOnly)");
                }
            }
            else if (mode_ == Mode::WriteOnly) {
                // 写模式：只写映射，Windows下不能真正只写，所以仍使用 PAGE_READWRITE，但只申请写权限的映射视图
                fileHandle_ = CreateFileA(
                    filename_.c_str(),
                    GENERIC_WRITE,
                    0,  // 不共享写
                    nullptr,
                    OPEN_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL,
                    nullptr
                );
                if (fileHandle_ == INVALID_HANDLE_VALUE) {
                    throw RuntimeError("Failed to open file (WriteOnly)");
                }

                mapHandle_ = CreateFileMapping(
                    fileHandle_,
                    nullptr,
                    PAGE_READWRITE,
                    0,
                    static_cast<DWORD>(size_),
                    nullptr
                );
                if (!mapHandle_) {
                    CloseHandle(fileHandle_);
                    throw RuntimeError("Failed to create file mapping (WriteOnly)");
                }

                // 映射时仅要求写权限
                data_ = MapViewOfFile(mapHandle_, FILE_MAP_WRITE, 0, 0, size_);
                if (!data_) {
                    CloseHandle(mapHandle_);
                    CloseHandle(fileHandle_);
                    throw RuntimeError("Failed to map view of file (WriteOnly)");
                }
            }
            else if (mode_ == Mode::ReadWrite) {
                // 可读可写模式：打开时同时要读和写
                fileHandle_ = CreateFileA(
                    filename_.c_str(),
                    GENERIC_READ | GENERIC_WRITE,
                    0,
                    nullptr,
                    OPEN_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL,
                    nullptr
                );
                if (fileHandle_ == INVALID_HANDLE_VALUE) {
                    throw RuntimeError("Failed to open file (ReadWrite)");
                }

                mapHandle_ = CreateFileMapping(
                    fileHandle_,
                    nullptr,
                    PAGE_READWRITE,
                    0,
                    static_cast<DWORD>(size_),
                    nullptr
                );
                if (!mapHandle_) {
                    CloseHandle(fileHandle_);
                    throw RuntimeError("Failed to create file mapping (ReadWrite)");
                }

                // 映射时允许同时读写
                data_ = MapViewOfFile(mapHandle_, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, size_);
                if (!data_) {
                    CloseHandle(mapHandle_);
                    CloseHandle(fileHandle_);
                    throw RuntimeError("Failed to map view of file (ReadWrite)");
                }
            }
#else
            if (mode_ == Mode::ReadOnly) {
                fileDescriptor_ = ::open(filename_.c_str(), O_RDONLY);
                if (fileDescriptor_ == -1) {
                    throw RuntimeError("Failed to open file (ReadOnly)");
                }

                data_ = mmap(nullptr, size_, PROT_READ, MAP_SHARED, fileDescriptor_, 0);
                if (data_ == MAP_FAILED) {
                    ::close(fileDescriptor_);
                    throw RuntimeError("Failed to mmap file (ReadOnly)");
                }
            }
            else if (mode_ == Mode::WriteOnly) {
                // 写模式：以读写方式打开文件，因为 mmap 写映射要求读写文件描述符
                fileDescriptor_ = ::open(filename_.c_str(), O_RDWR | O_CREAT, 0666);
                if (fileDescriptor_ == -1) {
                    throw RuntimeError("Failed to open file (WriteOnly)");
                }
                // 调整文件大小（确保文件至少有 size_ 字节）
                if (lseek(fileDescriptor_, size_ - 1, SEEK_SET) == -1 ||
                    write(fileDescriptor_, "", 1) != 1) {
                    ::close(fileDescriptor_);
                    throw RuntimeError("Failed to resize file (WriteOnly)");
                }

                // 仅写映射：只允许写操作，读操作可能导致错误
                data_ = mmap(nullptr, size_, PROT_WRITE, MAP_SHARED, fileDescriptor_, 0);
                if (data_ == MAP_FAILED) {
                    ::close(fileDescriptor_);
                    throw RuntimeError("Failed to mmap file (WriteOnly)");
                }
            }
            else if (mode_ == Mode::ReadWrite) {
                fileDescriptor_ = ::open(filename_.c_str(), O_RDWR | O_CREAT, 0666);
                if (fileDescriptor_ == -1) {
                    throw RuntimeError("Failed to open file (ReadWrite)");
                }
                // 调整文件大小（确保文件至少有 size_ 字节）
                if (lseek(fileDescriptor_, size_ - 1, SEEK_SET) == -1 ||
                    write(fileDescriptor_, "", 1) != 1) {
                    ::close(fileDescriptor_);
                    throw RuntimeError("Failed to resize file (ReadWrite)");
                }

                // 读写映射
                data_ = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fileDescriptor_, 0);
                if (data_ == MAP_FAILED) {
                    ::close(fileDescriptor_);
                    throw RuntimeError("Failed to mmap file (ReadWrite)");
                }
            }
#endif
        }

        void MemoryMappedFile::close() {
#ifdef _WIN32
            if (data_) {
                UnmapViewOfFile(data_);
                data_ = nullptr;
            }
            if (mapHandle_) {
                CloseHandle(mapHandle_);
                mapHandle_ = nullptr;
            }
            if (fileHandle_) {
                CloseHandle(fileHandle_);
                fileHandle_ = nullptr;
            }
#else
            if (data_) {
                munmap(data_, size_);
                data_ = nullptr;
            }
            if (fileDescriptor_ != -1) {
                ::close(fileDescriptor_);
                fileDescriptor_ = -1;
            }
#endif
        }
}
