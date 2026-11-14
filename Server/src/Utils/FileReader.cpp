#include <string>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#endif
#include "FileReader.h"

using namespace Blog;


namespace Blog{

    namespace {
        // Single failure exit: log a warning and keep the "not open" state
        // (data_ == nullptr, size_ == 0).
        void logOpenFailure(const std::string &filename, const char *reason) {
            Fei::Logger::instance()->log(
                Fei::lvl::warn, "MemoryMappedFile open failed ({}): {}", filename, reason);
        }
    }

        void MemoryMappedFile::open() {
            data_ = nullptr;
#ifdef _WIN32
            if (mode_ == Mode::ReadOnly) {
                // Read-only: open for read only; the file must already exist.
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
                    fileHandle_ = nullptr;
                    logOpenFailure(filename_, "CreateFile failed (ReadOnly)");
                    return;
                }

                if (size_ == 0) {
                    LARGE_INTEGER fileSize;
                    if (!GetFileSizeEx(fileHandle_, &fileSize) || fileSize.QuadPart <= 0) {
                        CloseHandle(fileHandle_);
                        fileHandle_ = nullptr;
                        logOpenFailure(filename_, "query size failed or empty (ReadOnly)");
                        return;
                    }
                    size_ = static_cast<size_t>(fileSize.QuadPart);
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
                    fileHandle_ = nullptr;
                    logOpenFailure(filename_, "CreateFileMapping failed (ReadOnly)");
                    return;
                }

                data_ = MapViewOfFile(mapHandle_, FILE_MAP_READ, 0, 0, size_);
                if (!data_) {
                    CloseHandle(mapHandle_);
                    mapHandle_ = nullptr;
                    CloseHandle(fileHandle_);
                    fileHandle_ = nullptr;
                    logOpenFailure(filename_, "MapViewOfFile failed (ReadOnly)");
                }
            }
            else if (mode_ == Mode::WriteOnly) {
                // Write-only mapping: Windows cannot map write-only, so use
                // PAGE_READWRITE and request a write-only view.
                fileHandle_ = CreateFileA(
                    filename_.c_str(),
                    GENERIC_WRITE,
                    0,  // no write sharing
                    nullptr,
                    OPEN_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL,
                    nullptr
                );
                if (fileHandle_ == INVALID_HANDLE_VALUE) {
                    fileHandle_ = nullptr;
                    logOpenFailure(filename_, "CreateFile failed (WriteOnly)");
                    return;
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
                    fileHandle_ = nullptr;
                    logOpenFailure(filename_, "CreateFileMapping failed (WriteOnly)");
                    return;
                }

                // Request write access only for the view.
                data_ = MapViewOfFile(mapHandle_, FILE_MAP_WRITE, 0, 0, size_);
                if (!data_) {
                    CloseHandle(mapHandle_);
                    mapHandle_ = nullptr;
                    CloseHandle(fileHandle_);
                    fileHandle_ = nullptr;
                    logOpenFailure(filename_, "MapViewOfFile failed (WriteOnly)");
                }
            }
            else if (mode_ == Mode::ReadWrite) {
                // Read+write: request both access rights on open.
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
                    fileHandle_ = nullptr;
                    logOpenFailure(filename_, "CreateFile failed (ReadWrite)");
                    return;
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
                    fileHandle_ = nullptr;
                    logOpenFailure(filename_, "CreateFileMapping failed (ReadWrite)");
                    return;
                }

                // Allow read and write on the view.
                data_ = MapViewOfFile(mapHandle_, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, size_);
                if (!data_) {
                    CloseHandle(mapHandle_);
                    mapHandle_ = nullptr;
                    CloseHandle(fileHandle_);
                    fileHandle_ = nullptr;
                    logOpenFailure(filename_, "MapViewOfFile failed (ReadWrite)");
                }
            }
#else
            if (mode_ == Mode::ReadOnly) {
                fileDescriptor_ = ::open(filename_.c_str(), O_RDONLY);
                if (fileDescriptor_ == -1) {
                    logOpenFailure(filename_, "open failed (ReadOnly)");
                    return;
                }

                if (size_ == 0) {
                    struct stat st;
                    if (::fstat(fileDescriptor_, &st) != 0) {
                        ::close(fileDescriptor_);
                        fileDescriptor_ = -1;
                        logOpenFailure(filename_, "fstat failed (ReadOnly)");
                        return;
                    }
                    size_ = static_cast<size_t>(st.st_size);
                }
                if (size_ == 0) {
                    // mmap with length 0 is undefined; treat empty files as failure.
                    ::close(fileDescriptor_);
                    fileDescriptor_ = -1;
                    logOpenFailure(filename_, "empty file (ReadOnly)");
                    return;
                }

                void *mapped = ::mmap(nullptr, size_, PROT_READ, MAP_SHARED, fileDescriptor_, 0);
                if (mapped == MAP_FAILED) {
                    ::close(fileDescriptor_);
                    fileDescriptor_ = -1;
                    logOpenFailure(filename_, "mmap failed (ReadOnly)");
                    return;
                }
                data_ = mapped;
            }
            else if (mode_ == Mode::WriteOnly) {
                // Write mode: open read+write because mmap write mappings need it.
                fileDescriptor_ = ::open(filename_.c_str(), O_RDWR | O_CREAT, 0666);
                if (fileDescriptor_ == -1) {
                    logOpenFailure(filename_, "open failed (WriteOnly)");
                    return;
                }
                // Grow the file so it holds at least size_ bytes.
                if (lseek(fileDescriptor_, size_ - 1, SEEK_SET) == -1 ||
                    write(fileDescriptor_, "", 1) != 1) {
                    ::close(fileDescriptor_);
                    fileDescriptor_ = -1;
                    logOpenFailure(filename_, "resize failed (WriteOnly)");
                    return;
                }

                void *mapped = ::mmap(nullptr, size_, PROT_WRITE, MAP_SHARED, fileDescriptor_, 0);
                if (mapped == MAP_FAILED) {
                    ::close(fileDescriptor_);
                    fileDescriptor_ = -1;
                    logOpenFailure(filename_, "mmap failed (WriteOnly)");
                    return;
                }
                data_ = mapped;
            }
            else if (mode_ == Mode::ReadWrite) {
                fileDescriptor_ = ::open(filename_.c_str(), O_RDWR | O_CREAT, 0666);
                if (fileDescriptor_ == -1) {
                    logOpenFailure(filename_, "open failed (ReadWrite)");
                    return;
                }
                // Grow the file so it holds at least size_ bytes.
                if (lseek(fileDescriptor_, size_ - 1, SEEK_SET) == -1 ||
                    write(fileDescriptor_, "", 1) != 1) {
                    ::close(fileDescriptor_);
                    fileDescriptor_ = -1;
                    logOpenFailure(filename_, "resize failed (ReadWrite)");
                    return;
                }

                void *mapped = ::mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fileDescriptor_, 0);
                if (mapped == MAP_FAILED) {
                    ::close(fileDescriptor_);
                    fileDescriptor_ = -1;
                    logOpenFailure(filename_, "mmap failed (ReadWrite)");
                    return;
                }
                data_ = mapped;
            }
#endif
            // Normalize the failure state: data_ == nullptr implies size_ == 0.
            if (!data_) {
                size_ = 0;
            }
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