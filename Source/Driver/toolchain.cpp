#include "Driver/toolchain.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

#if defined(_WIN32)
#include <io.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace walnut {
namespace toolchain {

namespace fs = std::filesystem;

namespace {

const char* const kMarker = "walnut-toolchain.txt";

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

std::string quote(const std::string& s) {
#if defined(_WIN32)
    return "\"" + s + "\"";
#else
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    return out + "'";
#endif
}

int run(const std::string& command) {
#if defined(_WIN32)
    return std::system(("\"" + command + "\"").c_str());
#else
    const int status = std::system(command.c_str());
    if (status == -1) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
#endif
}

std::string capture(const std::string& command) {
#if defined(_WIN32)
    FILE* pipe = _popen(("\"" + command + " 2>NUL\"").c_str(), "r");
#else
    FILE* pipe = popen((command + " 2>/dev/null").c_str(), "r");
#endif
    if (!pipe) return {};
    std::string out;
    char buf[1024];
    while (std::fgets(buf, sizeof buf, pipe)) out += buf;
#if defined(_WIN32)
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    return out;
}

std::string first_line(const std::string& text) {
    const std::size_t end = text.find_first_of("\r\n");
    return end == std::string::npos ? text : text.substr(0, end);
}

std::vector<std::string> split_path(const std::string& s) {
#if defined(_WIN32)
    const char sep = ';';
#else
    const char sep = ':';
#endif
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::optional<fs::path> find_program(const std::string& name) {
    std::vector<std::string> names{ name };
#if defined(_WIN32)
    if (fs::path(name).extension().empty()) names.push_back(name + ".exe");
#endif
    std::error_code ec;
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) {
        for (const std::string& n : names) if (fs::is_regular_file(n, ec)) return fs::path(n);
        return std::nullopt;
    }
    for (const std::string& dir : split_path(env("PATH"))) {
        for (const std::string& n : names) {
            const fs::path p = fs::path(dir) / n;
            if (fs::is_regular_file(p, ec)) return p;
        }
    }
    return std::nullopt;
}

#if defined(_WIN32)
std::string megabytes(std::uint64_t bytes) {
    return std::to_string((bytes + 500000) / 1000000) + " MB";
}

fs::path system32_tool(const char* name) {
    const std::string root = env("SystemRoot").empty() ? std::string("C:\\Windows") : env("SystemRoot");
    return fs::path(root) / "System32" / name;
}

bool download(const std::string& url, const fs::path& to) {
    std::error_code ec;
    fs::path curl = system32_tool("curl.exe");
    if (!fs::is_regular_file(curl, ec)) {
        if (const std::optional<fs::path> found = find_program("curl")) curl = *found;
        else curl.clear();
    }
    if (!curl.empty()) {
        return run(quote(curl.string()) + " -L --fail --retry 3 --progress-bar -o " + quote(to.string()) + " " + quote(url)) == 0;
    }
    return run("powershell -NoProfile -ExecutionPolicy Bypass -Command \"$ProgressPreference='SilentlyContinue'; Invoke-WebRequest -Uri '" + url + "' -OutFile '" + to.string() + "'\"") == 0;
}

bool extract(const fs::path& zip, const fs::path& into) {
    std::error_code ec;
    const fs::path tar = system32_tool("tar.exe");
    if (fs::is_regular_file(tar, ec)) {
        if (run(quote(tar.string()) + " -xf " + quote(zip.string()) + " -C " + quote(into.string())) == 0) return true;
    }
    return run("powershell -NoProfile -ExecutionPolicy Bypass -Command \"Expand-Archive -LiteralPath '" + zip.string() + "' -DestinationPath '" + into.string() + "' -Force\"") == 0;
}
#endif

struct Sha256 {
    std::array<std::uint32_t, 8> h{ 0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u };
    std::array<unsigned char, 64> block{};
    std::size_t used = 0;
    std::uint64_t total = 0;

