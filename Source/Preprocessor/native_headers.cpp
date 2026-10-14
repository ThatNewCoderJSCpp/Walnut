#include "Preprocessor/native_headers.hpp"
#include "Lexer/keywords.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <mutex>
#include <sstream>

#ifdef _WIN32
extern "C" __declspec(dllimport) void* __stdcall LoadLibraryA(const char* name);
extern "C" __declspec(dllimport) void* __stdcall GetProcAddress(void* module, const char* name);
#else
#include <dlfcn.h>
#endif

namespace walnut {
namespace native {

namespace {

struct CXString { const void* data; unsigned private_flags; };
struct CXCursor { int kind; int xdata; const void* data[3]; };
struct CXType { int kind; void* data[2]; };
struct CXSourceLocation { const void* ptr_data[2]; unsigned int_data; };
using CXIndex = void*;
using CXTranslationUnit = void*;
using CXDiagnostic = void*;
using CXVisitor = int (*)(CXCursor, CXCursor, void*);

enum : int {
    CursorUnexposedDecl = 1,
    CursorFunctionDecl = 8,
    CursorNamespace = 22,
    CursorLinkageSpec = 23,
    CursorFunctionTemplate = 30,
    TypeVoid = 2, TypeBool = 3, TypeCharU = 4, TypeUChar = 5, TypeChar16 = 6, TypeChar32 = 7,
    TypeUShort = 8, TypeUInt = 9, TypeULong = 10, TypeULongLong = 11,
    TypeCharS = 13, TypeSChar = 14, TypeWChar = 15, TypeShort = 16, TypeInt = 17, TypeLong = 18, TypeLongLong = 19,
    TypeFloat = 21, TypeDouble = 22,
    TypePointer = 101, TypeLValueReference = 103, TypeRecord = 105,
    VisitBreak = 0, VisitContinue = 1, VisitRecurse = 2
};

struct Api {
    bool        ok = false;
    std::string where;
    CXIndex (*createIndex)(int, int) = nullptr;
    void (*disposeIndex)(CXIndex) = nullptr;
    CXTranslationUnit (*parseTranslationUnit)(CXIndex, const char*, const char* const*, int, void*, unsigned, unsigned) = nullptr;
    void (*disposeTranslationUnit)(CXTranslationUnit) = nullptr;
    CXCursor (*getTranslationUnitCursor)(CXTranslationUnit) = nullptr;
    unsigned (*visitChildren)(CXCursor, CXVisitor, void*) = nullptr;
    int (*getCursorKind)(CXCursor) = nullptr;
    CXString (*getCursorSpelling)(CXCursor) = nullptr;
    const char* (*getCString)(CXString) = nullptr;
    void (*disposeString)(CXString) = nullptr;
    CXType (*getCursorType)(CXCursor) = nullptr;
    CXType (*getCursorResultType)(CXCursor) = nullptr;
    int (*getNumArguments)(CXCursor) = nullptr;
    CXCursor (*getArgument)(CXCursor, unsigned) = nullptr;
    CXType (*getCanonicalType)(CXType) = nullptr;
    CXType (*getPointeeType)(CXType) = nullptr;
    CXString (*getTypeSpelling)(CXType) = nullptr;
    unsigned (*isConstQualifiedType)(CXType) = nullptr;
    long long (*typeSizeOf)(CXType) = nullptr;
    CXSourceLocation (*getCursorLocation)(CXCursor) = nullptr;
    int (*isFromMainFile)(CXSourceLocation) = nullptr;
    unsigned (*isFunctionTypeVariadic)(CXType) = nullptr;
    int (*exceptionSpecification)(CXCursor) = nullptr;
    int (*getCursorAvailability)(CXCursor) = nullptr;
    unsigned (*getNumDiagnostics)(CXTranslationUnit) = nullptr;
    CXDiagnostic (*getDiagnostic)(CXTranslationUnit, unsigned) = nullptr;
    int (*getDiagnosticSeverity)(CXDiagnostic) = nullptr;
    CXString (*formatDiagnostic)(CXDiagnostic, unsigned) = nullptr;
    void (*disposeDiagnostic)(CXDiagnostic) = nullptr;
};

std::mutex& skip_mutex() {
    static std::mutex m;
    return m;
}

std::map<std::string, std::string>& skips() {
    static std::map<std::string, std::string> m;
    return m;
}

void* open_library(const std::string& path) {
#ifdef _WIN32
    return LoadLibraryA(path.c_str());
#else
    return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

void* symbol(void* lib, const char* name) {
#ifdef _WIN32
    return GetProcAddress(lib, name);
#else
    return dlsym(lib, name);
#endif
}

std::vector<std::string> search_list() {
    std::vector<std::string> list;
    if (const char* env = std::getenv("WALNUT_LIBCLANG")) if (*env) list.insert(list.begin(), env);
#ifdef _WIN32
    list.push_back("libclang.dll");
    list.push_back("C:\\Program Files\\LLVM\\bin\\libclang.dll");
#elif defined(__APPLE__)
    list.push_back("libclang.dylib");
    list.push_back("/Library/Developer/CommandLineTools/usr/lib/libclang.dylib");
    list.push_back("/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/lib/libclang.dylib");
#else
    list.push_back("libclang.so");
    list.push_back("libclang.so.1");
    for (int v = 22; v >= 10; --v) {
        list.push_back("libclang-" + std::to_string(v) + ".so");
        list.push_back("libclang-" + std::to_string(v) + ".so.1");
        list.push_back("/usr/lib/llvm-" + std::to_string(v) + "/lib/libclang.so.1");
    }
#endif
    return list;
}

template <class F>
bool bind(void* lib, F& fn, const char* name) {
    fn = reinterpret_cast<F>(symbol(lib, name));
    return fn != nullptr;
}

Api load_api() {
    Api a;

    for (const std::string& path : search_list()) {
        void* lib = open_library(path);
        if (!lib) continue;
        bool ok = true;
        ok = bind(lib, a.createIndex, "clang_createIndex") && ok;
        ok = bind(lib, a.disposeIndex, "clang_disposeIndex") && ok;
        ok = bind(lib, a.parseTranslationUnit, "clang_parseTranslationUnit") && ok;
        ok = bind(lib, a.disposeTranslationUnit, "clang_disposeTranslationUnit") && ok;
        ok = bind(lib, a.getTranslationUnitCursor, "clang_getTranslationUnitCursor") && ok;
        ok = bind(lib, a.visitChildren, "clang_visitChildren") && ok;
        ok = bind(lib, a.getCursorKind, "clang_getCursorKind") && ok;
        ok = bind(lib, a.getCursorSpelling, "clang_getCursorSpelling") && ok;
        ok = bind(lib, a.getCString, "clang_getCString") && ok;
        ok = bind(lib, a.disposeString, "clang_disposeString") && ok;
        ok = bind(lib, a.getCursorType, "clang_getCursorType") && ok;
        ok = bind(lib, a.getCursorResultType, "clang_getCursorResultType") && ok;
        ok = bind(lib, a.getNumArguments, "clang_Cursor_getNumArguments") && ok;
        ok = bind(lib, a.getArgument, "clang_Cursor_getArgument") && ok;
        ok = bind(lib, a.getCanonicalType, "clang_getCanonicalType") && ok;
        ok = bind(lib, a.getPointeeType, "clang_getPointeeType") && ok;
        ok = bind(lib, a.getTypeSpelling, "clang_getTypeSpelling") && ok;
        ok = bind(lib, a.isConstQualifiedType, "clang_isConstQualifiedType") && ok;
        ok = bind(lib, a.typeSizeOf, "clang_Type_getSizeOf") && ok;
        ok = bind(lib, a.getCursorLocation, "clang_getCursorLocation") && ok;
        ok = bind(lib, a.isFromMainFile, "clang_Location_isFromMainFile") && ok;
        ok = bind(lib, a.isFunctionTypeVariadic, "clang_isFunctionTypeVariadic") && ok;
        ok = bind(lib, a.exceptionSpecification, "clang_getCursorExceptionSpecificationType") && ok;
        ok = bind(lib, a.getCursorAvailability, "clang_getCursorAvailability") && ok;
        ok = bind(lib, a.getNumDiagnostics, "clang_getNumDiagnostics") && ok;
        ok = bind(lib, a.getDiagnostic, "clang_getDiagnostic") && ok;
        ok = bind(lib, a.getDiagnosticSeverity, "clang_getDiagnosticSeverity") && ok;
        ok = bind(lib, a.formatDiagnostic, "clang_formatDiagnostic") && ok;
        ok = bind(lib, a.disposeDiagnostic, "clang_disposeDiagnostic") && ok;

        if (ok) {
            a.ok = true;
            a.where = path;
            return a;
        }
    }

    return a;
}

Api& api() {
    static Api a = load_api();
    return a;
}

std::string take(CXString s) {
    const char* c = api().getCString(s);
    std::string out = c ? c : "";
    api().disposeString(s);
    return out;
}

bool is_std_string(std::string spelling) {
    for (const char* prefix : { "const ", "class ", "struct " }) if (spelling.rfind(prefix, 0) == 0) spelling.erase(0, std::char_traits<char>::length(prefix));
    if (spelling.rfind("std::", 0) != 0) return false;
    if (spelling == "std::string" || spelling == "std::__1::string" || spelling == "std::__cxx11::string") return true;
    const std::size_t at = spelling.find("basic_string<char");
    if (at == std::string::npos) return false;
    const char next = at + 17 < spelling.size() ? spelling[at + 17] : '\0';
    return next == '>' || next == ',';
}

std::string int_name(long long size, bool is_unsigned) {
    std::string base;
    if (size == 1) base = "short short int";
    else if (size == 2) base = "short int";
    else if (size == 4) base = "int";
    else if (size == 8) base = "long int";
    else return {};
    return is_unsigned ? "unsigned " + base : base;
}

std::string map_type(CXType original, bool cpp, bool is_return, std::string& why) {
    Api& a = api();
    CXType t = a.getCanonicalType(original);
    const std::string written = take(a.getTypeSpelling(original));

    switch (t.kind) {
        case TypeVoid:
            if (is_return) return "void";
            why = "it has a 'void' parameter";
            return {};
        case TypeBool: return "bool";
        case TypeChar32: return "char";
        case TypeCharS: case TypeSChar: return int_name(a.typeSizeOf(t), false);
        case TypeCharU: case TypeUChar: return int_name(a.typeSizeOf(t), true);
        case TypeShort: case TypeInt: case TypeLong: case TypeLongLong: {
            const std::string n = int_name(a.typeSizeOf(t), false);
            if (n.empty()) why = "'" + written + "' has no Walnut equivalent";
            return n;
        }
        case TypeUShort: case TypeUInt: case TypeULong: case TypeULongLong: {
            const std::string n = int_name(a.typeSizeOf(t), true);
            if (n.empty()) why = "'" + written + "' has no Walnut equivalent";
            return n;
        }
        case TypeFloat: return "short short float";
        case TypeDouble: return "short float";
        case TypePointer: {
            CXType p = a.getPointeeType(t);
            const int pk = a.getCanonicalType(p).kind;
            if (!cpp && (pk == TypeCharS || pk == TypeCharU || pk == TypeSChar) && a.isConstQualifiedType(p)) return "string";
            why = "it uses the pointer type '" + written + "'";
            return {};
        }
        case TypeLValueReference: {
            CXType p = a.getPointeeType(t);
            const std::string ps = take(a.getTypeSpelling(a.getCanonicalType(p)));
            if (cpp && a.isConstQualifiedType(p) && is_std_string(ps.rfind("const ", 0) == 0 ? ps.substr(6) : ps)) return "const string&";
            why = "it uses the reference type '" + written + "'";
            return {};
        }
        case TypeRecord: {
            const std::string s = take(a.getTypeSpelling(t));
            if (cpp && is_std_string(s.rfind("const ", 0) == 0 ? s.substr(6) : s)) return "string";
            why = "it uses the type '" + written + "'";
            return {};
        }
        default:
            why = "it uses the type '" + written + "'";
            return {};
    }
}

struct Function {
    std::vector<std::string> ns;
    std::string              line;
};

struct Context {
    bool                                             cpp = false;
    std::vector<std::string>                         ns;
    bool                                             anonymous = false;
    std::vector<Function>                            functions;
    std::vector<std::pair<std::string, std::string>> skipped;
};

std::string qualified(const std::vector<std::string>& ns, const std::string& name) {
    std::string out;
    for (const std::string& n : ns) out += n + "::";
    return out + name;
}

void take_function(CXCursor c, Context& ctx) {
    Api& a = api();
    const std::string name = take(a.getCursorSpelling(c));
    const std::string full = qualified(ctx.ns, name);
    auto skip = [&](const std::string& why) { ctx.skipped.emplace_back(full, why); };

    if (ctx.anonymous) return;
    if (name.rfind("operator", 0) == 0) { skip("operators can't be brought in from a header"); return; }
    if (a.getCursorAvailability(c) == 2) return;
    if (is_keyword(name)) { skip("'" + name + "' is a Walnut keyword, so it can't be used as a name"); return; }
    for (const std::string& n : ctx.ns) if (is_keyword(n)) { skip("its namespace '" + n + "' is a Walnut keyword"); return; }
    if (a.isFunctionTypeVariadic(a.getCursorType(c))) { skip("it takes a variable number of arguments ('...')"); return; }

    std::string why;
    const std::string ret = map_type(a.getCursorResultType(c), ctx.cpp, true, why);
    if (ret.empty()) { skip(why.empty() ? "its return type can't cross into Walnut" : why); return; }
    const int n = a.getNumArguments(c);
    if (n < 0) { skip("its parameters are unknown"); return; }
    std::string params;

    for (int i = 0; i < n; ++i) {
        const std::string pt = map_type(a.getCursorType(a.getArgument(c, static_cast<unsigned>(i))), ctx.cpp, false, why);
        if (pt.empty()) { skip(why); return; }
        if (i) params += ", ";
        params += pt + ": p" + std::to_string(i);
    }

    const int spec = a.exceptionSpecification(c);
    const bool nothrow = !ctx.cpp || spec == 1 || spec == 4;
    const std::string line = "function " + name + "(" + params + ")" + (nothrow ? " noexcept" : "") + " -> " + ret + ";";

    for (const Function& f : ctx.functions) if (f.ns == ctx.ns && f.line == line) return;
    ctx.functions.push_back(Function{ ctx.ns, line });
}

int visit(CXCursor c, CXCursor, void* data) {
    Context& ctx = *static_cast<Context*>(data);
    Api& a = api();
    const int kind = a.getCursorKind(c);

    if (kind == CursorNamespace) {
        const std::string name = take(a.getCursorSpelling(c));
        const bool saved = ctx.anonymous;
        if (name.empty()) ctx.anonymous = true;
        ctx.ns.push_back(name);
        a.visitChildren(c, visit, data);
        ctx.ns.pop_back();
        ctx.anonymous = saved;
        return VisitContinue;
    }

    if (kind == CursorLinkageSpec || kind == CursorUnexposedDecl) {
        a.visitChildren(c, visit, data);
        return VisitContinue;
    }

    if (kind == CursorFunctionDecl) {
        if (a.isFromMainFile(a.getCursorLocation(c))) take_function(c, ctx);
        return VisitContinue;
    }

    if (kind == CursorFunctionTemplate && a.isFromMainFile(a.getCursorLocation(c)) && !ctx.anonymous) {
        const std::string name = take(a.getCursorSpelling(c));
        ctx.skipped.emplace_back(qualified(ctx.ns, name), "it is a template, so Walnut doesn't know which types to use; declare the versions you need in an extern \"C++\" block, as in 'function " + name + "(long int: v) -> long int;'");
    }

    return VisitContinue;
}

std::string walnut_string(const std::string& s) {
    std::string out = "\"";
    for (char c : s) { if (c == '\\' || c == '"') out += '\\'; out += c; }
    return out + "\"";
}

} // namespace

std::string libclang_location() {
    return api().ok ? api().where : std::string();
}

HeaderScan read_headers(const std::vector<std::string>& headers, const std::vector<std::string>& other_files, bool cpp, const std::vector<std::string>& clang_args) {
    HeaderScan out;
    Api& a = api();

    if (!a.ok) {
        out.errors.push_back("reading C and C++ headers needs libclang, and Walnut couldn't find it; install it (for example 'sudo apt install libclang-dev'), or point the WALNUT_LIBCLANG environment variable at the libclang library");
        return out;
    }

    std::vector<std::string> files = headers;
    files.insert(files.end(), other_files.begin(), other_files.end());
    std::string from;
    for (const std::string& f : files) from += (from.empty() ? "" : ", ") + walnut_string(f);
    std::vector<Function> all;

    for (const std::string& header : headers) {
        std::vector<std::string> args{ "-x", cpp ? "c++" : "c", cpp ? "-std=c++17" : "-std=c11" };
        args.push_back("-I" + std::filesystem::path(header).parent_path().string());
        args.insert(args.end(), clang_args.begin(), clang_args.end());
        std::vector<const char*> argv;
        for (const std::string& s : args) argv.push_back(s.c_str());
        CXIndex index = a.createIndex(0, 0);
        CXTranslationUnit tu = a.parseTranslationUnit(index, header.c_str(), argv.data(), static_cast<int>(argv.size()), nullptr, 0, 64);

        if (!tu) {
            out.errors.push_back("libclang could not read '" + header + "'");
            a.disposeIndex(index);
            continue;
        }

        bool failed = false;

        for (unsigned i = 0, n = a.getNumDiagnostics(tu); i < n; ++i) {
            CXDiagnostic d = a.getDiagnostic(tu, i);
            if (a.getDiagnosticSeverity(d) >= 3) {
                failed = true;
                if (out.errors.size() < 8) out.errors.push_back(take(a.formatDiagnostic(d, 1 | 2 | 4)));
            }
            a.disposeDiagnostic(d);
        }

        if (!failed) {
            Context ctx;
            ctx.cpp = cpp;
            a.visitChildren(a.getTranslationUnitCursor(tu), visit, &ctx);
            for (Function& f : ctx.functions) all.push_back(std::move(f));
            for (auto& s : ctx.skipped) out.skipped.push_back(std::move(s));
        }

        a.disposeTranslationUnit(tu);
        a.disposeIndex(index);
    }

    std::ostringstream text;
    std::vector<std::vector<std::string>> groups;
    for (const Function& f : all) if (std::find(groups.begin(), groups.end(), f.ns) == groups.end()) groups.push_back(f.ns);

    for (const std::vector<std::string>& ns : groups) {
        for (const std::string& n : ns) text << "namespace " << n << " {\n";
        text << "extern " << (cpp ? "\"C++\"" : "\"C\"") << " from " << from << " {\n";
        for (const Function& f : all) if (f.ns == ns) { text << "    " << f.line << "\n"; ++out.functions; }
        text << "} end extern\n";
        for (auto it = ns.rbegin(); it != ns.rend(); ++it) text << "} end namespace " << *it << "\n";
    }

    out.walnut_text = text.str();
    return out;
}

void note_skipped(const std::string& name, const std::string& reason) {
    std::lock_guard<std::mutex> lock(skip_mutex());
    const std::size_t at = name.rfind("::");
    skips()[at == std::string::npos ? name : name.substr(at + 2)] = reason;
    skips()[name] = reason;
}

std::string skipped_reason(std::string_view name) {
    std::lock_guard<std::mutex> lock(skip_mutex());
    auto it = skips().find(std::string(name));
    return it == skips().end() ? std::string() : it->second;
}

} // namespace native
} // namespace walnut
