#include "walnut_cli.hpp"

#include <cinttypes>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>

namespace walnut_cli {

namespace {

struct LineTime {
    std::string   file;
    int           line = 0;
    std::uint64_t ns = 0;
    std::uint64_t hits = 0;
};

struct FunctionTime {
    std::string   name;
    std::string   file;
    int           line = 0;
    int           last = 0;
    std::uint64_t calls = 0;
    std::uint64_t total = 0;
    std::uint64_t own = 0;
};

struct LineMemory {
    std::string   file;
    int           line = 0;
    std::uint64_t bytes = 0;
    std::uint64_t blocks = 0;
    std::uint64_t peak = 0;
    std::uint64_t held = 0;
};

struct FunctionMemory {
    std::string   name;
    std::string   file;
    int           line = 0;
    std::uint64_t bytes = 0;
    std::uint64_t blocks = 0;
    std::uint64_t total = 0;
};

struct Field {
    std::string   name;
    std::uint64_t size = 0;
    std::uint64_t align = 1;
};

struct Layout {
    std::string        name;
    std::string        file;
    int                line = 0;
    std::uint64_t      size = 0;
    std::uint64_t      align = 1;
    std::vector<Field> fields;
    std::uint64_t      padding = 0;
    std::uint64_t      better = 0;
    std::vector<Field> order;
    std::map<int, int> moves;
};

struct Suggestion {
    const walnut::driver::PerfHint* hint = nullptr;
    double share = 0;
    std::string reason;
};

struct GrowthPoint {
    double n = 0;
    double calls = 0;
    double ns = 0;
    double heap = 0;
    double depth = 0;
};

struct GrowthSeries {
    std::string name;
    std::string file;
    int line = 0;
    std::string param;
    std::vector<GrowthPoint> points;
};

struct Measured {
    std::string name;
    std::string file;
    int line = 0;
    bool ok = false;
    std::string param;
    walnut::driver::Growth time;
    walnut::driver::Growth space;
    double low = 0;
    double high = 0;
    std::size_t sizes = 0;
    double calls = 0;
    std::string note;
};

std::vector<Measured> g_measured;

struct Profile {
    std::vector<GrowthSeries> growth;
    std::uint64_t wall = 0;
    std::uint64_t mark_cost_ps = 0;
    std::uint64_t total = 0;
    std::vector<LineTime> lines;
    std::vector<FunctionTime> functions;
    bool memory = false;
    std::uint64_t bytes = 0;
    std::uint64_t blocks = 0;
    std::uint64_t peak = 0;
    std::uint64_t held = 0;
    std::vector<LineMemory> memory_lines;
    std::vector<FunctionMemory> memory_functions;
    std::vector<Layout> layouts;
};

std::vector<std::string> split_tabs(const std::string& s) {
    std::vector<std::string> out;
    std::size_t at = 0;
    while (true) {
        const std::size_t tab = s.find('\t', at);
        out.push_back(s.substr(at, tab == std::string::npos ? std::string::npos : tab - at));
        if (tab == std::string::npos) break;
        at = tab + 1;
    }
    return out;
}

std::string bytes_text(std::uint64_t n);

std::uint64_t num(const std::string& s) { return static_cast<std::uint64_t>(std::strtoull(s.c_str(), nullptr, 10)); }

std::uint64_t less_cost(std::uint64_t ns, std::uint64_t marks, std::uint64_t cost_ps) {
    const std::uint64_t cost = marks * cost_ps / 1000;
    return ns > cost ? ns - cost : 0;
}

std::string norm(const std::string& p) {
    std::error_code ec;
    fs::path path(p);
    fs::path c = fs::weakly_canonical(path, ec);
    return (ec ? path : c).generic_string();
}

bool read_profile(const fs::path& data, Profile& out) {
    std::ifstream in(data, std::ios::binary);
    if (!in) return false;
    std::string line;
    std::map<std::pair<std::string, int>, LineTime> lines;
    std::map<std::tuple<std::string, std::string, int>, std::pair<FunctionTime, std::uint64_t>> fns;
    std::map<std::pair<std::string, int>, LineMemory> mlines;
    std::map<std::tuple<std::string, std::string, int>, FunctionMemory> mfns;
    bool header = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::vector<std::string> f = split_tabs(line);
        if (f.empty()) continue;
        if (f[0] == "walnut-profile") header = true;
        else if (f[0] == "wall" && f.size() >= 2) out.wall = num(f[1]);
        else if (f[0] == "mark-cost-ps" && f.size() >= 2) out.mark_cost_ps = num(f[1]);
        else if (f[0] == "site" && f.size() >= 5) {
            LineTime& t = lines[{ norm(f[1]), std::atoi(f[2].c_str()) }];
            t.file = norm(f[1]);
            t.line = std::atoi(f[2].c_str());
            t.ns += num(f[3]);
            t.hits += num(f[4]);
        } else if (f[0] == "function" && f.size() >= 8) {
            auto& entry = fns[{ f[1], norm(f[2]), std::atoi(f[3].c_str()) }];
            entry.first.name = f[1];
            entry.first.file = norm(f[2]);
            entry.first.line = std::atoi(f[3].c_str());
            entry.first.calls += num(f[4]);
            entry.first.total += less_cost(num(f[5]), num(f[6]), out.mark_cost_ps);
            entry.second += num(f[7]);
        } else if (f[0] == "memory" && f.size() >= 5) {
            out.memory = true;
            out.bytes = num(f[1]);
            out.blocks = num(f[2]);
            out.peak = num(f[3]);
            out.held = num(f[4]);
        } else if (f[0] == "msite" && f.size() >= 7) {
            LineMemory& m = mlines[{ norm(f[1]), std::atoi(f[2].c_str()) }];
            m.file = norm(f[1]);
            m.line = std::atoi(f[2].c_str());
            m.bytes += num(f[3]);
            m.blocks += num(f[4]);
            m.peak = std::max(m.peak, num(f[5]));
            m.held += num(f[6]);
        } else if (f[0] == "mfunction" && f.size() >= 7) {
            FunctionMemory& m = mfns[{ f[1], norm(f[2]), std::atoi(f[3].c_str()) }];
            m.name = f[1];
            m.file = norm(f[2]);
            m.line = std::atoi(f[3].c_str());
            m.bytes += num(f[4]);
            m.blocks += num(f[5]);
            m.total += num(f[6]);
        } else if (f[0] == "growth" && f.size() >= 10) {
            const std::string file = norm(f[2]);
            const int line = std::atoi(f[3].c_str());
            GrowthSeries* series = nullptr;
            for (GrowthSeries& g : out.growth) if (g.name == f[1] && g.file == file && g.line == line && g.param == f[4]) series = &g;
            if (!series) {
                out.growth.push_back(GrowthSeries{ f[1], file, line, f[4], {} });
                series = &out.growth.back();
            }
            series->points.push_back(GrowthPoint{ std::atof(f[5].c_str()), std::atof(f[6].c_str()), std::atof(f[7].c_str()), std::atof(f[8].c_str()), std::atof(f[9].c_str()) });
        } else if (f[0] == "layout" && f.size() >= 6) {
            Layout l;
            l.name = f[1];
            l.file = norm(f[2]);
            l.line = std::atoi(f[3].c_str());
            l.size = num(f[4]);
            l.align = std::max<std::uint64_t>(1, num(f[5]));
            if (f.size() >= 7) {
                std::size_t at = 0;
                const std::string& list = f[6];
                while (at < list.size()) {
                    std::size_t comma = list.find(',', at);
                    if (comma == std::string::npos) comma = list.size();
                    const std::string item = list.substr(at, comma - at);
                    const std::size_t a = item.find(':');
                    const std::size_t b = a == std::string::npos ? std::string::npos : item.find(':', a + 1);
                    if (b != std::string::npos) l.fields.push_back(Field{ item.substr(0, a), num(item.substr(a + 1, b - a - 1)), std::max<std::uint64_t>(1, num(item.substr(b + 1))) });
                    at = comma + 1;
                }
            }
            bool seen = false;
            for (const Layout& o : out.layouts) if (o.name == l.name && o.file == l.file && o.line == l.line) seen = true;
            if (!seen) out.layouts.push_back(std::move(l));
        }
    }
    if (!header) return false;
    for (auto& [key, t] : lines) {
        t.ns = less_cost(t.ns, t.hits, out.mark_cost_ps);
        out.total += t.ns;
        out.lines.push_back(t);
    }
    for (auto& [key, entry] : fns) {
        FunctionTime f = entry.first;
        std::uint64_t marks = 0;
        for (const walnut::driver::FunctionSpan& s : g_perf.functions) {
            if (norm(s.file) == f.file && static_cast<int>(s.first) == f.line) { f.last = static_cast<int>(s.last); break; }
        }
        for (const LineTime& t : out.lines) if (t.file == f.file && t.line >= f.line && t.line <= (f.last ? f.last : f.line)) marks += t.hits;
        f.own = less_cost(entry.second, marks, out.mark_cost_ps);
        out.functions.push_back(f);
    }
    for (auto& [key, m] : mlines) out.memory_lines.push_back(m);
    for (auto& [key, m] : mfns) out.memory_functions.push_back(m);
    return true;
}

std::uint64_t round_up(std::uint64_t n, std::uint64_t a) { return a ? (n + a - 1) / a * a : n; }

std::uint64_t packed_size(const std::vector<Field>& fields, std::uint64_t align) {
    std::uint64_t at = 0;
    std::uint64_t most = align;
    for (const Field& f : fields) {
        at = round_up(at, f.align) + f.size;
        most = std::max(most, f.align);
    }
    return round_up(at, most);
}

void plan_layouts(Profile& p) {
    for (Layout& l : p.layouts) {
        std::uint64_t used = 0;
        std::uint64_t most = 1;
        for (const Field& f : l.fields) { used += f.size; most = std::max(most, f.align); }
        const std::uint64_t now = packed_size(l.fields, 1);
        const std::uint64_t extra = l.size > now ? l.size - now : 0;
        l.padding = now > used ? now - used : 0;
        l.order = l.fields;
        std::stable_sort(l.order.begin(), l.order.end(), [](const Field& a, const Field& b) { return a.align != b.align ? a.align > b.align : a.size > b.size; });
        const std::uint64_t sorted = packed_size(l.order, 1);
        l.better = sorted < now ? sorted + extra : l.size;
        if (l.better >= l.size) { l.order.clear(); continue; }
        const walnut::driver::RecordSpan* span = nullptr;
        for (const walnut::driver::RecordSpan& r : g_perf.records) if (norm(r.file) == l.file && static_cast<int>(r.line) == l.line) span = &r;
        if (!span || span->fields.size() != l.fields.size()) continue;
        std::set<std::uint32_t> lines;
        bool one_per_line = true;
        for (const walnut::driver::FieldSpan& fs_ : span->fields) if (!lines.insert(fs_.line).second || !fs_.line) one_per_line = false;
        if (!one_per_line) continue;
        std::map<std::string, std::uint32_t> line_of;
        for (const walnut::driver::FieldSpan& fs_ : span->fields) line_of[fs_.name] = fs_.line;
        std::vector<std::uint32_t> slots;
        for (const Field& f : l.fields) {
            auto it = line_of.find(f.name);
            if (it == line_of.end()) { slots.clear(); break; }
            slots.push_back(it->second);
        }
        if (slots.size() != l.fields.size()) continue;
        std::vector<std::uint32_t> ordered = slots;
        std::sort(ordered.begin(), ordered.end());
        for (std::size_t i = 0; i < l.order.size(); ++i) {
            const std::uint32_t from = line_of[l.order[i].name];
            const std::uint32_t to = ordered[i];
            if (from != to) l.moves[static_cast<int>(to)] = static_cast<int>(from);
        }
    }
}

std::string duration(std::uint64_t ns) {
    std::ostringstream s;
    s << std::fixed;
    if (ns >= 1000000000ull) s << std::setprecision(2) << static_cast<double>(ns) / 1e9 << " s";
    else if (ns >= 1000000ull) s << std::setprecision(1) << static_cast<double>(ns) / 1e6 << " ms";
    else if (ns >= 1000ull) s << std::setprecision(1) << static_cast<double>(ns) / 1e3 << " us";
    else s << ns << " ns";
    return s.str();
}

std::string percent(double p) {
    std::ostringstream s;
    s << std::fixed << std::setprecision(p >= 10 ? 1 : 2) << p << "%";
    return s.str();
}

std::string pad_left(const std::string& s, std::size_t w) { return s.size() >= w ? s : std::string(w - s.size(), ' ') + s; }

std::string pad_right(const std::string& s, std::size_t w) { return s.size() >= w ? s : s + std::string(w - s.size(), ' '); }

class Sources {
public:
    const std::string& line(const std::string& file, int n) {
        auto it = m_files.find(file);
        if (it == m_files.end()) {
            std::vector<std::string> lines;
            std::ifstream in(file, std::ios::binary);
            std::string l;
            while (std::getline(in, l)) {
                if (!l.empty() && l.back() == '\r') l.pop_back();
                lines.push_back(l);
            }
            it = m_files.emplace(file, std::move(lines)).first;
        }
        static const std::string empty;
        if (n < 1 || static_cast<std::size_t>(n) > it->second.size()) return empty;
        return it->second[static_cast<std::size_t>(n - 1)];
    }

