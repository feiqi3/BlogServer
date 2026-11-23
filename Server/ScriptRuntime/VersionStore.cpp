#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#include "VersionStore.h"
#include <openssl/evp.h>
#include <atomic>
#include <chrono>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <algorithm>

namespace Blog::Script {
namespace {
namespace fs = std::filesystem;
void validId(const std::string& value) {
    if (value.size() != 64 || value.find_first_not_of("0123456789abcdef") != std::string::npos) throw std::invalid_argument("Invalid script version");
}
void validate(const ModuleBundle& bundle) {
    if (bundle.sources.empty() || bundle.sources.size() > 32 || !bundle.sources.contains(bundle.entry) || bundle.entry.find('/') != std::string::npos)
        throw std::invalid_argument("Invalid module entry");
    std::size_t total = 0;
    for (const auto& [name, source] : bundle.sources) {
        if (name.size() > 160 || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/_-.") != std::string::npos) throw std::invalid_argument("Unsupported module filename");
        if (name.empty() || name.front() == '/' || name.find('\\') != std::string::npos || name.find(':') != std::string::npos || name.find('\0') != std::string::npos || fs::path(name).lexically_normal().generic_string() != name)
            throw std::invalid_argument("Invalid module path");
        for (const auto& part : fs::path(name)) if (part == ".." || part == ".") throw std::invalid_argument("Module path escapes bundle");
        if (fs::path(name).extension() != ".js" && fs::path(name).extension() != ".mjs") throw std::invalid_argument("Unsupported module type");
        total += source.size();
        if (source.size() > 128 * 1024 || total > 512 * 1024) throw std::invalid_argument("Module size exceeded");
    }
}
std::string digest(const ModuleBundle& bundle) {
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!ctx || EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1) throw std::runtime_error("Cannot initialize version digest");
    auto add = [&](const std::string& value) {
        const auto length = std::to_string(value.size()) + ":";
        if (EVP_DigestUpdate(ctx.get(), length.data(), length.size()) != 1 || EVP_DigestUpdate(ctx.get(), value.data(), value.size()) != 1) throw std::runtime_error("Cannot hash script version");
    };
    add(bundle.entry);
    for (const auto& [name, source] : bundle.sources) { add(name); add(source); }
    unsigned char bytes[EVP_MAX_MD_SIZE]; unsigned length = 0;
    if (EVP_DigestFinal_ex(ctx.get(), bytes, &length) != 1) throw std::runtime_error("Cannot finish version digest");
    constexpr char hex[] = "0123456789abcdef";
    std::string value;
    for (unsigned i = 0; i < length; ++i) { value += hex[bytes[i] >> 4]; value += hex[bytes[i] & 15]; }
    return value;
}
std::string nonce() {
    static std::atomic<unsigned> count{0};
    return std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(++count);
}
void write(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), text.size()); out.close();
    if (!out) throw std::runtime_error("Cannot persist script version");
}
void replace(const fs::path& from, const fs::path& to) {
#ifdef _WIN32
    if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("Cannot activate script version");
#else
    fs::rename(from, to);
#endif
}
struct PendingRecord {
    fs::path temporary, destination;
    bool committed = false;
    ~PendingRecord() { if (!committed) { std::error_code error; fs::remove(temporary, error); } }
    void commit() { if (!committed) { replace(temporary, destination); committed = true; } }
};
}
VersionStore::VersionStore(std::filesystem::path root) {
    std::filesystem::create_directories(root);
    root_ = std::filesystem::canonical(root);
}
std::string VersionStore::save(const ModuleBundle& bundle) {
    validate(bundle);
    auto version = digest(bundle);
    auto destination = root_ / version;
    if (fs::exists(destination)) { load(version); return version; }
    if (versions().size() >= 100) throw std::runtime_error("Script history limit reached (100 versions)");
    auto stage = root_ / (".staging-" + nonce());
    fs::create_directory(stage);
    try {
        fs::create_directory(stage / "code");
        for (const auto& [name, source] : bundle.sources) {
            auto path = stage / "code" / name;
            fs::create_directories(path.parent_path()); write(path, source);
        }
        write(stage / "entry.txt", bundle.entry + "\n");
        fs::rename(stage, destination);
    } catch (...) { fs::remove_all(stage); throw; }
    return version;
}
std::filesystem::path VersionStore::entryPath(const std::string& version) const {
    validId(version);
    auto directory = root_ / version;
    if (fs::is_symlink(fs::symlink_status(directory)) || fs::is_symlink(fs::symlink_status(directory / "code")) || fs::is_symlink(fs::symlink_status(directory / "entry.txt"))) throw std::runtime_error("Invalid version directory");
    std::ifstream input(directory / "entry.txt"); std::string entry;
    if (!input || fs::file_size(directory / "entry.txt") > 256 || !std::getline(input, entry) || entry.empty() || entry.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.") != std::string::npos || (fs::path(entry).extension() != ".js" && fs::path(entry).extension() != ".mjs")) throw std::runtime_error("Invalid stored entry");
    return directory / "code" / entry;
}
ModuleBundle VersionStore::load(const std::string& version) const {
    auto bundle = readModuleBundle(entryPath(version).string());
    validate(bundle);
    if (digest(bundle) != version) throw std::runtime_error("Stored script version failed integrity check");
    return bundle;
}
ActiveVersion VersionStore::active() const {
    ActiveVersion result;
    if (!fs::exists(root_ / "active")) return result;
    std::ifstream input(root_ / "active");
    if (fs::is_symlink(fs::symlink_status(root_ / "active")) || fs::file_size(root_ / "active") > 132 || !std::getline(input, result.current) || !std::getline(input, result.previous)) throw std::runtime_error("Invalid active version record");
    validId(result.current); if (!result.previous.empty()) validId(result.previous);
    return result;
}
std::vector<std::string> VersionStore::versions() const {
    std::vector<std::pair<fs::file_time_type,std::string>> entries;
    for (const auto& entry:fs::directory_iterator(root_)) {
        auto name=entry.path().filename().string();
        if(name.size()!=64||name.find_first_not_of("0123456789abcdef")!=std::string::npos||entry.is_symlink()||!entry.is_directory())continue;
        entries.emplace_back(entry.last_write_time(),std::move(name));
    }
    std::sort(entries.rbegin(),entries.rend());
    std::vector<std::string> result;
    for(auto& entry:entries)result.push_back(std::move(entry.second));
    return result;
}
void VersionStore::activate(const std::string& version) {
    prepareActivation(version)();
}
std::function<void()> VersionStore::prepareActivation(const std::string& version) {
    load(version);
    auto old = active();
    if (old.current == version) return [] {};
    auto pending = std::make_shared<PendingRecord>();
    pending->temporary = root_ / (".active-" + nonce());
    pending->destination = root_ / "active";
    write(pending->temporary, version + "\n" + old.current + "\n");
    return [pending] { pending->commit(); };
}
}