    static std::uint32_t rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    void compress(const unsigned char* p) {
        static const std::uint32_t k[64] = {
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
            0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
            0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
            0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
            0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
            0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
            0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
            0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
        };
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i) w[i] = (std::uint32_t(p[i * 4]) << 24) | (std::uint32_t(p[i * 4 + 1]) << 16) | (std::uint32_t(p[i * 4 + 2]) << 8) | std::uint32_t(p[i * 4 + 3]);
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            const std::uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    void update(const unsigned char* data, std::size_t n) {
        total += n;
        while (n > 0) {
            const std::size_t take = std::min(n, block.size() - used);
            std::copy(data, data + take, block.begin() + static_cast<std::ptrdiff_t>(used));
            used += take;
            data += take;
            n -= take;
            if (used == block.size()) {
                compress(block.data());
                used = 0;
            }
        }
    }

    std::string finish() {
        const std::uint64_t bits = total * 8;
        const unsigned char one = 0x80;
        update(&one, 1);
        const unsigned char zero = 0;
        while (used != 56) update(&zero, 1);
        unsigned char len[8];
        for (int i = 0; i < 8; ++i) len[i] = static_cast<unsigned char>(bits >> (56 - 8 * i));
        update(len, 8);
        static const char* digits = "0123456789abcdef";
        std::string out;
        for (std::uint32_t v : h) {
            for (int shift = 28; shift >= 0; shift -= 4) out += digits[(v >> shift) & 15u];
        }
        return out;
    }
};

#if !defined(_WIN32) && !defined(__APPLE__)
std::string os_release_value(const std::string& text, const std::string& key) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind(key + "=", 0) != 0) continue;
        std::string v = line.substr(key.size() + 1);
        if (v.size() >= 2 && (v.front() == '"' || v.front() == '\'')) v = v.substr(1, v.size() - 2);
        return v;
    }
    return {};
}

bool has_word(const std::string& list, const std::string& word) {
    std::istringstream in(list);
    std::string w;
    while (in >> w) if (w == word) return true;
    return false;
}
#endif

int install_with_package_manager(const InitOptions& o) {
#if defined(_WIN32)
    (void)o;
    std::cerr << "walnut: this walnut has no compiler to download; install MSYS2 (https://www.msys2.org) and its C++ compiler\n";
    return 1;
#elif defined(__APPLE__)
    std::cout << "walnut: macOS's C++ compiler comes with Apple's Command Line Tools\n";
    if (!ask_yes("Open Apple's installer for them now?", o.yes)) return 1;
    if (run("xcode-select --install") != 0) {
        std::cerr << "walnut: xcode-select couldn't start the installer; install the Command Line Tools from https://developer.apple.com/download/all/\n";
        return 1;
    }
    std::cout << "walnut: finish the installer that opened, then 'walnut init --status' checks that the compiler works\n";
    return 0;
#else
    std::string text;
    {
        std::ifstream f("/etc/os-release");
        std::ostringstream ss;
        ss << f.rdbuf();
        text = ss.str();
    }
    const std::string id = os_release_value(text, "ID");
    const std::string like = id + " " + os_release_value(text, "ID_LIKE");

    std::string command;
    std::string name;
    if (has_word(like, "fedora") || has_word(like, "rhel") || has_word(like, "centos")) {
        command = find_program("dnf") ? "dnf install -y gcc-c++" : "yum install -y gcc-c++";
        name = "gcc-c++";
    } else if (has_word(like, "debian") || has_word(like, "ubuntu")) {
        command = "sh -c 'apt-get update && apt-get install -y g++'";
        name = "g++";
    } else if (has_word(like, "arch")) {
        command = "pacman -S --needed --noconfirm gcc";
        name = "gcc";
    } else if (has_word(like, "suse") || has_word(like, "opensuse")) {
        command = "zypper install -y gcc-c++";
        name = "gcc-c++";
    } else if (has_word(like, "alpine")) {
        command = "apk add g++";
        name = "g++";
    } else if (has_word(like, "void")) {
        command = "xbps-install -y gcc";
        name = "gcc";
    } else {
        std::cerr << "walnut: walnut doesn't know how to install a compiler on this system" << (id.empty() ? std::string() : " (" + id + ")") << "\n"
                  << "walnut: install GCC's C++ compiler (g++) or Clang with your package manager, then run 'walnut init --status'\n";
        return 1;
    }

    const bool root = geteuid() == 0;
    if (!root) {
        if (!find_program("sudo")) {
            std::cerr << "walnut: installing " << name << " needs administrator rights; as root, run:\n    " << command << "\n";
            return 1;
        }
        command = "sudo " + command;
    }
    std::cout << "walnut: no C++ compiler found; walnut can install " << name << " with:\n    " << command << "\n";
    if (!ask_yes("Run it now?", o.yes)) return 1;
    if (run(command) != 0) {
        std::cerr << "walnut: the install command failed\n";
        return 1;
    }
    for (const char* c : { "c++", "g++", "clang++" }) {
        if (find_program(c)) {
            std::cout << "walnut: ready; programs will be compiled with " << c << "\n";
            return 0;
        }
    }
    std::cerr << "walnut: the install finished, but no C++ compiler is on PATH yet; open a new terminal and run 'walnut init --status'\n";
    return 1;
#endif
}

