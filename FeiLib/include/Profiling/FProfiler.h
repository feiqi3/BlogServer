#pragma once
#include "FDef.h"
#include <atomic>
#include <chrono>
#include <exception>
#include <functional>
#include <string>
#include <vector>

#ifndef FEILIB_ENABLE_PROFILING
#define FEILIB_ENABLE_PROFILING 1
#endif

namespace Fei {
using ProfileId = std::uint32_t;

// Literal names have static storage. The owned strings are created on first use.
struct FProfileSite {
    template<std::size_t N>
    constexpr FProfileSite(const char (&value)[N], const char* source, unsigned sourceLine)
        : name(value), file(source), line(sourceLine) {}
    const char* const name;
    const char* const file;
    const unsigned line;
    std::atomic<ProfileId> cached{0};
};

struct FProfileLabel {
    std::string name, category, file, script, sourceVersion;
    unsigned line = 0;
    std::uint64_t generation = 0;
};
struct FProfileSample {
    ProfileId id = 0;
    std::uint32_t thread = 0; // Completion thread for an asynchronous span.
    std::uint64_t elapsedNs = 0, endNs = 0; // endNs is relative to this session.
    bool failed = false;
};
struct FProfileRow {
    ProfileId id = 0;
    FProfileLabel label;
    std::uint64_t calls = 0, errors = 0, totalNs = 0, maxNs = 0;
};
struct FProfileSnapshot {
    bool enabled = false;
    std::uint64_t session = 0, droppedSamples = 0, droppedThreads = 0, rejectedLabels = 0, failedSamples = 0;
    std::string sinkError;
    std::vector<FProfileRow> rows;
};
struct FProfileBatch {
    std::vector<FProfileSample> samples;
    FProfileSnapshot summary;
};
struct FProfilerOptions {
    std::chrono::milliseconds interval{100};
    std::size_t maxThreads = 128;
};

class F_API FProfiler {
public:
    using Sink = std::function<void(const FProfileBatch&)>;
    // One process-wide collector; start/flush/stop are control-thread operations.
    static bool start(FProfilerOptions = {}, Sink = {});
    static void stop();
    static void flush();
    static bool enabled() noexcept {
#if FEILIB_ENABLE_PROFILING
        return enabled_.load(std::memory_order_seq_cst);
#else
        return false;
#endif
    }
    static ProfileId registerLabel(FProfileLabel);
    static ProfileId siteId(FProfileSite&) noexcept;
    static void sourceVersion(const std::string& script, std::uint64_t generation, const std::string& version);
    static FProfileSnapshot snapshot();
    // Also accepts an existing timer's duration. No string work on this path.
    static void record(ProfileId, std::chrono::nanoseconds elapsed, bool failed = false) noexcept;
private:
    friend class FProfileSpan;
    static void recordSession(ProfileId, std::uint64_t session, std::uint64_t begin, bool failed) noexcept;
    static std::atomic_bool enabled_;
};

class F_API FProfileSpan {
public:
    FProfileSpan() noexcept = default;
    static FProfileSpan begin(ProfileId) noexcept;
    static FProfileSpan begin(FProfileSite&) noexcept;
    ~FProfileSpan() { if(id_)finish(); }
    FProfileSpan(const FProfileSpan&) = delete;
    FProfileSpan& operator=(const FProfileSpan&) = delete;
    FProfileSpan(FProfileSpan&&) noexcept;
    FProfileSpan& operator=(FProfileSpan&&) noexcept;
    void finish(bool failed = false) noexcept;
    explicit operator bool()const noexcept{return id_!=0;}
private:
    ProfileId id_ = 0;
    std::uint64_t session_ = 0, begin_ = 0;
};

class FProfileScope {
public:
    explicit FProfileScope(FProfileSite& site) noexcept {
        if(FProfiler::enabled()){span_=FProfileSpan::begin(site);exceptions_=std::uncaught_exceptions();}
    }
    explicit FProfileScope(ProfileId id) noexcept {
        if(FProfiler::enabled()){span_=FProfileSpan::begin(id);exceptions_=std::uncaught_exceptions();}
    }
    ~FProfileScope() { if(span_)span_.finish(std::uncaught_exceptions() > exceptions_); }
    FProfileScope(const FProfileScope&) = delete;
    FProfileScope& operator=(const FProfileScope&) = delete;
private:
    FProfileSpan span_;
    int exceptions_=0;
};
}

#define FEI_PROFILE_JOIN_IMPL(a,b) a##b
#define FEI_PROFILE_JOIN(a,b) FEI_PROFILE_JOIN_IMPL(a,b)
#if FEILIB_ENABLE_PROFILING
#define FEI_PROFILE_SCOPE_IMPL(label,n) \
    static ::Fei::FProfileSite FEI_PROFILE_JOIN(_feiProfileSite_,n){label,__FILE__,__LINE__}; \
    ::Fei::FProfileScope FEI_PROFILE_JOIN(_feiProfileScope_,n){FEI_PROFILE_JOIN(_feiProfileSite_,n)}
#define FeiScope(label) FEI_PROFILE_SCOPE_IMPL(label,__COUNTER__)
#define FeiScopeId(id) ::Fei::FProfileScope FEI_PROFILE_JOIN(_feiProfileScope_,__COUNTER__){id}
#else
#define FeiScope(label) ((void)0)
#define FeiScopeId(id) ((void)0)
#endif
