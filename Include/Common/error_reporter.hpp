#ifndef WALNUT_ERROR_REPORTER_HPP
#define WALNUT_ERROR_REPORTER_HPP

#include <string>
#include <vector>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <cstdint>
#include "source_manager.hpp"

namespace walnut {

enum class ErrorPhase : std::uint8_t {
    Lexer = 0,
    Parser,
    Semantic,
    Preprocessor,
    Loader
};

enum class PathStyle : std::uint8_t {
    Absolute = 0,
    Relative,    
    Filename       
};

enum class ErrorOutput : std::uint8_t {
    Silent = 0,
    StdErr = 1 << 0,
    File   = 1 << 1
};

constexpr ErrorOutput operator|(ErrorOutput a, ErrorOutput b) noexcept {
    return static_cast<ErrorOutput>(
        static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b)
    );
}

constexpr bool operator&(ErrorOutput a, ErrorOutput b) noexcept {
    return (static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(b)) != 0;
}

struct CompilerError {
    std::string message;
    std::size_t line;
    ErrorPhase  phase;
    FileId      file_id = INVALID_FILE;

    constexpr const char* phase_name() const noexcept {
        switch (phase) {
            case ErrorPhase::Lexer:        return "Lexer";
            case ErrorPhase::Parser:       return "Parser";
            case ErrorPhase::Semantic:     return "Semantic";
            case ErrorPhase::Preprocessor: return "Preprocessor";
            case ErrorPhase::Loader:       return "Loader";
            default:                       return "Unknown";
        }
    }
};

class ErrorReporter {
private:
    std::vector<CompilerError> m_errors;                 
    std::string m_output_path;
    ErrorOutput m_mode;
    const SourceManager* m_sources = nullptr; 
    FileId m_current_file = INVALID_FILE;                
    PathStyle m_path_style = PathStyle::Absolute;
    std::vector<std::vector<CompilerError>> m_traps;

public:
    explicit ErrorReporter(ErrorOutput mode = ErrorOutput::Silent) : m_mode(mode) {}

    void set_sources(const SourceManager* sources) noexcept { m_sources = sources; }

    void set_current_file(FileId id) noexcept { m_current_file = id; }
    FileId current_file() const noexcept { return m_current_file; }

    void set_path_style(PathStyle style) noexcept { m_path_style = style; }
    PathStyle path_style() const noexcept { return m_path_style; }

    void report(ErrorPhase phase, FileId file_id, std::size_t line, std::string message);

    void report(ErrorPhase phase, std::size_t line, std::string message) {
        report(phase, m_current_file, line, std::move(message));
    }

    void set_output_path(std::string path) { m_output_path = std::move(path); }
    void set_mode(ErrorOutput mode) noexcept { m_mode = mode; }
    ErrorOutput mode() const noexcept { return m_mode; }
    bool has_errors() const noexcept { return !m_errors.empty(); }
    std::size_t error_count() const noexcept { return m_errors.size(); }
    const std::vector<CompilerError>& errors() const noexcept { return m_errors; }

    std::size_t count(ErrorPhase phase) const noexcept {
        std::size_t n = 0;
        for (const auto& e : m_errors) { if (e.phase == phase) ++n; }
        return n;
    }

    void flush() const;

    void clear() noexcept { m_errors.clear(); }

    std::string display_path(FileId file_id) const;

    void push_trap() { m_traps.emplace_back(); }

    bool pop_trap(bool commit);

private:
    std::string display_name(const CompilerError& e) const;

    void write_to_stream(std::ostream& os) const;

    void write_to_file() const;
};

class DiagnosticTrap {
public:
    explicit DiagnosticTrap(ErrorReporter& r) : m_r(r) { m_r.push_trap(); }
    ~DiagnosticTrap() { if (!m_done) { m_r.pop_trap(false); } }   

    bool commit()  { m_done = true; return m_r.pop_trap(true);  }
    bool discard() { m_done = true; return m_r.pop_trap(false); }

    DiagnosticTrap(const DiagnosticTrap&) = delete;
    DiagnosticTrap& operator=(const DiagnosticTrap&) = delete;

private:
    ErrorReporter& m_r;
    bool m_done = false;
};

template <typename Reporter>
class ScopedFile {
public:
    ScopedFile(Reporter& r, FileId id) : m_r(r), m_prev(r.current_file()) { m_r.set_current_file(id); }
    ~ScopedFile() { m_r.set_current_file(m_prev); }

    ScopedFile(const ScopedFile&) = delete;
    ScopedFile& operator=(const ScopedFile&) = delete;

private:
    Reporter& m_r;
    FileId    m_prev;
};

} // namespace walnut

#endif // WALNUT_ERROR_REPORTER_HPP