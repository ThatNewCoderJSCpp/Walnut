#ifndef WALNUT_FILE_KIND_HPP
#define WALNUT_FILE_KIND_HPP

#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>

namespace walnut {

enum class FileKind {
    Walnut,
    WalnutHeader,
    CSource,
    CppSource,
    CHeader,
    CppHeader,
    Object,
    Library,
    Other
};

inline std::string lower_extension(std::string_view path) {
    std::string ext = std::filesystem::path(std::string(path)).extension().string();
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

inline FileKind classify_file(std::string_view path) {
    const std::string ext = lower_extension(path);
    if (ext == ".wal") return FileKind::Walnut;
    if (ext == ".walh") return FileKind::WalnutHeader;
    if (ext == ".c") return FileKind::CSource;
    if (ext == ".cc" || ext == ".cpp" || ext == ".cxx" || ext == ".c++") return FileKind::CppSource;
    if (ext == ".h") return FileKind::CHeader;
    if (ext == ".hh" || ext == ".hpp" || ext == ".hxx" || ext == ".h++") return FileKind::CppHeader;
    if (ext == ".o" || ext == ".obj") return FileKind::Object;
    if (ext == ".a" || ext == ".so" || ext == ".dylib" || ext == ".lib" || ext == ".dll") return FileKind::Library;
    return FileKind::Other;
}

inline bool is_walnut_file(FileKind k) { return k == FileKind::Walnut || k == FileKind::WalnutHeader; }
inline bool is_native_header(FileKind k) { return k == FileKind::CHeader || k == FileKind::CppHeader; }
inline bool is_native_code(FileKind k) { return k == FileKind::CSource || k == FileKind::CppSource || k == FileKind::Object || k == FileKind::Library; }

inline std::string file_display_name(std::string_view path) {
    const std::string name = std::filesystem::path(std::string(path)).filename().string();
    return name.empty() ? std::string(path) : name;
}

inline std::string not_walnut_reason(std::string_view path, std::string_view what) {
    const FileKind k = classify_file(path);
    const std::string name = file_display_name(path);
    std::string msg = "'" + name + "' isn't a Walnut file; " + std::string(what) + " only takes .wal and .walh files";
    if (is_native_header(k) || is_native_code(k)) {
        const char* lang = (k == FileKind::CSource || k == FileKind::CHeader) ? "C" : "C++";
        msg += "; to use C or C++ code, declare the functions you need in an extern block, as in 'extern \"" + std::string(lang) + "\" from \"" + name + "\" { ... } end extern'";
    }
    return msg;
}

} // namespace walnut

#endif // WALNUT_FILE_KIND_HPP
