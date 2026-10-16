#include "Common/error_reporter.hpp"

namespace walnut {

void ErrorReporter::report(ErrorPhase phase, FileId file_id, std::size_t line, std::string message) {
    std::vector<CompilerError>& dst = m_traps.empty() ? m_errors : m_traps.back();
    dst.push_back({ std::move(message), line, phase, file_id });
}

void ErrorReporter::report_at(ErrorPhase phase, FileId file_id, std::size_t line, std::string message, const char* at, std::size_t width) {
    std::vector<CompilerError>& dst = m_traps.empty() ? m_errors : m_traps.back();
    CompilerError e{ std::move(message), line, phase, file_id };
    e.at = at;
    e.width = width;
    dst.push_back(std::move(e));
}

std::string ErrorReporter::source_excerpt(FileId file_id, std::size_t line, const char* at, std::size_t width) const {
    if (!m_sources || file_id == INVALID_FILE || file_id >= m_sources->file_count() || line == 0) return {};
    const char* data = m_sources->data(file_id);
    const std::size_t size = m_sources->size(file_id);
    if (!data || size == 0) return {};
    const char* stop = data + size;
    const char* begin = data;

    if (at && at >= data && at <= stop) {
        begin = at;
        while (begin > data && begin[-1] != '\n') --begin;
    } else {
        at = nullptr;
        std::size_t current = 1;
        while (begin < stop && current < line) {
            if (*begin == '\n') ++current;
            ++begin;
        }
        if (current != line) return {};
    }

    const char* end = begin;
    while (end < stop && *end != '\n' && *end != '\r') ++end;
    std::string text(begin, end);
    if (text.find_first_not_of(" \t") == std::string::npos) return {};

    const std::string number = std::to_string(line);
    const std::string gutter(number.size(), ' ');
    std::string out = "  " + number + " | " + text + "\n";

    if (at) {
        std::string marks;
        for (const char* p = begin; p < at && p < end; ++p) marks += (*p == '\t') ? '\t' : ' ';
        const char* last = width == 0 ? at : at + width;
        if (last > end) last = end;
        bool any = false;
        for (const char* p = at; p < last; ++p) {
            if ((static_cast<unsigned char>(*p) & 0xC0) == 0x80) continue;
            marks += '^';
            any = true;
        }
        if (!any) marks += '^';
        out += "  " + gutter + " | " + marks + "\n";
    }

    return out;
}

void ErrorReporter::flush() const {
    if (m_errors.empty()) return;
    if (m_mode & ErrorOutput::StdErr) write_to_stream(std::cerr);
    if (m_mode & ErrorOutput::StdOut) write_to_stream(std::cout);
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
        os << source_excerpt(e.file_id == INVALID_FILE ? m_current_file : e.file_id, e.line, e.at, e.width);
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