int install_pinned(const Pinned& p, const InitOptions& o) {
#if defined(_WIN32)
    std::error_code ec;
    const fs::path dir = install_dir(p);
    const fs::path base = root();
    std::cout << "walnut: walnut's compiler is " << p.description << "\n"
              << "walnut: it's about " << megabytes(p.size) << " to download and goes in " << dir.string() << "\n";
    if (!ask_yes("Download it now?", o.yes)) return 1;

    fs::create_directories(base, ec);
    if (ec) {
        std::cerr << "walnut: can't create " << base.string() << ": " << ec.message() << "\n";
        return 1;
    }
    const fs::path zip = base / (p.name + ".zip.part");
    const fs::path partial = base / (p.name + ".partial");
    fs::remove(zip, ec);
    fs::remove_all(partial, ec);

    std::cout << "walnut: downloading " << p.url << "\n";
    if (!download(p.url, zip) || !fs::is_regular_file(zip, ec)) {
        fs::remove(zip, ec);
        std::cerr << "walnut: the download failed; check the internet connection and run 'walnut init' again\n";
        return 1;
    }
    std::cout << "walnut: checking the download\n";
    const std::string got = sha256_file(zip);
    if (got != p.sha256) {
        fs::remove(zip, ec);
        std::cerr << "walnut: the download isn't the expected file (its SHA-256 is " << got << ", not " << p.sha256 << "), so it was deleted\n";
        return 1;
    }
    std::cout << "walnut: unpacking\n";
    fs::create_directories(partial, ec);
    if (!extract(zip, partial)) {
        fs::remove(zip, ec);
        fs::remove_all(partial, ec);
        std::cerr << "walnut: couldn't unpack the compiler\n";
        return 1;
    }
    fs::remove(zip, ec);
    if (!fs::is_regular_file(partial / p.archive_root / p.compiler, ec)) {
        fs::remove_all(partial, ec);
        std::cerr << "walnut: the unpacked files don't have " << (fs::path(p.archive_root) / p.compiler).string() << "\n";
        return 1;
    }
    fs::remove_all(dir, ec);
    fs::rename(partial, dir, ec);
    if (ec) {
        fs::remove_all(partial, ec);
        std::cerr << "walnut: couldn't move the compiler into " << dir.string() << ": " << ec.message() << "\n";
        return 1;
    }
    {
        std::ofstream marker(dir / kMarker, std::ios::binary);
        marker << p.name << "\n" << p.sha256 << "\n" << p.url << "\n";
    }

    const std::optional<fs::path> cxx = installed_compiler();
    if (!cxx) {
        std::cerr << "walnut: the compiler was installed but can't be found\n";
        return 1;
    }
    add_to_path(installed_path_dirs());
    const std::string version = first_line(capture(quote(cxx->string()) + " --version"));
    if (version.empty()) {
        std::cerr << "walnut: " << cxx->string() << " was installed but doesn't run\n";
        return 1;
    }
    std::cout << "walnut: ready; programs will be compiled with " << version << "\n";
    return 0;
#else
    (void)p;
    (void)o;
    return 1;
#endif
}

} // namespace