    std::size_t count(const std::string& file) {
        line(file, 1);
        return m_files[file].size();
    }

private:
    std::map<std::string, std::vector<std::string>> m_files;
};

std::string line_trimmed(const std::string& s) {
    const std::size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return std::string();
    const std::size_t b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}

std::string short_name(const std::string& file) {
    std::error_code ec;
    const fs::path rel = fs::relative(file, fs::current_path(ec), ec);
    if (!ec && !rel.empty() && rel.generic_string().rfind("..", 0) != 0) return rel.generic_string();
    return fs::path(file).filename().generic_string();
}

bool file_matches(const std::string& file, const std::string& query) {
    const std::string q = fs::path(query).generic_string();
    if (norm(q) == file) return true;
    return file.size() >= q.size() && file.compare(file.size() - q.size(), q.size(), q) == 0 && (file.size() == q.size() || file[file.size() - q.size() - 1] == '/');
}

bool function_matches(const std::string& name, const std::string& query) {
    if (name == query) return true;
    return name.size() > query.size() + 2 && name.compare(name.size() - query.size(), query.size(), query) == 0 && name.compare(name.size() - query.size() - 2, 2, "::") == 0;
}

const FunctionTime* function_at(const Profile& p, const std::string& file, int line) {
    const FunctionTime* best = nullptr;
    for (const FunctionTime& f : p.functions) {
        if (f.file != file || line < f.line || line > (f.last ? f.last : f.line)) continue;
        if (!best || f.line > best->line) best = &f;
    }
    return best;
}

const walnut::driver::FunctionSpan* span_at(const std::string& file, int line) {
    const walnut::driver::FunctionSpan* best = nullptr;
    for (const walnut::driver::FunctionSpan& s : g_perf.functions) {
        if (norm(s.file) != file || line < static_cast<int>(s.first) || line > static_cast<int>(s.last)) continue;
        if (!best || s.first > best->first) best = &s;
    }
    return best;
}

double share_of(const Profile& p, std::uint64_t ns) { return p.total ? 100.0 * static_cast<double>(ns) / static_cast<double>(p.total) : 0; }

std::vector<Suggestion> pick_suggestions(const Profile& p, const Options& o) {
    std::vector<Suggestion> out;
    for (const walnut::driver::PerfHint& h : g_perf.hints) {
        const std::string file = norm(h.file);
        if (!o.profile_files.empty()) {
            bool keep = false;
            for (const std::string& q : o.profile_files) if (file_matches(file, q)) keep = true;
            if (!keep) continue;
        }
        if (!o.profile_functions.empty()) {
            bool keep = false;
            for (const std::string& q : o.profile_functions) if (function_matches(h.function, q)) keep = true;
            if (!keep) continue;
        }
        const LineTime* lt = nullptr;
        for (const LineTime& t : p.lines) if (t.file == file && t.line == static_cast<int>(h.line)) lt = &t;
        const FunctionTime* fn = function_at(p, file, static_cast<int>(h.line));
        const double line_share = lt ? share_of(p, lt->ns) : 0;
        const double fn_share = fn ? share_of(p, fn->total) : 0;
        const std::uint64_t calls = fn ? fn->calls : 0;
        Suggestion s;
        s.hint = &h;
        const std::string kind = h.kind.substr(0, h.kind.find(':'));
        if (kind == "copy-param") {
            if (calls < 100 && fn_share < 5) continue;
            s.share = fn_share;
            s.reason = std::to_string(calls) + " calls, " + percent(fn_share) + " of the time";
        } else if (kind == "memoize") {
            if (calls < 1000) continue;
            s.share = fn_share;
            s.reason = std::to_string(calls) + " calls, " + percent(fn_share) + " of the time";
        } else if (kind == "inline") {
            if (!fn || calls < 10000 || fn_share < 2) continue;
            const double per_call = static_cast<double>(fn->total) / static_cast<double>(calls);
            if (per_call > 500) continue;
            s.share = fn_share;
            s.reason = std::to_string(calls) + " calls of about " + duration(static_cast<std::uint64_t>(per_call)) + " each";
        } else if (kind == "throw") {
            if (!lt || lt->hits < 1000) continue;
            s.share = line_share;
            s.reason = "thrown " + std::to_string(lt->hits) + " times";
        } else if (kind == "wide-int" || kind == "loop-call" || kind == "copy-loop") {
            const double own = fn ? share_of(p, fn->own) : 0;
            s.share = std::max(line_share, own);
            if (s.share < 2) continue;
            s.reason = fn ? "'" + fn->name + "' spends " + percent(own) + " of the time in its own lines" : percent(line_share) + " of the time";
        } else {
            const LineMemory* lm = nullptr;
            for (const LineMemory& m : p.memory_lines) if (m.file == file && m.line == static_cast<int>(h.line)) lm = &m;
            const bool busy_memory = lm && lm->blocks >= 1000 && (kind == "reserve" || kind == "new-in-loop" || kind == "string-append");
            if (line_share < 1 && !busy_memory) continue;
            s.share = line_share;
            s.reason = percent(line_share) + " of the time on this line";
            if (busy_memory) s.reason += ", " + std::to_string(lm->blocks) + " allocations (" + bytes_text(lm->bytes) + ")";
        }
        out.push_back(s);
    }
    std::stable_sort(out.begin(), out.end(), [](const Suggestion& a, const Suggestion& b) { return a.share > b.share; });
    return out;
}

std::string apply_edit(const std::string& current, const std::string& before, const std::string& after) {
    std::size_t pre = 0;
    while (pre < before.size() && pre < after.size() && before[pre] == after[pre]) ++pre;
    std::size_t suf = 0;
    while (suf < before.size() - pre && suf < after.size() - pre && before[before.size() - 1 - suf] == after[after.size() - 1 - suf]) ++suf;
    const std::string old_mid = before.substr(pre, before.size() - pre - suf);
    const std::string new_mid = after.substr(pre, after.size() - pre - suf);
    if (current == before) return after;
    const std::string left = before.substr(0, pre);
    const std::size_t anchor = current.find(left.substr(left.size() > 12 ? left.size() - 12 : 0));
    std::size_t at = old_mid.empty() ? std::string::npos : current.find(old_mid, anchor == std::string::npos ? 0 : anchor);
    if (old_mid.empty()) {
        const std::string right = before.substr(pre, std::min<std::size_t>(12, before.size() - pre));
        at = current.find(right, anchor == std::string::npos ? 0 : anchor);
        if (at == std::string::npos) return current;
        return current.substr(0, at) + new_mid + current.substr(at);
    }
    if (at == std::string::npos) return current;
    return current.substr(0, at) + new_mid + current.substr(at + old_mid.size());
}

std::map<std::string, std::map<int, std::pair<std::string, std::string>>> edits_of(const std::vector<Suggestion>& picked) {
    std::map<std::string, std::map<int, std::pair<std::string, std::string>>> out;
    for (const Suggestion& s : picked) {
        const walnut::driver::PerfHint& h = *s.hint;
        if (h.before.empty() || h.after.empty()) continue;
        auto& slot = out[norm(h.file)][static_cast<int>(h.line)];
        if (slot.first.empty()) slot = { h.before, h.after };
        else slot.second = apply_edit(slot.second, h.before, h.after);
    }
    return out;
}

std::string unified_diff(const std::map<std::string, std::map<int, std::pair<std::string, std::string>>>& edits, Sources& src) {
    std::ostringstream os;
    const int context = 3;
    for (const auto& [file, lines] : edits) {
        std::vector<int> changed;
        for (const auto& [line, change] : lines) if (change.first != change.second && src.line(file, line) == change.first) changed.push_back(line);
        if (changed.empty()) continue;
        const int total = static_cast<int>(src.count(file));
        const std::string name = short_name(file);
        os << "--- a/" << name << "\n+++ b/" << name << "\n";
        std::size_t i = 0;
        while (i < changed.size()) {
            const int first = std::max(1, changed[i] - context);
            int last = std::min(total, changed[i] + context);
            std::size_t j = i + 1;
            while (j < changed.size() && changed[j] - context <= last + 1) { last = std::min(total, changed[j] + context); ++j; }
            const int count = last - first + 1;
            os << "@@ -" << first << "," << count << " +" << first << "," << count << " @@\n";
            for (int l = first; l <= last; ++l) {
                auto it = lines.find(l);
                if (it != lines.end() && std::find(changed.begin() + static_cast<std::ptrdiff_t>(i), changed.begin() + static_cast<std::ptrdiff_t>(j), l) != changed.begin() + static_cast<std::ptrdiff_t>(j)) {
                    os << "-" << it->second.first << "\n+" << it->second.second << "\n";
                } else {
                    os << " " << src.line(file, l) << "\n";
                }
            }
            i = j;
        }
    }
    return os.str();
}

std::string json_text(const std::string& s);

std::string complexity_json(const std::vector<const walnut::driver::ComplexityEstimate*>& rows);

std::string bytes_text(std::uint64_t n) {
    std::ostringstream s;
    s << std::fixed;
    if (n >= 1024ull * 1024 * 1024) s << std::setprecision(2) << static_cast<double>(n) / (1024.0 * 1024 * 1024) << " GB";
    else if (n >= 1024ull * 1024) s << std::setprecision(1) << static_cast<double>(n) / (1024.0 * 1024) << " MB";
    else if (n >= 1024ull) s << std::setprecision(1) << static_cast<double>(n) / 1024.0 << " KB";
    else s << n << " B";
    return s.str();
}

double memory_share(const Profile& p, std::uint64_t n) { return p.bytes ? 100.0 * static_cast<double>(n) / static_cast<double>(p.bytes) : 0; }

bool access_line(const std::string& text) {
    const std::string t = line_trimmed(text);
    return t.rfind("public", 0) == 0 || t.rfind("private", 0) == 0 || t.rfind("protected", 0) == 0;
}

void layout_edits(Profile& p, Sources& src, std::map<std::string, std::map<int, std::pair<std::string, std::string>>>& edits) {
    for (Layout& l : p.layouts) {
        if (l.moves.empty()) continue;
        int first = l.moves.begin()->first;
        int last = l.moves.rbegin()->first;
        for (const auto& [to, from] : l.moves) { first = std::min({ first, to, from }); last = std::max({ last, to, from }); }
        bool mixed = false;
        for (int k = first; k <= last; ++k) if (access_line(src.line(l.file, k))) mixed = true;
        if (mixed) { l.moves.clear(); continue; }
        for (const auto& [to, from] : l.moves) {
            const std::string before = src.line(l.file, to);
            const std::string after = src.line(l.file, from);
            if (before.empty() || after.empty()) continue;
            edits[l.file][to] = { before, after };
        }
    }
}

std::string field_list(const std::vector<Field>& fields) {
    std::string out;
    for (const Field& f : fields) out += (out.empty() ? "" : ", ") + f.name;
    return out;
}

struct GrowthFit {
    bool ok = false;
    bool grows = false;
    walnut::driver::Growth growth;
    double r2 = 0;
};

GrowthFit fit_growth(std::vector<std::pair<double, double>> pts, double floor) {
    GrowthFit out;
    std::sort(pts.begin(), pts.end());
    if (pts.size() < 4) return out;
    const double lo = pts.front().first;
    const double hi = pts.back().first;
    if (hi < 4 * std::max(lo, 1.0) && hi - lo < 8) return out;
    out.ok = true;
    double top = 0;
    for (const auto& pt : pts) top = std::max(top, pt.second);
    const std::size_t quarter = std::max<std::size_t>(1, pts.size() / 4);
    double first = 0;
    double last = 0;
    for (std::size_t i = 0; i < quarter; ++i) {
        first += pts[i].second;
        last += pts[pts.size() - 1 - i].second;
    }
    first /= static_cast<double>(quarter);
    last /= static_cast<double>(quarter);
    if (top < floor || last < 1.15 * std::max(first, floor * 0.01)) return out;
    struct Model {
        walnut::driver::Growth growth;
        double (*f)(double);
    };
    auto g = [](int power, int logs, bool expo) {
        walnut::driver::Growth r;
        r.power = power;
        r.logs = logs;
        r.exponential = expo;
        return r;
    };
    std::vector<Model> models = {
        { g(0, 1, false), [](double n) { return std::log2(n + 1); } },
        { g(1, 0, false), [](double n) { return n; } },
        { g(1, 1, false), [](double n) { return n * std::log2(n + 1); } },
        { g(2, 0, false), [](double n) { return n * n; } },
        { g(2, 1, false), [](double n) { return n * n * std::log2(n + 1); } },
        { g(3, 0, false), [](double n) { return n * n * n; } },
    };
    if (hi <= 60) models.push_back({ g(0, 0, true), [](double n) { return std::pow(2.0, n); } });
    double mean_y = 0;
    for (const auto& pt : pts) mean_y += pt.second;
    mean_y /= static_cast<double>(pts.size());
    double total = 0;
    for (const auto& pt : pts) total += (pt.second - mean_y) * (pt.second - mean_y);
    if (total <= 0) return out;
    double best = -1;
    for (const Model& m : models) {
        double mx = 0;
        for (const auto& pt : pts) mx += m.f(pt.first);
        mx /= static_cast<double>(pts.size());
        double sxy = 0;
        double sxx = 0;
        for (const auto& pt : pts) {
            const double dx = m.f(pt.first) - mx;
            sxy += dx * (pt.second - mean_y);
            sxx += dx * dx;
        }
        if (sxx <= 0) continue;
        const double slope = sxy / sxx;
        if (slope <= 0) continue;
        const double cut = mean_y - slope * mx;
        double res = 0;
        for (const auto& pt : pts) {
            const double e = pt.second - (cut + slope * m.f(pt.first));
            res += e * e;
        }
        const double r2 = 1 - res / total;
        if (r2 > best + 0.01) {
            best = r2;
            out.growth = m.growth;
        }
    }
    if (best < 0.5) return out;
    out.grows = true;
    out.r2 = best;
    return out;
}

void measure_growth(const Profile& p) {
    g_measured.clear();
    std::map<std::tuple<std::string, std::string, int>, std::vector<const GrowthSeries*>> by_function;
    for (const GrowthSeries& g : p.growth) by_function[{ g.name, g.file, g.line }].push_back(&g);
    for (const auto& [key, series] : by_function) {
        Measured m;
        m.name = std::get<0>(key);
        m.file = std::get<1>(key);
        m.line = std::get<2>(key);
        const GrowthSeries* chosen = nullptr;
        GrowthFit chosen_fit;
        for (const GrowthSeries* g : series) {
            std::vector<std::pair<double, double>> pts;
            for (const GrowthPoint& pt : g->points) pts.push_back({ pt.n, pt.ns });
            const GrowthFit fit = fit_growth(pts, 50);
            if (!fit.ok) continue;
            const bool better = !chosen || (fit.grows && !chosen_fit.grows) || (fit.grows == chosen_fit.grows && fit.r2 > chosen_fit.r2);
            if (better) { chosen = g; chosen_fit = fit; }
        }
        for (const GrowthPoint& pt : series.front()->points) m.calls += pt.calls;
        if (!chosen) {
            m.note = series.size() == 1 ? "'" + series.front()->param + "' didn't take enough different sizes to measure; run it with a wider range of inputs"
                                        : "none of its arguments took enough different sizes to measure; run it with a wider range of inputs";
            g_measured.push_back(std::move(m));
            continue;
        }
        m.ok = true;
        m.param = chosen->param;
        m.time = chosen_fit.growth;
        m.low = 1e300;
        for (const GrowthPoint& pt : chosen->points) { m.low = std::min(m.low, pt.n); m.high = std::max(m.high, pt.n); }
        m.sizes = chosen->points.size();
        std::vector<std::pair<double, double>> heap;
        std::vector<std::pair<double, double>> depth;
        for (const GrowthPoint& pt : chosen->points) { heap.push_back({ pt.n, pt.heap }); depth.push_back({ pt.n, pt.depth }); }
        const GrowthFit h = fit_growth(heap, 256);
        const GrowthFit d = fit_growth(depth, 3);
        if (h.grows) m.space = h.growth;
        if (d.grows) m.space = m.space.larger(d.growth);
        g_measured.push_back(std::move(m));
    }
}

const Measured* measured_of(const std::string& name, const std::string& file, int line) {
    for (const Measured& m : g_measured) if (m.name == name && m.file == file && m.line == line) return &m;
    return nullptr;
}

std::string size_range(const Measured& m) {
    std::ostringstream s;
    s << std::setprecision(6) << "grows with '" << m.param << "', measured over " << m.sizes << " sizes from " << m.low << " to " << m.high;
    return s.str();
}

std::vector<const walnut::driver::ComplexityEstimate*> chosen_complexity(const Options& o) {
    std::vector<const walnut::driver::ComplexityEstimate*> out;
    for (const walnut::driver::ComplexityEstimate& e : g_perf.complexity) {
        const std::string file = norm(e.file);
        if (!o.profile_files.empty()) {
            bool keep = false;
            for (const std::string& q : o.profile_files) if (file_matches(file, q)) keep = true;
            if (!keep) continue;
        }
        if (!o.profile_functions.empty()) {
            bool keep = false;
            for (const std::string& q : o.profile_functions) if (function_matches(e.name, q)) keep = true;
            if (!keep) continue;
        }
        out.push_back(&e);
    }
    return out;
}

const walnut::driver::ComplexityEstimate* complexity_of(const std::string& name, const std::string& file, int line) {
    for (const walnut::driver::ComplexityEstimate& e : g_perf.complexity) if (e.name == name && norm(e.file) == file && static_cast<int>(e.first) == line) return &e;
    return nullptr;
}

std::string first_reason(const std::vector<walnut::driver::ComplexityReason>& why) {
    return why.empty() ? std::string() : why.front().text;
}

void complexity_rows(std::ostream& os, const std::vector<const walnut::driver::ComplexityEstimate*>& rows, bool reasons) {
    std::size_t width = 8;
    for (const auto* e : rows) width = std::max(width, e->name.size());
    const bool measured = !g_measured.empty();
    if (measured) {
        os << "  " << pad_right("", width) << "  " << pad_right("time", 27) << "  " << pad_right("memory", 27) << "\n";
        os << "  " << pad_right("function", width) << "  " << pad_right("estimated", 13) << "  " << pad_right("measured", 12) << "  " << pad_right("estimated", 13) << "  " << pad_right("measured", 12) << "  where\n";
    } else {
        os << "  " << pad_right("function", width) << "  " << pad_right("time", 13) << "  " << pad_right("memory", 13) << "  where\n";
    }
    for (const auto* e : rows) {
        const Measured* m = measured ? measured_of(e->name, norm(e->file), static_cast<int>(e->first)) : nullptr;
        os << "  " << pad_right(e->name, width) << "  " << pad_right(e->time.text(), 13) << "  ";
        if (measured) os << pad_right(m && m->ok ? m->time.text() : std::string("-"), 12) << "  ";
        os << pad_right(e->space.text(), 13) << "  ";
        if (measured) os << pad_right(m && m->ok ? m->space.text() : std::string("-"), 12) << "  ";
        os << short_name(norm(e->file)) << ":" << e->first << "\n";
        if (m && reasons) os << "  " << std::string(width, ' ') << "    measured: " << (m->ok ? size_range(*m) : m->note) << "\n";
        else if (measured && reasons) os << "  " << std::string(width, ' ') << "    measured: it has no arguments with a size (numbers, lists or strings) to measure it against\n";
        if (m && m->ok && reasons && (m->time.below(e->time) || e->time.below(m->time))) os << "  " << std::string(width, ' ') << "    the measured time " << (m->time.below(e->time) ? "grows more slowly" : "grows faster") << " than the estimate; trust the measurement for the sizes it ran with\n";
        if (!reasons) continue;
        for (const walnut::driver::ComplexityReason& r : e->time_reasons) os << "  " << std::string(width, ' ') << "    time: " << r.text << "\n";
        for (const walnut::driver::ComplexityReason& r : e->space_reasons) os << "  " << std::string(width, ' ') << "    memory: " << r.text << "\n";
    }
}

void memory_text(std::ostream& os, const Profile& p, const Options& o, Sources& src) {
    if (!p.memory) return;
    const std::size_t top = static_cast<std::size_t>(o.profile_top);
    os << "\nMemory: " << bytes_text(p.bytes) << " allocated in " << p.blocks << (p.blocks == 1 ? " block" : " blocks") << "; at most " << bytes_text(p.peak) << " was in use at once";
    if (p.held) os << "; " << bytes_text(p.held) << " was still held when the program ended";
    os << "\n";
    std::vector<const LineMemory*> lines;
    for (const LineMemory& m : p.memory_lines) lines.push_back(&m);
    std::stable_sort(lines.begin(), lines.end(), [](const LineMemory* a, const LineMemory* b) { return a->bytes > b->bytes; });
    if (lines.size() > top) lines.resize(top);
    if (!lines.empty()) {
        os << "\nLines that allocate the most (memory the standard library asks for counts toward the line that called it)\n";
        os << " allocated        %     blocks  per block  most held  held at end  line\n";
        for (const LineMemory* m : lines) {
            os << pad_left(bytes_text(m->bytes), 10) << pad_left(percent(memory_share(p, m->bytes)), 9) << pad_left(std::to_string(m->blocks), 11)
               << pad_left(m->blocks ? bytes_text(m->bytes / m->blocks) : std::string("-"), 11) << pad_left(bytes_text(m->peak), 11) << pad_left(m->held ? bytes_text(m->held) : std::string("-"), 13)
               << "  " << pad_right(short_name(m->file) + ":" + std::to_string(m->line), 16) << "  " << line_trimmed(src.line(m->file, m->line)) << "\n";
        }
    }
    std::vector<const FunctionMemory*> fns;
    for (const FunctionMemory& m : p.memory_functions) if (m.total || m.bytes) fns.push_back(&m);
    std::stable_sort(fns.begin(), fns.end(), [](const FunctionMemory* a, const FunctionMemory* b) { return std::max(a->total, a->bytes) > std::max(b->total, b->bytes); });
    if (fns.size() > top) fns.resize(top);
    if (!fns.empty()) {
        os << "\nFunctions that allocate the most (total counts the functions it calls; own is just its lines)\n";
        os << "     total        %        own     blocks  function\n";
        for (const FunctionMemory* m : fns) {
            const std::uint64_t total = std::max(m->total, m->bytes);
            os << pad_left(bytes_text(total), 10) << pad_left(percent(memory_share(p, total)), 9) << pad_left(bytes_text(m->bytes), 11) << pad_left(std::to_string(m->blocks), 11)
               << "  " << m->name << "  (" << short_name(m->file) << ":" << m->line << ")\n";
        }
    }
    std::vector<const LineMemory*> held;
    for (const LineMemory& m : p.memory_lines) if (m.held >= 1024) held.push_back(&m);
    std::stable_sort(held.begin(), held.end(), [](const LineMemory* a, const LineMemory* b) { return a->held > b->held; });
    if (held.size() > top) held.resize(top);
    if (!held.empty()) {
        os << "\nStill held when the program ended (globals and objects that were never freed; if it keeps growing in a long run, look for something that is added to but never cleared)\n";
        for (const LineMemory* m : held) os << pad_left(bytes_text(m->held), 10) << "  " << pad_right(short_name(m->file) + ":" + std::to_string(m->line), 16) << "  " << line_trimmed(src.line(m->file, m->line)) << "\n";
    }
}

void layout_text(std::ostream& os, const Profile& p) {
    std::vector<const Layout*> smaller;
    for (const Layout& l : p.layouts) if (l.better < l.size) smaller.push_back(&l);
    if (smaller.empty()) return;
    std::stable_sort(smaller.begin(), smaller.end(), [](const Layout* a, const Layout* b) { return a->size - a->better > b->size - b->better; });
    os << "\nClasses that could be smaller (each field is placed after the one before it, and the compiler adds unused bytes so every field starts where its type needs it to)\n";
    for (const Layout* l : smaller) {
        const std::uint64_t saved = l->size - l->better;
        os << "\n  " << l->name << " (" << short_name(l->file) << ":" << l->line << ") takes " << bytes_text(l->size) << " per object, " << bytes_text(l->padding) << " of it unused padding\n";
        os << "    fields now:    " << field_list(l->fields) << "\n";
        os << "    better order:  " << field_list(l->order) << "  ->  " << bytes_text(l->better) << " per object (" << bytes_text(saved) << ", " << percent(100.0 * static_cast<double>(saved) / static_cast<double>(l->size)) << " less";
        os << "; " << bytes_text(saved * 1000000) << " less for a million of them)\n";
        if (l->moves.empty()) os << "    (the fields aren't one per line, or sit in different public and private sections, so this one has to be reordered by hand)\n";
        else os << "    (--suggest-out writes this change and --apply makes it)\n";
    }
    os << "\n  Putting the biggest fields first keeps the small ones packed together at the end. Field values given in the class are still set in the new order, so check none of them uses a field declared after it.\n";
}

std::string json_text(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) { char buf[8]; std::snprintf(buf, sizeof buf, "\\u%04x", c); out += buf; }
                else out += static_cast<char>(c);
        }
    }
    return out + "\"";
}

