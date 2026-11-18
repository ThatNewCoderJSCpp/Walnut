#ifndef WALNUT_CODEGEN_CODE_BUFFER_HPP
#define WALNUT_CODEGEN_CODE_BUFFER_HPP

#include <charconv>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>

namespace walnut {
namespace codegen {

class CodeBuffer {
    std::string m_text;

public:
    CodeBuffer() { m_text.reserve(256); }

    CodeBuffer& operator<<(std::string_view s) { m_text.append(s.data(), s.size()); return *this; }
    CodeBuffer& operator<<(const std::string& s) { m_text.append(s); return *this; }
    CodeBuffer& operator<<(const char* s) { m_text.append(s); return *this; }
    CodeBuffer& operator<<(char c) { m_text.push_back(c); return *this; }
    CodeBuffer& operator<<(const CodeBuffer& other) { m_text.append(other.m_text); return *this; }

    template <typename T, typename = std::enable_if_t<std::is_arithmetic_v<T> && !std::is_same_v<T, char> && !std::is_same_v<T, bool>>>
    CodeBuffer& operator<<(T v) {
        if constexpr (std::is_integral_v<T>) {
            char buf[32];
            const auto r = std::to_chars(buf, buf + sizeof buf, v);
            m_text.append(buf, static_cast<std::size_t>(r.ptr - buf));
        } else {
            std::ostringstream os;
            os << v;
            m_text.append(os.str());
        }
        return *this;
    }

    CodeBuffer& operator<<(bool) = delete;

    const std::string& str() const& noexcept { return m_text; }
    std::string str() && noexcept { return std::move(m_text); }
    std::string& text() noexcept { return m_text; }
    void str(std::string s) { m_text = std::move(s); }
    bool empty() const noexcept { return m_text.empty(); }
    std::size_t size() const noexcept { return m_text.size(); }
    void clear() noexcept { m_text.clear(); }
};

inline std::ostream& operator<<(std::ostream& os, const CodeBuffer& b) {
    return os.write(b.str().data(), static_cast<std::streamsize>(b.str().size()));
}

} // namespace codegen
} // namespace walnut

#endif // WALNUT_CODEGEN_CODE_BUFFER_HPP