std::optional<Pinned> pinned() {
#if defined(_WIN32) && (defined(__aarch64__) || defined(_M_ARM64))
    Pinned p;
    p.name = "llvm-mingw-20261020-ucrt-aarch64";
    p.description = "llvm-mingw 20261020 (Clang 23.1.4, UCRT, ARM64)";
    p.url = "https://github.com/mstorsjo/llvm-mingw/releases/download/20261020/llvm-mingw-20261020-ucrt-aarch64.zip";
    p.sha256 = "8f24fedd0b75c674c6c83dec2d76a7ac31a690120212843a17e465ef49854f69";
    p.archive_root = "llvm-mingw-20261020-ucrt-aarch64";
    p.compiler = "bin/clang++.exe";
    p.path_dirs = { "bin", "aarch64-w64-mingw32/bin" };
    p.size = 185737889;
    return p;
#elif defined(_WIN32) && defined(_UCRT)
    Pinned p;
    p.name = "winlibs-gcc-16.2.0-ucrt-r3";
    p.description = "WinLibs GCC 16.2.0 with MinGW-w64 14.0.0 (UCRT, x86_64)";
    p.url = "https://github.com/brechtsanders/winlibs_mingw/releases/download/16.2.0posix-23.1.3-14.0.0-ucrt-r3/winlibs-x86_64-posix-seh-gcc-16.2.0-mingw-w64ucrt-14.0.0-r3.zip";
    p.sha256 = "3c69305081e4a647144339a936244fcf7d3d9e558319e1ad7be2cd2886d4b6a1";
    p.archive_root = "mingw64";
    p.compiler = "bin/g++.exe";
    p.path_dirs = { "bin" };
    p.size = 276414382;
    return p;
#elif defined(_WIN32)
    Pinned p;
    p.name = "winlibs-gcc-16.2.0-msvcrt-r3";
    p.description = "WinLibs GCC 16.2.0 with MinGW-w64 14.0.0 (MSVCRT, x86_64)";
    p.url = "https://github.com/brechtsanders/winlibs_mingw/releases/download/16.2.0posix-23.1.3-14.0.0-msvcrt-r3/winlibs-x86_64-posix-seh-gcc-16.2.0-mingw-w64msvcrt-14.0.0-r3.zip";
    p.sha256 = "62af32ea58acdea1318ea5dd8eb7017eaee694dd40b269d52a59c769dcfefb82";
    p.archive_root = "mingw64";
    p.compiler = "bin/g++.exe";
    p.path_dirs = { "bin" };
    p.size = 277203392;
    return p;
#else
    return std::nullopt;
#endif
}

fs::path root() {
    const std::string over = env("WALNUT_TOOLCHAIN_DIR");
    if (!over.empty()) return fs::path(over);
#if defined(_WIN32)
    if (!env("LOCALAPPDATA").empty()) return fs::path(env("LOCALAPPDATA")) / "walnut" / "toolchain";
    return fs::temp_directory_path() / "walnut" / "toolchain";
#else
    if (!env("XDG_DATA_HOME").empty()) return fs::path(env("XDG_DATA_HOME")) / "walnut" / "toolchain";
    if (!env("HOME").empty()) return fs::path(env("HOME")) / ".local" / "share" / "walnut" / "toolchain";
    return fs::temp_directory_path() / "walnut" / "toolchain";
#endif
}

fs::path install_dir(const Pinned& p) {
    return root() / p.name;
}

std::optional<fs::path> installed_compiler() {
    const std::optional<Pinned> p = pinned();
    if (!p) return std::nullopt;
    const fs::path dir = install_dir(*p);
    std::error_code ec;
    const fs::path cxx = dir / p->archive_root / p->compiler;
    if (!fs::is_regular_file(dir / kMarker, ec) || !fs::is_regular_file(cxx, ec)) return std::nullopt;
    return cxx.lexically_normal().make_preferred();
}

std::vector<fs::path> installed_path_dirs() {
    std::vector<fs::path> out;
    const std::optional<Pinned> p = pinned();
    if (!p || !installed_compiler()) return out;
    for (const std::string& d : p->path_dirs) out.push_back((install_dir(*p) / p->archive_root / d).lexically_normal().make_preferred());
    return out;
}

void add_to_path(const std::vector<fs::path>& dirs) {
    if (dirs.empty()) return;
#if defined(_WIN32)
    const char sep = ';';
#else
    const char sep = ':';
#endif
    std::string value;
    for (const fs::path& d : dirs) {
        if (!value.empty()) value += sep;
        value += d.string();
    }
    const std::string old = env("PATH");
    if (!old.empty()) value += sep + old;
#if defined(_WIN32)
    _putenv_s("PATH", value.c_str());
#else
    setenv("PATH", value.c_str(), 1);
#endif
}