std::string to_json(const Profile& p, const std::vector<Suggestion>& picked, Sources& src, int status, const Options& o) {
    std::ostringstream os;
    os << "{\"version\":1,\"exit_code\":" << status << ",\"wall_ns\":" << p.wall << ",\"total_ns\":" << p.total << ",\"lines\":[";
    bool first = true;
    for (const LineTime& t : p.lines) {
        if (!first) os << ",";
        first = false;
        const FunctionTime* fn = function_at(p, t.file, t.line);
        os << "{\"file\":" << json_text(t.file) << ",\"line\":" << t.line << ",\"ns\":" << t.ns << ",\"hits\":" << t.hits
           << ",\"percent\":" << share_of(p, t.ns) << ",\"function\":" << json_text(fn ? fn->name : std::string()) << ",\"text\":" << json_text(line_trimmed(src.line(t.file, t.line))) << "}";
    }
    os << "],\"functions\":[";
    first = true;
    for (const FunctionTime& f : p.functions) {
        if (!first) os << ",";
        first = false;
        os << "{\"name\":" << json_text(f.name) << ",\"file\":" << json_text(f.file) << ",\"line\":" << f.line << ",\"last_line\":" << (f.last ? f.last : f.line)
           << ",\"calls\":" << f.calls << ",\"total_ns\":" << f.total << ",\"own_ns\":" << f.own << ",\"percent\":" << share_of(p, f.total) << "}";
    }
    os << "],\"suggestions\":[";
    first = true;
    for (const Suggestion& s : picked) {
        if (!first) os << ",";
        first = false;
        const walnut::driver::PerfHint& h = *s.hint;
        os << "{\"file\":" << json_text(norm(h.file)) << ",\"line\":" << h.line << ",\"function\":" << json_text(h.function) << ",\"kind\":" << json_text(h.kind.substr(0, h.kind.find(':')))
           << ",\"message\":" << json_text(h.message) << ",\"why\":" << json_text(s.reason) << ",\"percent\":" << s.share
           << ",\"before\":" << json_text(h.before) << ",\"after\":" << json_text(h.after) << "}";
    }
    os << "]";
    os << ",\"memory\":{\"tracked\":" << (p.memory ? "true" : "false") << ",\"bytes\":" << p.bytes << ",\"blocks\":" << p.blocks << ",\"peak\":" << p.peak << ",\"held\":" << p.held << "}";
    os << ",\"memory_lines\":[";
    first = true;
    for (const LineMemory& m : p.memory_lines) {
        if (!first) os << ",";
        first = false;
        const FunctionTime* fn = function_at(p, m.file, m.line);
        os << "{\"file\":" << json_text(m.file) << ",\"line\":" << m.line << ",\"bytes\":" << m.bytes << ",\"blocks\":" << m.blocks << ",\"peak\":" << m.peak << ",\"held\":" << m.held
           << ",\"percent\":" << memory_share(p, m.bytes) << ",\"function\":" << json_text(fn ? fn->name : std::string()) << ",\"text\":" << json_text(line_trimmed(src.line(m.file, m.line))) << "}";
    }
    os << "],\"memory_functions\":[";
    first = true;
    for (const FunctionMemory& m : p.memory_functions) {
        if (!first) os << ",";
        first = false;
        os << "{\"name\":" << json_text(m.name) << ",\"file\":" << json_text(m.file) << ",\"line\":" << m.line << ",\"own_bytes\":" << m.bytes << ",\"blocks\":" << m.blocks << ",\"total_bytes\":" << std::max(m.total, m.bytes) << "}";
    }
    os << "],\"layouts\":[";
    first = true;
    for (const Layout& l : p.layouts) {
        if (!first) os << ",";
        first = false;
        os << "{\"name\":" << json_text(l.name) << ",\"file\":" << json_text(l.file) << ",\"line\":" << l.line << ",\"size\":" << l.size << ",\"padding\":" << l.padding << ",\"better_size\":" << l.better << ",\"fields\":[";
        for (std::size_t i = 0; i < l.fields.size(); ++i) os << (i ? "," : "") << "{\"name\":" << json_text(l.fields[i].name) << ",\"size\":" << l.fields[i].size << ",\"align\":" << l.fields[i].align << "}";
        os << "],\"better_order\":[";
        for (std::size_t i = 0; i < l.order.size(); ++i) os << (i ? "," : "") << json_text(l.order[i].name);
        os << "],\"edits\":[";
        bool any = false;
        for (const auto& [to, from] : l.moves) {
            if (any) os << ",";
            any = true;
            os << "{\"line\":" << to << ",\"before\":" << json_text(src.line(l.file, to)) << ",\"after\":" << json_text(src.line(l.file, from)) << "}";
        }
        os << "]}";
    }
    os << "],\"complexity\":" << complexity_json(chosen_complexity(o));
    os << "}\n";
    return os.str();
}

