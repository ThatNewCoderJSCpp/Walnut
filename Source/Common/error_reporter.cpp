#include "Common/error_reporter.hpp"

namespace walnut {

void ErrorReporter::report(ErrorPhase phase, FileId file_id, std::size_t line, std::string message) {
    std::vector<CompilerError>& dst = m_traps.empty() ? m_errors : m_traps.back();
    dst.push_back({ std::move(message), line, phase, file_id });
}

void ErrorReporter::flush() const {
    if (m_errors.empty()) return;
    if (m_mode & ErrorOutput::StdErr) write_to_stream(std::cerr);
    if (m_mode & ErrorOutput::File)   write_to_file();
}

std::string ErrorReporter::display_path(FileId file_id) const {
    if (m_sources && file_id < m_sources->file_count()) {
        switch (m_path_style) {
            case PathStyle::Absolute: return m_sources->absolute_path(file_id);
            case PathStyle::Relative: return m_sources->relative_path(file_id);
            case PathStyle::Filename: return m_sources->filename(file_id);
        }
    }
    return {};
}

bool ErrorReporter::pop_trap(bool commit) {
    std::vector<CompilerError> t = std::move(m_traps.back());
    m_traps.pop_back();
    const bool captured = !t.empty();

    if (commit) {
        std::vector<CompilerError>& dst = m_traps.empty() ? m_errors : m_traps.back();
        for (CompilerError& e : t) { dst.push_back(std::move(e)); }
    }

    return captured;
}

std::string ErrorReporter::display_name(const CompilerError& e) const {
    std::string p = display_path(e.file_id);
    if (p.empty()) p = display_path(m_current_file);
    return p.empty() ? "<unknown>" : p;
}

void ErrorReporter::write_to_stream(std::ostream& os) const {
    for (const auto& e : m_errors) {
        os << "[" << e.phase_name() << "] " << display_name(e) << ":" << e.line << ": " << e.message << "\n";
    }
}

void ErrorReporter::write_to_file() const {
    if (m_output_path.empty()) {
        std::cerr << "Warning: ErrorOutput::File set but no output path configured\n";
        return;
    }

    std::ofstream out(m_output_path);

    if (!out.is_open()) {
        std::cerr << "Warning: Could not open error log: " << m_output_path << "\n";
        return;
    }

    write_to_stream(out);
}

} // namespace walnut