std::string sha256_bytes(const std::string& data) {
    Sha256 s;
    s.update(reinterpret_cast<const unsigned char*>(data.data()), data.size());
    return s.finish();
}

std::string sha256_file(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return {};
    Sha256 s;
    std::vector<char> buf(1 << 20);
    while (in) {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const std::streamsize n = in.gcount();
        if (n > 0) s.update(reinterpret_cast<const unsigned char*>(buf.data()), static_cast<std::size_t>(n));
    }
    return s.finish();
}

bool interactive() {
#if defined(_WIN32)
    return _isatty(_fileno(stdin)) && _isatty(_fileno(stderr));
#else
    return isatty(fileno(stdin)) && isatty(fileno(stderr));
#endif
}

bool ask_yes(const std::string& question, bool assume_yes) {
    if (assume_yes) return true;
    if (!interactive()) {
        std::cerr << "walnut: " << question << " (not asking, since this isn't a terminal; add --yes to go ahead)\n";
        return false;
    }
    std::cerr << question << " [Y/n] " << std::flush;
    std::string answer;
    if (!std::getline(std::cin, answer)) return false;
    while (!answer.empty() && (answer.back() == ' ' || answer.back() == '\r')) answer.pop_back();
    while (!answer.empty() && answer.front() == ' ') answer.erase(answer.begin());
    return answer.empty() || answer == "y" || answer == "Y" || answer == "yes" || answer == "Yes" || answer == "YES";
}

int init(const InitOptions& o) {
    const std::optional<Pinned> p = pinned();
    const std::optional<fs::path> managed = installed_compiler();

    if (o.print_compiler) {
        const std::optional<fs::path> found = find_program(o.current_cxx);
        if (!found) return 1;
        std::cout << fs::absolute(*found).lexically_normal().string() << "\n";
        return 0;
    }

    if (o.remove) {
        std::error_code ec;
        const fs::path dir = root();
        if (!fs::exists(dir, ec)) {
            std::cout << "walnut: there's no compiler installed by walnut init\n";
            return 0;
        }
        const std::uintmax_t n = fs::remove_all(dir, ec);
        if (ec) {
            std::cerr << "walnut: couldn't delete " << dir.string() << ": " << ec.message() << "\n";
            return 1;
        }
        std::cout << "walnut: deleted " << dir.string() << " (" << n << " files)\n";
        return 0;
    }

    if (o.status) {
        const std::optional<fs::path> found = find_program(o.current_cxx);
        std::string source = "the system";
        if (o.cxx_from_user) source = "--cxx or WALNUT_CXX";
        else if (managed && found) {
            std::error_code ec;
            if (fs::equivalent(*found, *managed, ec)) source = "walnut init";
        }
        if (!found) {
            std::cout << "compiler: none (looked for '" << o.current_cxx << "')\n"
                      << "walnut: run 'walnut init' to set one up\n";
            return 1;
        }
        std::cout << "compiler: " << fs::absolute(*found).lexically_normal().string() << " (from " << source << ")\n";
        const std::string version = first_line(capture(quote(found->string()) + " --version"));
        std::cout << "version:  " << (version.empty() ? std::string("unknown (it didn't answer --version)") : version) << "\n";
        if (p) std::cout << "walnut init installs: " << p->description << (managed ? " (installed)" : " (not installed)") << "\n";
        return version.empty() ? 1 : 0;
    }

    if (p) {
        if (managed && !o.force) {
            std::cout << "walnut: already set up; programs are compiled with " << managed->string() << "\n";
            return 0;
        }
        if (!managed && !o.force && !o.cxx_from_user) {
            if (const std::optional<fs::path> found = find_program(o.current_cxx)) {
                std::cout << "walnut: found a C++ compiler at " << found->string() << ", so walnut will use it\n"
                          << "walnut: 'walnut init --force' downloads walnut's own compiler instead\n";
                return 0;
            }
        }
        return install_pinned(*p, o);
    }

    if (const std::optional<fs::path> found = find_program(o.current_cxx)) {
        std::cout << "walnut: ready; programs are compiled with " << found->string() << "\n";
        return 0;
    }
    return install_with_package_manager(o);
}

} // namespace toolchain
} // namespace walnut