std::string complexity_json(const std::vector<const walnut::driver::ComplexityEstimate*>& rows) {
    std::ostringstream os;
    os << "[";
    bool first = true;
    auto reasons = [&os](const std::vector<walnut::driver::ComplexityReason>& why) {
        os << "[";
        for (std::size_t i = 0; i < why.size(); ++i) os << (i ? "," : "") << "{\"line\":" << why[i].line << ",\"text\":" << json_text(why[i].text) << "}";
        os << "]";
    };
    for (const auto* e : rows) {
        if (!first) os << ",";
        first = false;
        os << "{\"name\":" << json_text(e->name) << ",\"file\":" << json_text(norm(e->file)) << ",\"line\":" << e->first << ",\"last_line\":" << e->last
           << ",\"time\":" << json_text(e->time.text()) << ",\"memory\":" << json_text(e->space.text()) << ",\"recursive\":" << (e->recursive ? "true" : "false") << ",\"time_reasons\":";
        reasons(e->time_reasons);
        os << ",\"memory_reasons\":";
        reasons(e->space_reasons);
        if (const Measured* m = measured_of(e->name, norm(e->file), static_cast<int>(e->first))) {
            os << ",\"measured\":{\"ok\":" << (m->ok ? "true" : "false") << ",\"time\":" << json_text(m->ok ? m->time.text() : std::string()) << ",\"memory\":" << json_text(m->ok ? m->space.text() : std::string())
               << ",\"argument\":" << json_text(m->param) << ",\"low\":" << m->low << ",\"high\":" << m->high << ",\"sizes\":" << m->sizes << ",\"calls\":" << m->calls
               << ",\"note\":" << json_text(m->ok ? size_range(*m) : m->note) << "}";
        }
        os << "}";
    }
    os << "]";
    return os.str();
}

