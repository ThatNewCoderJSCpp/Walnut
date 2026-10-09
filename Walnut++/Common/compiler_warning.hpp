#ifndef WALNUT_WARNING_REPORTER_HPP
#define WALNUT_WARNING_REPORTER_HPP

#include <string>
#include <vector>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <cstdint>
#include "error_reporter.hpp"   

namespace walnut {

struct CompilerWarning {
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

class WarningReporter {
private:
    std::vector<CompilerWarning> m_warnings;
    std::string m_output_path;
    ErrorOutput m_mode;
    const SourceManager* m_sources = nullptr;
    FileId m_current_file = 0;
    PathStyle m_path_style = PathStyle::Absolute;
    bool m_enabled = true;           

public:
    explicit WarningReporter(ErrorOutput mode = ErrorOutput::Silent) : m_mode(mode) {}

    void set_sources(const SourceManager* sources) noexcept { m_sources = sources; }
    void set_current_file(FileId id) noexcept { m_current_file = id; }
    FileId current_file() const noexcept { return m_current_file; }
    void set_path_style(PathStyle style) noexcept { m_path_style = style; }
    PathStyle path_style() const noexcept { return m_path_style; }

    void set_enabled(bool on) noexcept { m_enabled = on; }
    bool enabled() const noexcept { return m_enabled; }

    void report(ErrorPhase phase, FileId file_id, std::size_t line, std::string message) {
        if (!m_enabled) return;      
        m_warnings.push_back({ std::move(message), line, phase, file_id });
    }

    void report(ErrorPhase phase, std::size_t line, std::string message) {
        report(phase, m_current_file, line, std::move(message));
    }

    void set_output_path(std::string path) { m_output_path = std::move(path); }
    void set_mode(ErrorOutput mode) noexcept { m_mode = mode; }
    ErrorOutput mode() const noexcept { return m_mode; }

    bool has_warnings() const noexcept { return !m_warnings.empty(); }
    std::size_t warning_count() const noexcept { return m_warnings.size(); }
    const std::vector<CompilerWarning>& warnings() const noexcept { return m_warnings; }

    std::size_t count(ErrorPhase phase) const noexcept {
        std::size_t n = 0;
        for (const auto& w : m_warnings) { if (w.phase == phase) ++n; }
        return n;
    }

    void flush() const {
        if (m_warnings.empty()) return;
        if (m_mode & ErrorOutput::StdErr) write_to_stream(std::cerr);
        if (m_mode & ErrorOutput::File)   write_to_file();
    }

    void clear() noexcept { m_warnings.clear(); }

    std::string display_path(FileId file_id) const {
        if (m_sources && file_id < m_sources->file_count()) {
            switch (m_path_style) {
                case PathStyle::Absolute: return m_sources->absolute_path(file_id);
                case PathStyle::Relative: return m_sources->relative_path(file_id);
                case PathStyle::Filename: return m_sources->filename(file_id);
            }
        }
        return {};
    }

private:
    std::string display_name(const CompilerWarning& w) const {
        std::string p = display_path(w.file_id);
        if (p.empty()) p = display_path(m_current_file);
        return p.empty() ? "<unknown>" : p;
    }

    void write_to_stream(std::ostream& os) const {
        for (const auto& w : m_warnings) {
            os << "[" << w.phase_name() << " warning] " << display_name(w) << ":" << w.line << ": " << w.message << "\n";
        }
    }

    void write_to_file() const {
        if (m_output_path.empty()) {
            std::cerr << "Warning: WarningReporter File mode set but no output path configured\n";
            return;
        }

        std::ofstream out(m_output_path);
        
        if (!out.is_open()) {
            std::cerr << "Warning: Could not open warning log: " << m_output_path << "\n";
            return;
        }
        
        write_to_stream(out);
    }
};

} // namespace walnut

#endif // WALNUT_WARNING_REPORTER_HPP