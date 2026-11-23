#include "ModuleBundle.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
namespace Fei::Script {
ModuleBundle readModuleBundle(const std::string& entryFile) {
    namespace fs = std::filesystem;
    fs::path input = fs::absolute(entryFile);
    if (fs::is_symlink(fs::symlink_status(input))) throw std::runtime_error("Module entry must not be a symlink");
    auto root = fs::canonical(input.parent_path());
    ModuleBundle bundle;
    bundle.entry = input.filename().generic_string();
    std::size_t visited = 0, total = 0;
    for (auto it = fs::recursive_directory_iterator(root); it != fs::recursive_directory_iterator(); ++it) {
        if (++visited > 256 || it.depth() > 4) throw std::runtime_error("Module directory is too large");
        if (it->is_symlink()) { it.disable_recursion_pending(); continue; }
        if (!it->is_regular_file()) continue;
        auto extension = it->path().extension();
        if (extension != ".js" && extension != ".mjs") continue;
        auto size = it->file_size();
        if (size > 128 * 1024 || total + size > 512 * 1024 || bundle.sources.size() >= 32) throw std::runtime_error("Module source limit exceeded");
        std::ifstream file(it->path(), std::ios::binary);
        if (!file) throw std::runtime_error("Cannot read module");
        std::string source{std::istreambuf_iterator<char>(file), {}};
        if (source.size() != size) throw std::runtime_error("Module changed while reading");
        total += source.size();
        bundle.sources.emplace(it->path().lexically_relative(root).generic_string(), std::move(source));
    }
    if (!bundle.sources.contains(bundle.entry)) throw std::runtime_error("Entry is missing from module snapshot");
    return bundle;
}
}