void line_table(std::ostream& os, const Profile& p, std::vector<const LineTime*> rows, Sources& src, bool with_text) {
    os << "      time        %       runs    per run  line\n";
    for (const LineTime* t : rows) {
        os << pad_left(duration(t->ns), 10) << pad_left(percent(share_of(p, t->ns)), 9) << pad_left(std::to_string(t->hits), 11)
           << pad_left(t->hits ? duration(t->ns / t->hits) : std::string("-"), 11) << "  " << pad_right(short_name(t->file) + ":" + std::to_string(t->line), 16);
        if (with_text) os << "  " << line_trimmed(src.line(t->file, t->line));
        os << "\n";
    }
}

void breakdown(std::ostream& os, const Profile& p, Sources& src, const std::string& file, int first, int last) {
    std::map<int, const LineTime*> at;
    for (const LineTime& t : p.lines) if (t.file == file && t.line >= first && t.line <= last) at[t.line] = &t;
    os << "      time        %       runs  line\n";
    for (int l = first; l <= last; ++l) {
        const std::string& text = src.line(file, l);
        auto it = at.find(l);
        if (it != at.end()) os << pad_left(duration(it->second->ns), 10) << pad_left(percent(share_of(p, it->second->ns)), 9) << pad_left(std::to_string(it->second->hits), 11);
        else os << std::string(30, ' ');
        os << pad_left(std::to_string(l), 6) << "  " << text << "\n";
    }
}

