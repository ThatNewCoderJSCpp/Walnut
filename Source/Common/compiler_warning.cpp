#include "Common/compiler_warning.hpp"

namespace walnut {

void WarningReporter::flush() const {
    if (m_warnings.empty()) return;
    if (m_mode & ErrorOutput::StdErr) write_to_stream(std::cerr);
    if (m_mode & ErrorOutput::File)   write_to_file();
}

std::string WarningReporter::display_path(FileId file_id) const {
    if (m_sources && file_id < m_sources->file_count()) {
        switch (m_path_style) {
            case PathStyle::Absolute: return m_sources->absolute_path(file_id);
            case PathStyle::Relative: return m_sources->relative_path(file_id);
            case PathStyle::Filename: return m_sources->filename(file_id);
        }
    }
    return {};
}

std::string WarningReporter::display_name(const CompilerWarning& w) const {
    std::string p = display_path(w.file_id);
    if (p.empty()) p = display_path(m_current_file);
    return p.empty() ? "<unknown>" : p;
}

void WarningReporter::write_to_stream(std::ostream& os) const {
    for (const auto& w : m_warnings) {
        os << "[" << w.phase_name() << " warning] " << display_name(w) << ":" << w.line << ": " << w.message << "\n";
    }
}

void WarningReporter::write_to_file() const {
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

} // namespace walnut