std::string report_text(const Profile& p, const std::vector<Suggestion>& picked, const Options& o, Sources& src, int status) {
    std::ostringstream os;
    os << "\nwalnut profile: " << short_name(norm(o.input)) << " spent " << duration(p.total) << " in your code (the run took " << duration(p.wall) << "; the time spent measuring is taken out)";
    if (status != 0) os << "; it exited with code " << status;
    os << "\n";
    if (p.lines.empty()) {
        os << "\nNo lines of your code ran, so there is nothing to show.\n";
        return os.str();
    }

    const std::size_t top = static_cast<std::size_t>(o.profile_top);
    std::vector<const LineTime*> lines;
    for (const LineTime& t : p.lines) lines.push_back(&t);
    std::stable_sort(lines.begin(), lines.end(), [](const LineTime* a, const LineTime* b) { return a->ns > b->ns; });
    if (lines.size() > top) lines.resize(top);
    os << "\nSlowest lines (the time each line takes itself; time in the standard library counts toward the line that called it)\n";
    line_table(os, p, lines, src, true);

    if (!p.functions.empty()) {
        std::vector<const FunctionTime*> fns;
        for (const FunctionTime& f : p.functions) fns.push_back(&f);
        std::stable_sort(fns.begin(), fns.end(), [](const FunctionTime* a, const FunctionTime* b) { return a->total > b->total; });
        if (fns.size() > top) fns.resize(top);
        os << "\nSlowest functions (total counts the functions it calls; own is just its lines)\n";
        os << "     total        %        own       calls   per call  function\n";
        for (const FunctionTime* f : fns) {
            os << pad_left(duration(f->total), 10) << pad_left(percent(share_of(p, f->total)), 9) << pad_left(duration(f->own), 11) << pad_left(std::to_string(f->calls), 12)
               << pad_left(f->calls ? duration(f->total / f->calls) : std::string("-"), 11) << "  " << f->name << "  (" << short_name(f->file) << ":" << f->line << ")\n";
        }
    }

    std::map<std::string, std::uint64_t> per_file;
    for (const LineTime& t : p.lines) per_file[t.file] += t.ns;
    if (per_file.size() > 1) {
        std::vector<std::pair<std::string, std::uint64_t>> files(per_file.begin(), per_file.end());
        std::stable_sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        os << "\nFiles\n      time        %  file\n";
        for (const auto& [file, ns] : files) os << pad_left(duration(ns), 10) << pad_left(percent(share_of(p, ns)), 9) << "  " << short_name(file) << "\n";
    }

    memory_text(os, p, o, src);
    layout_text(os, p);

    {
        std::vector<const FunctionTime*> fns;
        for (const FunctionTime& f : p.functions) fns.push_back(&f);
        std::stable_sort(fns.begin(), fns.end(), [](const FunctionTime* a, const FunctionTime* b) { return a->total > b->total; });
        if (fns.size() > top) fns.resize(top);
        std::vector<const walnut::driver::ComplexityEstimate*> rows;
        for (const FunctionTime* f : fns) if (const auto* e = complexity_of(f->name, f->file, f->line)) rows.push_back(e);
        if (!rows.empty()) {
            os << "\nHow the slowest functions grow with the size of their input, n (" << (g_measured.empty() ? "an estimate from reading the code; 'walnut complexity' explains each one" : "estimated from the code and measured from this run") << ")\n";
            complexity_rows(os, rows, false);
        }
    }

    for (const std::string& q : o.profile_functions) {
        bool found = false;
        for (const FunctionTime& f : p.functions) {
            if (!function_matches(f.name, q)) continue;
            found = true;
            os << "\n" << f.name << " (" << short_name(f.file) << ":" << f.line << "): " << duration(f.total) << " in total, " << duration(f.own) << " in its own lines, " << f.calls << (f.calls == 1 ? " call" : " calls") << "\n";
            breakdown(os, p, src, f.file, f.line, f.last ? f.last : f.line);
        }
        if (!found) os << "\nNo function named '" << q << "' ran.\n";
    }

    for (const std::string& q : o.profile_files) {
        std::set<std::string> files;
        for (const LineTime& t : p.lines) if (file_matches(t.file, q)) files.insert(t.file);
        if (files.empty()) os << "\nNo lines of '" << q << "' ran.\n";
        for (const std::string& file : files) {
            std::uint64_t ns = 0;
            for (const LineTime& t : p.lines) if (t.file == file) ns += t.ns;
            os << "\n" << short_name(file) << ": " << duration(ns) << " (" << percent(share_of(p, ns)) << ")\n";
            breakdown(os, p, src, file, 1, static_cast<int>(src.count(file)));
        }
    }

    if (o.profile_suggest) {
        if (picked.empty()) {
            os << "\nNo suggestions: nothing in the slow parts matches a pattern walnut knows how to speed up.\n";
        } else {
            os << "\nSuggestions (the slowest parts first)\n";
            for (const Suggestion& s : picked) {
                const walnut::driver::PerfHint& h = *s.hint;
                os << "\n  " << short_name(norm(h.file)) << ":" << h.line;
                if (!h.function.empty()) os << " in '" << h.function << "'";
                os << " (" << s.reason << ")\n";
                os << "    " << h.message << "\n";
                if (!h.before.empty() && !h.after.empty()) os << "    - " << line_trimmed(h.before) << "\n    + " << line_trimmed(h.after) << "\n";
            }
        }
    }
    return os.str();
}

bool write_text(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    if (!out) { std::cerr << "walnut: cannot write '" << path << "'\n"; return false; }
    out << text;
    return static_cast<bool>(out);
}

std::string annotated(const Profile& p, Sources& src, const std::string& file) {
    std::map<int, const LineTime*> at;
    for (const LineTime& t : p.lines) if (t.file == file) at[t.line] = &t;
    std::ostringstream os;
    const std::size_t n = src.count(file);
    for (std::size_t l = 1; l <= n; ++l) {
        auto it = at.find(static_cast<int>(l));
        if (it != at.end()) os << pad_left(duration(it->second->ns), 10) << pad_left(percent(share_of(p, it->second->ns)), 9) << pad_left(std::to_string(it->second->hits), 11) << " | ";
        else os << std::string(30, ' ') << " | ";
        os << src.line(file, static_cast<int>(l)) << "\n";
    }
    return os.str();
}

}

int profile_report(const Options& o, int status, const fs::path& data) {
    Profile p;
    if (!read_profile(data, p)) {
        std::cerr << "walnut: the program didn't leave any profile data, so there's nothing to report (it may have crashed or been stopped before it ended)\n";
        return status ? status : 1;
    }

    if (!o.profile_memory) {
        p.memory = false;
        p.memory_lines.clear();
        p.memory_functions.clear();
    }
    measure_growth(p);
    if (o.complexity) {
        std::cout.flush();
        const int result = complexity_report(o);
        return status ? status : result;
    }

    Sources src;
    plan_layouts(p);
    std::map<std::string, std::map<int, std::pair<std::string, std::string>>> layout_changes;
    if (o.profile_suggest) layout_edits(p, src, layout_changes);
    const std::vector<Suggestion> picked = o.profile_suggest ? pick_suggestions(p, o) : std::vector<Suggestion>{};
    const std::string text = report_text(p, picked, o, src, status);
    if (!o.profile_report.empty()) {
        if (!write_text(o.profile_report, text)) return 1;
        std::cerr << "walnut: profile report written to " << o.profile_report << "\n";
    } else {
        std::cout.flush();
        std::cerr << text;
    }

    if (!o.profile_json.empty() && !write_text(o.profile_json, to_json(p, picked, src, status, o))) return 1;

    auto edits = edits_of(picked);
    for (const auto& [file, lines] : layout_changes) for (const auto& [line, change] : lines) if (!edits[file].count(line)) edits[file][line] = change;

    if (!o.profile_suggest_out.empty()) {
        const std::string ext = fs::path(o.profile_suggest_out).extension().string();
        std::string out;
        if (ext == ".diff" || ext == ".patch") {
            out = unified_diff(edits, src);
        } else {
            std::ostringstream os;
            for (const Suggestion& s : picked) {
                const walnut::driver::PerfHint& h = *s.hint;
                os << short_name(norm(h.file)) << ":" << h.line << (h.function.empty() ? std::string() : " in '" + h.function + "'") << " (" << s.reason << ")\n  " << h.message << "\n";
                if (!h.before.empty() && !h.after.empty()) os << "  - " << line_trimmed(h.before) << "\n  + " << line_trimmed(h.after) << "\n";
                os << "\n";
            }
            const std::string diff = unified_diff(edits, src);
            if (!diff.empty()) os << "The changes as a patch:\n\n" << diff;
            out = os.str();
        }
        if (!write_text(o.profile_suggest_out, out)) return 1;
        std::cerr << "walnut: suggestions written to " << o.profile_suggest_out << "\n";
    }

    if (!o.profile_annotate.empty()) {
        const std::string main_file = norm(fs::absolute(o.input).string());
        if (!write_text(o.profile_annotate, annotated(p, src, main_file))) return 1;
        std::cerr << "walnut: annotated copy of " << short_name(main_file) << " written to " << o.profile_annotate << "\n";
    }

    if (o.profile_apply) {
        std::size_t changed = 0;
        for (const auto& [file, lines] : edits) {
            std::ifstream in(file, std::ios::binary);
            std::stringstream buf;
            buf << in.rdbuf();
            const std::string original = buf.str();
            std::vector<std::string> rows;
            std::vector<bool> cr;
            std::size_t at = 0;
            while (at <= original.size()) {
                const std::size_t nl = original.find('\n', at);
                std::string row = original.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
                const bool has_cr = !row.empty() && row.back() == '\r';
                if (has_cr) row.pop_back();
                rows.push_back(row);
                cr.push_back(has_cr);
                if (nl == std::string::npos) break;
                at = nl + 1;
            }
            std::size_t here = 0;
            for (const auto& [line, change] : lines) {
                const std::size_t i = static_cast<std::size_t>(line - 1);
                if (i < rows.size() && rows[i] == change.first && change.first != change.second) { rows[i] = change.second; ++here; }
            }
            if (!here) continue;
            std::string out;
            for (std::size_t i = 0; i < rows.size(); ++i) {
                out += rows[i];
                if (cr[i]) out += '\r';
                if (i + 1 < rows.size()) out += '\n';
            }
            std::error_code ec;
            fs::copy_file(file, file + ".bak", fs::copy_options::overwrite_existing, ec);
            if (ec) { std::cerr << "walnut: couldn't back up '" << file << "', so it wasn't changed: " << ec.message() << "\n"; continue; }
            if (!write_text(file, out)) continue;
            changed += here;
            std::cerr << "walnut: changed " << here << (here == 1 ? " line" : " lines") << " in " << short_name(file) << " (the old version is in " << short_name(file) << ".bak)\n";
        }
        if (!changed) std::cerr << "walnut: none of the suggestions could be applied automatically\n";
    }

    return status;
}

} // namespace walnut_cli

namespace walnut_cli {

int complexity_report(const Options& o) {
    const std::vector<const walnut::driver::ComplexityEstimate*> rows = chosen_complexity(o);
    if (!o.profile_json.empty()) {
        if (!write_text(o.profile_json, "{\"version\":1,\"complexity\":" + complexity_json(rows) + "}\n")) return 1;
    }
    if (rows.empty()) {
        std::cout << (g_perf.complexity.empty() ? "There are no functions in your code to estimate.\n" : "No function matched.\n");
        return 0;
    }
    std::cout << "How each function's time and memory grow with the size of its input, n.\n"
                 "The estimates come from reading the code: loops over a whole list or up to a size count as n,\n"
                 "loops that halve or double count as log n, and calls count as whatever the called function costs.\n";
    if (!g_measured.empty())
        std::cout << "The measured columns come from running the program: every call's time, the most memory it held and how deep\n"
                     "it called itself were timed against the size of each argument (a count for numbers, the size for lists and\n"
                     "strings), and the closest curve won. They only cover the sizes the run used.\n";
    std::cout << "\n";
    complexity_rows(std::cout, rows, true);
    return 0;
}

} // namespace walnut_cli
