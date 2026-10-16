#ifndef WALNUT_GRAPHICS_HPP
#define WALNUT_GRAPHICS_HPP

#include "walnut_runtime.hpp"
#include "walnut_collections.hpp"
#include "fizmo/../../Source/Colors/color.hpp"
#include "fizmo/../../Source/Images/Bitmap/image.hpp"
#include "fizmo/../../Source/Images/Bitmap/functions.hpp"
#include "fizmo/../../Source/Images/Formats/image_formats.hpp"
#include "fizmo/../../Source/Windows/window.hpp"
#include "fizmo/../../Source/Windows/dialogs.hpp"
#include "fizmo/../../Source/Input/input_manager.hpp"
#include "fizmo/../../Source/Input/gamepad.hpp"
#include "fizmo/../../Source/Graphics/model_3d.hpp"
#include "fizmo/../../Source/Graphics/Renderer/renderer.hpp"
#include "fizmo/../../Source/Text/text_style.hpp"
#include "fizmo/../../Source/Text/fonts.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace walnut_rt {
namespace gfx {

namespace fc = fizmo::colors;
namespace fi = fizmo::images;
namespace fw = fizmo::windows;
namespace fg = fizmo::graphics;
namespace ft = fizmo::text;
namespace fn = fizmo::input;
namespace linalg = fizmo::linalg;

using F = walnut_rt::f128;

inline double d(const F& v) { return walnut_rt::to_native<double>(v); }
inline F w(double v) { return walnut_rt::from_native<F>(v); }
inline int i(const F& v) { return static_cast<int>(std::lround(d(v))); }
inline unsigned int u(const F& v) { const double x = d(v); return x <= 0.0 ? 0u : static_cast<unsigned int>(std::lround(x)); }
inline unsigned int u(long long v) { return v <= 0 ? 0u : static_cast<unsigned int>(v); }

inline fc::Color color(long long packed) {
    const auto p = static_cast<std::uint32_t>(packed);
    return fc::Color(static_cast<std::uint8_t>(p >> 24), static_cast<std::uint8_t>(p >> 16), static_cast<std::uint8_t>(p >> 8), static_cast<std::uint8_t>(p));
}

inline long long packed(const fc::Color& c) {
    return static_cast<long long>((static_cast<std::uint32_t>(c.red()) << 24) | (static_cast<std::uint32_t>(c.green()) << 16) | (static_cast<std::uint32_t>(c.blue()) << 8) | static_cast<std::uint32_t>(c.alpha()));
}

inline std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

inline std::string squash(const std::string& s) {
    std::string out;
    for (char c : s) if (c != ' ' && c != '-' && c != '_') out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

class image {
public:
    fi::BitmapImage img;

    image() = default;
    image(long long width, long long height, long long fill) : img(u(width), u(height)) {
        img.fill_area(0, 0, img.width() ? img.width() - 1 : 0, img.height() ? img.height() - 1 : 0, color(fill));
    }
    explicit image(fi::BitmapImage b) : img(std::move(b)) {}

    static std::string& last_error() {
        static thread_local std::string e;
        return e;
    }

    static image load(const std::string& path) {
        image out;
        std::string why;
        if (!fi::load_image(path, out.img, &why)) last_error() = why.empty() ? "could not read the image" : why;
        else last_error().clear();
        return out;
    }

    static std::string load_error() { return last_error(); }

    bool valid() const { return img.width() > 0 && img.height() > 0; }
    long long width() const { return img.width(); }
    long long height() const { return img.height(); }
    bool contains(long long x, long long y) const { return x >= 0 && y >= 0 && x < width() && y < height(); }
    long long get(long long x, long long y) const { return packed(img.get_pixel(static_cast<unsigned int>(x), static_cast<unsigned int>(y))); }
    void set(long long x, long long y, long long c) { img.set_pixel(static_cast<unsigned int>(x), static_cast<unsigned int>(y), color(c)); }

    void fill(long long c) {
        if (valid()) img.fill_area(0, 0, img.width() - 1, img.height() - 1, color(c));
    }

    void fill_rect(long long x, long long y, long long w, long long h, long long c) {
        const long long x0 = std::max<long long>(0, x), y0 = std::max<long long>(0, y);
        const long long x1 = std::min<long long>(width(), x + w), y1 = std::min<long long>(height(), y + h);
        if (x1 <= x0 || y1 <= y0) return;
        img.fill_area(static_cast<unsigned int>(x0), static_cast<unsigned int>(y0), static_cast<unsigned int>(x1 - 1), static_cast<unsigned int>(y1 - 1), color(c));
    }

    std::string save(const std::string& path, long long quality) const {
        std::string why;
        fi::EncodeOptions options;
        options.quality = static_cast<int>(quality);
        if (!fi::save_image(path, img, fi::ImageFormat::Unknown, options, &why)) return why.empty() ? std::string("could not write the image") : why;
        return std::string();
    }

    image resized(long long w, long long h) const { return image(img.as_resized(u(w), u(h))); }

    image cropped(long long x, long long y, long long w, long long h) const {
        const long long x0 = std::max<long long>(0, x), y0 = std::max<long long>(0, y);
        const long long x1 = std::min<long long>(width(), x + w), y1 = std::min<long long>(height(), y + h);
        image out;
        if (x1 <= x0 || y1 <= y0) return out;
        out.img = fi::BitmapImage(static_cast<unsigned int>(x1 - x0), static_cast<unsigned int>(y1 - y0));
        for (long long yy = y0; yy < y1; ++yy)
            for (long long xx = x0; xx < x1; ++xx)
                out.img.set_pixel(static_cast<unsigned int>(xx - x0), static_cast<unsigned int>(yy - y0), img.get_pixel(static_cast<unsigned int>(xx), static_cast<unsigned int>(yy)));
        return out;
    }

    image flipped(bool horizontal) const {
        image out;
        out.img = fi::BitmapImage(img.width(), img.height());
        const unsigned int wd = img.width(), ht = img.height();
        for (unsigned int yy = 0; yy < ht; ++yy)
            for (unsigned int xx = 0; xx < wd; ++xx)
                out.img.set_pixel(horizontal ? wd - 1 - xx : xx, horizontal ? yy : ht - 1 - yy, img.get_pixel(xx, yy));
        return out;
    }

    image rotated(long long quarter_turns) const {
        const long long q = ((quarter_turns % 4) + 4) % 4;
        const unsigned int wd = img.width(), ht = img.height();
        image out;
        out.img = (q % 2 == 0) ? fi::BitmapImage(wd, ht) : fi::BitmapImage(ht, wd);
        for (unsigned int yy = 0; yy < ht; ++yy)
            for (unsigned int xx = 0; xx < wd; ++xx) {
                unsigned int nx = xx, ny = yy;
                if (q == 1) { nx = ht - 1 - yy; ny = xx; }
                else if (q == 2) { nx = wd - 1 - xx; ny = ht - 1 - yy; }
                else if (q == 3) { nx = yy; ny = wd - 1 - xx; }
                out.img.set_pixel(nx, ny, img.get_pixel(xx, yy));
            }
        return out;
    }

    void draw(const image& other, long long x, long long y) {
        const long long ow = other.width(), oh = other.height();
        for (long long yy = 0; yy < oh; ++yy) {
            const long long ty = y + yy;
            if (ty < 0 || ty >= height()) continue;
            for (long long xx = 0; xx < ow; ++xx) {
                const long long tx = x + xx;
                if (tx < 0 || tx >= width()) continue;
                const fc::Color src = other.img.get_pixel(static_cast<unsigned int>(xx), static_cast<unsigned int>(yy));
                const fc::Color dst = img.get_pixel(static_cast<unsigned int>(tx), static_cast<unsigned int>(ty));
                img.set_pixel(static_cast<unsigned int>(tx), static_cast<unsigned int>(ty), dst.blend_over(src));
            }
        }
    }

    image grayscale(const F& amount) const { return image(fi::adjust_grayscale(img, d(amount))); }
    image inverted() const { return image(fi::invert(img)); }
    image sepia(const F& amount) const { return image(fi::apply_sepia(img, d(amount))); }
    image blurred(long long radius) const { return image(fi::blur(img, u(radius))); }
    image brightness(long long amount) const { return image(fi::adjust_brightness(img, static_cast<int>(amount))); }
    image contrast(const F& factor) const { return image(fi::adjust_contrast(img, d(factor))); }
    image saturation(const F& percent) const { return image(fi::adjust_saturation(img, d(percent))); }
    image hue_rotated(const F& degrees) const { return image(fi::rotate_hue(img, d(degrees))); }
    image opacity(const F& percent) const { return image(fi::adjust_opacity(img, d(percent))); }
    image replace_color(long long from, long long to) const { return image(fi::replace_color(img, color(from), color(to))); }
};

inline bool can_read_format(const std::string& extension) { return fi::can_decode(fi::format_from_extension(extension)); }
inline bool can_write_format(const std::string& extension) { return fi::can_encode(fi::format_from_extension(extension)); }

class event {
public:
    fw::WindowEvent e{};

    event() { e.type = fw::WindowEventType::Count; }
    explicit event(const fw::WindowEvent& ev) : e(ev) {}

    long long type() const { return static_cast<long long>(e.type); }
    long long x() const { return static_cast<long long>(e.x); }
    long long y() const { return static_cast<long long>(e.y); }
    long long dx() const { return e.dx; }
    long long dy() const { return e.dy; }
    long long button() const { return static_cast<long long>(e.button); }
    long long key() const { return static_cast<long long>(e.physical_key); }
    long long logical_key() const { return static_cast<long long>(e.logical_key); }
    std::string key_name() const { return e.key_name; }
    long long modifiers() const { return static_cast<long long>(e.mods); }
    bool repeat() const { return e.repeat; }
    std::string text() const { return e.text; }
    F scroll_x() const { return w(e.scroll_x); }
    F scroll_y() const { return w(e.scroll_y); }
    F fx() const { return w(e.fx); }
    F fy() const { return w(e.fy); }
    F pressure() const { return w(e.pressure); }
    long long pointer() const { return static_cast<long long>(e.pointer); }
    long long pointer_id() const { return static_cast<long long>(e.pointer_id); }
    long long window_x() const { return e.window_x; }
    long long window_y() const { return e.window_y; }
    F dpi_scale() const { return w(e.dpi_scale); }
    long long gamepad() const { return e.gamepad; }
    long long gamepad_button() const { return static_cast<long long>(e.gamepad_button); }
    long long gamepad_axis() const { return static_cast<long long>(e.gamepad_axis); }
    F axis_value() const { return w(e.axis_value); }
    long long path_count() const { return static_cast<long long>(e.paths.size()); }
    std::string path(long long index) const { return index >= 0 && index < path_count() ? e.paths[static_cast<std::size_t>(index)] : std::string(); }
};

class renderer;

struct window_state {
    std::unique_ptr<fw::Window> win;
    std::deque<fw::WindowEvent> queue;
    fn::InputManager            input;
    std::unique_ptr<fn::GamepadManager> pads;
    std::vector<std::weak_ptr<void>> renderers;
    std::vector<std::function<void()>> unbinders;
    bool created = false;

    ~window_state() {
        for (auto& f : unbinders) f();
        unbinders.clear();
        if (pads) pads->detach();
        pads.reset();
        input.disconnect();
    }
};

class window {
public:
    std::shared_ptr<window_state> s;

    window() = default;
    window(const std::string& title, long long width, long long height, long long background) : s(std::make_shared<window_state>()) {
        s->win = std::make_unique<fw::Window>(u(width), u(height), title, color(background));
        window_state* raw = s.get();
        for (int t = 0; t < static_cast<int>(fw::WindowEventType::Count); ++t) {
            const auto type = static_cast<fw::WindowEventType>(t);
            if (type == fw::WindowEventType::WindowRenderRequest) continue;
            s->win->add_event_listener(type, [raw](const fw::WindowEvent& e) {
                if (raw->queue.size() < 4096) raw->queue.push_back(e);
            });
        }
        s->input.connect(s->win->event_handler());
        s->created = s->win->create();
        if (s->created) {
            s->pads = std::make_unique<fn::GamepadManager>();
            s->pads->attach(s->win->event_handler());
            s->pads->set_window(s->win->native_handle());
        }
    }

    bool valid() const { return s && s->win && s->created; }
    bool is_open() const { return valid() && s->win->is_open(); }

    bool poll() {
        if (!valid()) return false;
        s->input.end_frame();
        s->win->poll_events();
        if (s->pads) s->pads->update();
        return s->win->is_open();
    }

    bool has_event() const { return s && !s->queue.empty(); }

    event next_event() {
        if (!s || s->queue.empty()) return event();
        event out(s->queue.front());
        s->queue.pop_front();
        return out;
    }

    void clear_events() { if (s) s->queue.clear(); }

    void close() {
        if (!valid()) return;
        for (auto& f : s->unbinders) f();
        s->unbinders.clear();
        s->win->close();
    }

    long long width() const { return valid() ? s->win->width() : 0; }
    long long height() const { return valid() ? s->win->height() : 0; }
    std::string title() const { return valid() ? s->win->title() : std::string(); }
    void set_title(const std::string& t) { if (valid()) s->win->set_title(t); }
    void set_background(long long c) { if (valid()) s->win->set_background_color(color(c)); }
    long long background() const { return valid() ? packed(s->win->background_color()) : 0; }
    bool set_size(long long wd, long long ht) { return valid() && s->win->set_size(u(wd), u(ht)); }
    void set_min_size(long long wd, long long ht) { if (valid()) s->win->set_min_size(u(wd), u(ht)); }
    void set_resizable(bool r) { if (valid()) s->win->set_resizable(r); }
    bool set_position(long long x, long long y) { return valid() && s->win->set_position(static_cast<int>(x), static_cast<int>(y)); }
    long long x() const { int px = 0, py = 0; if (valid()) s->win->position(px, py); return px; }
    long long y() const { int px = 0, py = 0; if (valid()) s->win->position(px, py); return py; }
    void minimize() { if (valid()) s->win->minimize(); }
    void maximize() { if (valid()) s->win->maximize(); }
    void restore() { if (valid()) s->win->restore(); }
    void focus() { if (valid()) s->win->focus(); }
    bool set_mode(long long mode, long long monitor) { return valid() && s->win->set_mode(static_cast<fw::WindowMode>(mode), static_cast<int>(monitor)); }
    long long mode() const { return valid() ? static_cast<long long>(s->win->mode()) : 0; }
    bool set_fullscreen(bool on) { return valid() && s->win->set_fullscreen(on); }
    bool fullscreen() const { return valid() && s->win->fullscreen(); }
    bool set_borderless(bool on) { return valid() && s->win->set_borderless(on); }
    F dpi_scale() const { return w(valid() ? s->win->dpi_scale() : 1.0f); }
    long long monitor_index() const { return valid() ? s->win->monitor_index() : 0; }
    bool set_cursor(long long cursor) { return valid() && s->win->set_cursor(static_cast<fw::SystemCursor>(cursor)); }
    bool set_cursor_image(const image& img, long long hx, long long hy) { return valid() && s->win->set_cursor(img.img, static_cast<int>(hx), static_cast<int>(hy)); }
    void set_cursor_visible(bool v) { if (valid()) s->win->set_cursor_visible(v); }
    bool cursor_visible() const { return !valid() || s->win->cursor_visible(); }
    bool set_cursor_locked(bool v) { return valid() && s->win->set_cursor_locked(v); }
    bool cursor_locked() const { return valid() && s->win->cursor_locked(); }
    bool set_relative_mouse(bool v) { return valid() && s->win->set_relative_mouse(v); }
    std::string clipboard() { return valid() ? s->win->clipboard_text() : std::string(); }
    bool set_clipboard(const std::string& t) { return valid() && s->win->set_clipboard_text(t); }
    void set_drop_enabled(bool on) { if (valid()) s->win->set_drop_enabled(on); }
    void start_text_input() { if (valid()) s->win->start_text_input(); }
    void stop_text_input() { if (valid()) s->win->stop_text_input(); }
    bool text_input_active() const { return valid() && s->win->text_input_active(); }

    bool key_down(long long k) const { return s && s->input.is_key_down(static_cast<fn::Key>(k)); }
    bool key_pressed(long long k) const { return s && s->input.is_key_just_pressed(static_cast<fn::Key>(k)); }
    bool key_released(long long k) const { return s && s->input.is_key_just_released(static_cast<fn::Key>(k)); }
    bool key_double_pressed(long long k) const { return s && s->input.is_key_double_pressed(static_cast<fn::Key>(k)); }
    F key_held_for(long long k) const { return w(s ? s->input.key_hold_duration(static_cast<fn::Key>(k)) : 0.0); }
    bool logical_key_down(long long k) const { return s && s->input.is_logical_key_down(static_cast<fn::Key>(k)); }
    bool logical_key_pressed(long long k) const { return s && s->input.is_logical_key_just_pressed(static_cast<fn::Key>(k)); }
    long long modifiers() const { return s ? static_cast<long long>(s->input.modifiers()) : 0; }
    std::string typed_text() const { return s ? s->input.text_input() : std::string(); }
    long long mouse_x() const { return s ? s->input.mouse_x() : 0; }
    long long mouse_y() const { return s ? s->input.mouse_y() : 0; }
    long long mouse_dx() const { return s ? s->input.mouse_delta_x() : 0; }
    long long mouse_dy() const { return s ? s->input.mouse_delta_y() : 0; }
    bool mouse_down(long long b) const { return s && s->input.is_mouse_button_down(static_cast<fn::MouseButton>(b)); }
    bool mouse_pressed(long long b) const { return s && s->input.is_mouse_button_just_pressed(static_cast<fn::MouseButton>(b)); }
    bool mouse_released(long long b) const { return s && s->input.is_mouse_button_just_released(static_cast<fn::MouseButton>(b)); }
    bool mouse_double_clicked(long long b) const { return s && s->input.is_mouse_double_clicked(static_cast<fn::MouseButton>(b)); }
    F scroll_x() const { return w(s ? s->input.scroll_x() : 0.0f); }
    F scroll_y() const { return w(s ? s->input.scroll_y() : 0.0f); }
};

struct input_api {};
struct windows_api {};
struct typography_api {};

inline std::string key_name(long long k) { return fn::key_name(static_cast<fn::Key>(k)); }
inline long long key_from_name(const std::string& n) { return static_cast<long long>(fn::key_from_name(n)); }

class monitor {
public:
    fw::MonitorInfo m;
    monitor() = default;
    explicit monitor(fw::MonitorInfo info) : m(std::move(info)) {}
    std::string name() const { return m.name; }
    long long x() const { return m.bounds.x; }
    long long y() const { return m.bounds.y; }
    long long width() const { return m.bounds.width; }
    long long height() const { return m.bounds.height; }
    long long work_x() const { return m.work_area.x; }
    long long work_y() const { return m.work_area.y; }
    long long work_width() const { return m.work_area.width; }
    long long work_height() const { return m.work_area.height; }
    F refresh_rate() const { return w(m.current.refresh_rate); }
    F scale() const { return w(m.scale); }
    F dpi() const { return w(m.dpi); }
    bool primary() const { return m.primary; }
};

inline long long monitor_count() { return static_cast<long long>(fw::monitors().size()); }
inline monitor monitor_at(long long index) {
    const auto all = fw::monitors();
    if (index < 0 || index >= static_cast<long long>(all.size())) return monitor();
    return monitor(all[static_cast<std::size_t>(index)]);
}
inline monitor primary_monitor() { return monitor(fw::primary_monitor()); }

inline fw::dialogs::FileDialogOptions dialog_options(const std::string& title, const std::string& start, const walnut_rt::coll::vector<std::string>& filter_names, const walnut_rt::coll::vector<std::string>& filter_patterns) {
    fw::dialogs::FileDialogOptions o;
    o.title = title;
    o.default_path = start;
    const std::size_t n = std::min(filter_names.size(), filter_patterns.size());
    for (std::size_t k = 0; k < n; ++k) {
        fw::dialogs::FileFilter f;
        f.name = filter_names[k];
        std::string cur;
        for (char c : filter_patterns[k]) {
            if (c == ';' || c == ',' || c == ' ') { if (!cur.empty()) f.patterns.push_back(cur); cur.clear(); }
            else cur.push_back(c);
        }
        if (!cur.empty()) f.patterns.push_back(cur);
        o.filters.push_back(f);
    }
    return o;
}

inline bool dialogs_available() { return fw::dialogs::file_dialogs_available(); }

inline std::string open_file(const std::string& title, const std::string& start, const walnut_rt::coll::vector<std::string>& names, const walnut_rt::coll::vector<std::string>& patterns) {
    auto r = fw::dialogs::open_file(dialog_options(title, start, names, patterns));
    return r ? *r : std::string();
}

inline walnut_rt::coll::vector<std::string> open_files(const std::string& title, const std::string& start, const walnut_rt::coll::vector<std::string>& names, const walnut_rt::coll::vector<std::string>& patterns) {
    auto r = fw::dialogs::open_files(dialog_options(title, start, names, patterns));
    return walnut_rt::coll::vector<std::string>(r.begin(), r.end());
}

inline std::string save_file(const std::string& title, const std::string& start, const std::string& name, const walnut_rt::coll::vector<std::string>& names, const walnut_rt::coll::vector<std::string>& patterns) {
    auto o = dialog_options(title, start, names, patterns);
    o.default_name = name;
    auto r = fw::dialogs::save_file(o);
    return r ? *r : std::string();
}

inline std::string select_folder(const std::string& title, const std::string& start) {
    fw::dialogs::FileDialogOptions o;
    o.title = title;
    o.default_path = start;
    auto r = fw::dialogs::select_folder(o);
    return r ? *r : std::string();
}

inline long long message_box(const std::string& title, const std::string& message, long long buttons, long long icon) {
    return static_cast<long long>(fw::dialogs::message_box(title, message, static_cast<fw::dialogs::MessageButtons>(buttons), static_cast<fw::dialogs::MessageIcon>(icon)));
}

inline ft::Font font_named(const std::string& name) {
    const std::string want = squash(name);
    if (want.empty()) return ft::Font::None;
    for (int k = 1; k < static_cast<int>(ft::Font::Count_); ++k) {
        const auto f = static_cast<ft::Font>(k);
        const std::string n = ft::font_css_name(f);
        if (n.rfind("Invalid", 0) != 0 && squash(n) == want) return f;
    }
    return ft::Font::None;
}

class text_style {
public:
    ft::TextStyle st;

    text_style() = default;
    text_style(const F& size, long long c) : st(d(size), color(c)) {}

    bool set_font(const std::string& name) {
        const ft::Font f = font_named(name);
        if (f == ft::Font::None) return false;
        st.set_font(f);
        return true;
    }
    std::string font() const { return st.has_font() ? std::string(ft::font_css_name(st.font())) : std::string(); }
    void clear_font() { st.clear_font(); }
    void set_size(const F& px) { st.set_size(d(px)); }
    F size() const { return w(st.size()); }
    void set_color(long long c) { st.set_fg_color(color(c)); }
    long long color_value() const { return st.has_fg_color() ? packed(st.fg_color().value()) : packed(fc::Color(0, 0, 0, 255)); }
    void set_background(long long c) { st.set_bg_color(color(c)); }
    void clear_background() { st.clear_bg_color(); }
    void set_weight(long long wt) { st.set_weight(static_cast<ft::FontWeight>(wt)); }
    long long weight() const { return static_cast<long long>(st.weight()); }
    void set_bold(bool b) { st.set_bold(b); }
    bool bold() const { return st.is_bold(); }
    void set_italic(bool b) { st.set_italic(b); }
    bool italic() const { return st.is_italic(); }
    void set_underline(bool b) { st.set_underline(b); }
    bool underline() const { return st.is_underlined(); }
    void set_strikethrough(bool b) { st.set_strikethrough(b); }
    bool strikethrough() const { return st.is_strikethrough(); }
    void set_overline(bool b) { st.set_overline(b); }
    bool overline() const { return st.is_overlined(); }
    void set_letter_spacing(const F& px) { st.set_letter_spacing(d(px)); }
    F letter_spacing() const { return w(st.has_letter_spacing() ? st.letter_spacing() : 0.0); }
    void set_word_spacing(const F& px) { st.set_word_spacing(d(px)); }
    void set_line_height(const F& m) { st.set_line_height(d(m)); }
    F line_height() const { return w(st.has_line_height() ? st.line_height() : 1.0); }
    void set_align(long long a) { st.set_text_align(static_cast<ft::TextAlign>(a)); }
    long long align() const { return static_cast<long long>(st.text_align()); }
    void set_overflow(long long o) { st.set_text_overflow(static_cast<ft::TextOverflow>(o)); }
    void set_transform(long long t) { st.set_transform(static_cast<ft::TextTransform>(t)); }
    void set_opacity(const F& o) { st.set_opacity(static_cast<float>(d(o))); }
    void set_outline(long long c, const F& wd) { st.set_outline_color(color(c)); st.set_outline_width(d(wd)); }
    void clear_outline() { st.clear_outline_color(); st.clear_outline_width(); }
    void set_shadow(const F& ox, const F& oy, const F& blur, long long c) { st.set_shadow(d(ox), d(oy), d(blur), color(c)); }
    void clear_shadow() { st.clear_shadow(); }
    void set_max_lines(long long n) { st.set_max_lines(u(n)); }
    void set_indent(const F& px) { st.set_indent(d(px)); }
};

inline walnut_rt::coll::vector<std::string> font_names() {
    walnut_rt::coll::vector<std::string> out;
    for (int k = 1; k < static_cast<int>(ft::Font::Count_); ++k) {
        const std::string n = ft::font_css_name(static_cast<ft::Font>(k));
        if (!n.empty() && n.rfind("Invalid", 0) != 0) out.push_back(n);
    }
    return out;
}

class gradient {
public:
    std::shared_ptr<fg::Gradient> g = std::make_shared<fg::Gradient>();

    static gradient linear(const F& x0, const F& y0, const F& x1, const F& y1) { gradient out; *out.g = fg::Gradient::linear(d(x0), d(y0), d(x1), d(y1)); return out; }
    static gradient radial(const F& cx, const F& cy, const F& r) { gradient out; *out.g = fg::Gradient::radial(d(cx), d(cy), d(r)); return out; }
    void add_stop(const F& position, long long c) { own(); g->add_stop(d(position), color(c)); }
    void set_spread(long long s) { own(); g->set_spread(static_cast<fg::GradientSpread>(s)); }
    void set_focal(const F& fx, const F& fy) { own(); g->set_focal(d(fx), d(fy)); }
    long long color_at(const F& t) const { return packed(g->color_at(d(t))); }
    long long stop_count() const { return static_cast<long long>(g->stops().size()); }

private:
    void own() { if (g.use_count() > 1) g = std::make_shared<fg::Gradient>(*g); }
};

class paint {
public:
    fg::Paint p;
    std::shared_ptr<fg::Gradient> fill_g, stroke_g;

    paint() = default;

    static paint fill(long long c) { paint out; out.p = fg::Paint::fill(color(c)); return out; }
    static paint stroke(long long c, const F& width) { paint out; out.p = fg::Paint::stroke(color(c), std::max(1u, u(width))); return out; }
    static paint fill_and_stroke(long long f, long long s, const F& width) {
        paint out;
        out.p = fg::Paint::fill(color(f));
        out.p.set_stroke_color(color(s)).set_stroke_width(std::max(1u, u(width))).set_style(fg::PaintStyle::FillAndStroke);
        return out;
    }
    static paint gradient_fill(const gradient& gr) {
        paint out;
        out.fill_g = std::make_shared<fg::Gradient>(*gr.g);
        out.p = fg::Paint::gradient_fill(*out.fill_g);
        out.p.set_fill_gradient(out.fill_g.get());
        return out;
    }

    void set_fill_color(long long c) { p.set_fill_color(color(c)); if (p.style() == fg::PaintStyle::Stroke) p.set_style(fg::PaintStyle::FillAndStroke); }
    void set_stroke(long long c, const F& width) { p.set_stroke_color(color(c)).set_stroke_width(std::max(1u, u(width))); if (p.style() == fg::PaintStyle::Fill) p.set_style(fg::PaintStyle::FillAndStroke); }
    void set_style(long long s) { p.set_style(static_cast<fg::PaintStyle>(s)); }
    void set_line_cap(long long c) { p.set_line_cap(static_cast<fg::LineCap>(c)); }
    void set_line_join(long long j) { p.set_line_join(static_cast<fg::LineJoin>(j)); }
    void set_opacity(const F& o) { p.set_opacity(static_cast<float>(d(o))); }
    void set_origin(long long o) { p.set_origin(static_cast<fizmo::RectOrigin>(o)); }
    void set_angles(const F& start, const F& end) { p.set_angles(d(start), d(end), true); }
    void set_stroke_gradient(const gradient& gr) { stroke_g = std::make_shared<fg::Gradient>(*gr.g); p.set_stroke_gradient(stroke_g.get()); }
    long long fill_color() const { return packed(p.fill_color()); }
    long long stroke_color() const { return packed(p.stroke_color()); }
    F stroke_width() const { return w(static_cast<double>(p.stroke_width())); }
    long long style() const { return static_cast<long long>(p.style()); }

    paint(const paint& o) : p(o.p), fill_g(o.fill_g), stroke_g(o.stroke_g) { relink(); }
    paint& operator=(const paint& o) { p = o.p; fill_g = o.fill_g; stroke_g = o.stroke_g; relink(); return *this; }

private:
    void relink() {
        if (fill_g) p.set_fill_gradient(fill_g.get());
        if (stroke_g) p.set_stroke_gradient(stroke_g.get());
    }
};

class path {
public:
    fg::Path2D pth;
    void move_to(const F& x, const F& y) { pth.move_to(d(x), d(y)); }
    void line_to(const F& x, const F& y) { pth.line_to(d(x), d(y)); }
    void quad_to(const F& cx, const F& cy, const F& x, const F& y) { pth.quad_to(d(cx), d(cy), d(x), d(y)); }
    void cubic_to(const F& c1x, const F& c1y, const F& c2x, const F& c2y, const F& x, const F& y) { pth.cubic_to(d(c1x), d(c1y), d(c2x), d(c2y), d(x), d(y)); }
    void arc_to(const F& cx, const F& cy, const F& rx, const F& ry, const F& start, const F& sweep) { pth.arc_to(d(cx), d(cy), d(rx), d(ry), d(start), d(sweep)); }
    void close() { pth.close(); }
    void clear() { pth.clear(); }
    bool empty() const { return pth.empty(); }
    static path circle(const F& cx, const F& cy, const F& r) { path out; out.pth = fg::Path2D::circle(d(cx), d(cy), d(r)); return out; }
    static path ellipse(const F& cx, const F& cy, const F& rx, const F& ry) { path out; out.pth = fg::Path2D::ellipse(d(cx), d(cy), d(rx), d(ry)); return out; }
    static path rounded_rect(const F& x, const F& y, const F& wd, const F& ht, const F& r) { path out; out.pth = fg::Path2D::rounded_rect(d(x), d(y), d(wd), d(ht), d(r)); return out; }
};

class texture {
public:
    fg::Texture t;
    texture() = default;
    texture(const image& img, long long filter, long long wrap) : t(img.img, static_cast<fg::SampleFilter>(filter), static_cast<fg::WrapMode>(wrap)) {}
    bool valid() const { return t.valid(); }
    long long width() const { return t.width(); }
    long long height() const { return t.height(); }
    void set_filter(long long f) { t.set_filter(static_cast<fg::SampleFilter>(f)); }
    void set_wrap(long long m) { t.set_wrap(static_cast<fg::WrapMode>(m)); }
    long long filter() const { return static_cast<long long>(t.filter()); }
    image to_image() const { return t.image() ? image(*t.image()) : image(); }
};

class sprite {
public:
    fg::Sprite sp;
    sprite() = default;
    explicit sprite(const texture& tx) : sp(tx.t) {}
    F x() const { return w(sp.x()); }
    F y() const { return w(sp.y()); }
    void set_position(const F& x, const F& y) { sp.set_position(d(x), d(y)); }
    void move(const F& dx, const F& dy) { sp.translate(d(dx), d(dy)); }
    void move_forward(const F& forward, const F& sideways) { sp.move_local(d(forward), d(sideways)); }
    F rotation() const { return w(sp.rotation()); }
    void set_rotation(const F& deg) { sp.set_rotation(d(deg)); }
    void rotate(const F& deg) { sp.rotate(d(deg)); }
    void look_at(const F& x, const F& y, const F& offset) { sp.look_at(d(x), d(y), d(offset)); }
    F scale_x() const { return w(sp.scale_x()); }
    F scale_y() const { return w(sp.scale_y()); }
    void set_scale(const F& sx, const F& sy) { sp.set_scale(d(sx), d(sy)); }
    void set_size(const F& wd, const F& ht) { sp.set_size(d(wd), d(ht)); }
    void fit_size(const F& wd, const F& ht) { sp.fit_size(d(wd), d(ht)); }
    F width() const { return w(sp.display_width()); }
    F height() const { return w(sp.display_height()); }
    void set_flip(bool h, bool v) { sp.set_flip_h(h); sp.set_flip_v(v); }
    bool flipped_h() const { return sp.flipped_h(); }
    bool flipped_v() const { return sp.flipped_v(); }
    void set_origin(long long o) { sp.set_origin(static_cast<fizmo::RectOrigin>(o)); }
    void set_pivot(const F& nx, const F& ny) { sp.set_pivot(d(nx), d(ny)); }
    void set_opacity(const F& o) { sp.set_opacity(static_cast<float>(d(o))); }
    F opacity() const { return w(sp.opacity()); }
    void set_visible(bool v) { sp.set_visible(v); }
    bool visible() const { return sp.visible(); }
    void set_z_order(long long z) { sp.set_z_order(static_cast<int>(z)); }
    long long z_order() const { return sp.z_order(); }
    void set_tint(long long c) { sp.set_tint(color(c)); }
    void clear_tint() { sp.clear_tint(); }
    long long tint() const { return packed(sp.tint()); }
    void set_blend_mode(long long m) { sp.set_blend_mode(static_cast<fg::BlendMode>(m)); }
    void set_texture(const texture& tx) { sp.set_texture(tx.t); }
    void set_source_rect(long long sx, long long sy, long long sw, long long sh) { sp.set_source_rect(static_cast<int>(sx), static_cast<int>(sy), u(sw), u(sh)); }
    void reset_source_rect() { sp.reset_source_rect(); }
    bool set_frame(long long index, long long fw_, long long fh, long long columns) { return sp.set_frame(u(index), u(fw_), u(fh), u(columns)); }
    bool contains(const F& px, const F& py) const { return sp.contains(d(px), d(py)); }
    bool overlaps(const sprite& o) const { return sp.bounds().intersects(o.sp.bounds()); }
    F bounds_x() const { return w(sp.bounds().x); }
    F bounds_y() const { return w(sp.bounds().y); }
    F bounds_width() const { return w(sp.bounds().w); }
    F bounds_height() const { return w(sp.bounds().h); }
};

class render_target {
public:
    fg::RenderTarget rt;
    render_target() = default;
    render_target(long long wd, long long ht, long long filter) : rt(u(wd), u(ht), static_cast<fg::SampleFilter>(filter)) {}
    bool valid() const { return rt.valid(); }
    long long width() const { return rt.width(); }
    long long height() const { return rt.height(); }
    void resize(long long wd, long long ht) { rt.resize(u(wd), u(ht)); }
};

struct renderer_state {
    std::shared_ptr<window_state> win;
    std::unique_ptr<fg::Renderer> r;
    bool bound = false;
    bool dead = false;

    void unbind() {
        if (r && bound) r->unbind();
        bound = false;
        dead = true;
    }
};

class renderer {
public:
    std::shared_ptr<renderer_state> s;

    renderer() = default;
    renderer(const window& win, long long backend, bool vsync) : s(std::make_shared<renderer_state>()) {
        if (!win.valid()) return;
        s->win = win.s;
        s->r = std::make_unique<fg::Renderer>(*win.s->win, static_cast<fg::RendererBackend>(backend));
        s->r->set_vsync(vsync);
        s->bound = s->r->bind();
        std::weak_ptr<renderer_state> weak = s;
        win.s->unbinders.push_back([weak] { if (auto st = weak.lock()) st->unbind(); });
    }

    bool valid() const { return s && s->r && s->bound && !s->dead && s->win && s->win->win && s->win->win->is_open(); }
    fg::Renderer& R() const { return *s->r; }

    std::string backend_name() const { return s && s->r && s->bound ? std::string(s->r->backend_name()) : std::string(); }
    bool is_gpu() const { return valid() && R().is_gpu(); }
    void set_vsync(bool on) { if (valid()) R().set_vsync(on); }
    bool vsync() const { return valid() && R().vsync(); }
    long long width() const { return valid() ? R().viewport_width() : 0; }
    long long height() const { return valid() ? R().viewport_height() : 0; }
    F frame_ms() const { return w(valid() ? R().frame_stats().interval_ms : 0.0); }

    void begin_frame() { if (valid()) R().begin_frame(); }
    void present() { if (valid()) R().present(); }
    void clear(long long c) { if (valid()) R().clear(color(c)); }

    image capture() {
        image out;
        if (valid()) R().capture(out.img);
        return out;
    }

    void push() { if (valid()) R().push_transform(); }
    void pop() { if (valid()) R().pop_transform(); }
    void reset_transform() { if (valid()) R().reset_transform(); }
    void translate(const F& dx, const F& dy) { if (valid()) R().translate(d(dx), d(dy)); }
    void rotate(const F& deg) { if (valid()) R().rotate(d(deg), true); }
    void scale(const F& sx, const F& sy) { if (valid()) R().scale(d(sx), d(sy)); }

    void set_clip(const F& x, const F& y, const F& wd, const F& ht) { if (valid()) R().set_clip_rect(i(x), i(y), u(wd), u(ht)); }
    void reset_clip() { if (valid()) R().reset_clip_rect(); }
    void set_blend_mode(long long m) { if (valid()) R().set_blend_mode(static_cast<fg::BlendMode>(m)); }
    long long blend_mode() const { return valid() ? static_cast<long long>(R().blend_mode()) : 0; }

    void begin_camera(const F& x, const F& y, const F& zoom, const F& rotation) {
        if (!valid()) return;
        fg::Camera2D cam(d(x), d(y), R().viewport_width(), R().viewport_height());
        cam.set_zoom(d(zoom));
        cam.set_rotation(d(rotation));
        R().begin_2d(cam);
    }
    void end_camera() { if (valid()) R().end_2d(); }

    void pixel(const F& x, const F& y, long long c) { if (valid()) R().draw_pixel(i(x), i(y), color(c)); }
    void line(const F& x1, const F& y1, const F& x2, const F& y2, const paint& p) { if (valid()) R().draw_line(i(x1), i(y1), i(x2), i(y2), p.p); }
    void rect(const F& x, const F& y, const F& wd, const F& ht, const paint& p) { if (valid()) R().draw_rect(i(x), i(y), u(wd), u(ht), p.p); }
    void rounded_rect(const F& x, const F& y, const F& wd, const F& ht, const F& r, const paint& p) { if (valid()) R().draw_rounded_rect(d(x), d(y), d(wd), d(ht), d(r), p.p); }
    void circle(const F& cx, const F& cy, const F& r, const paint& p) { if (valid()) R().draw_circle(i(cx), i(cy), u(r), p.p); }
    void ellipse(const F& cx, const F& cy, const F& rx, const F& ry, const paint& p) { if (valid()) R().draw_ellipse(i(cx), i(cy), u(rx), u(ry), p.p); }
    void arc(const F& cx, const F& cy, const F& rx, const F& ry, const paint& p) { if (valid()) R().draw_arc(i(cx), i(cy), u(rx), u(ry), p.p); }
    void triangle(const F& x1, const F& y1, const F& x2, const F& y2, const F& x3, const F& y3, const paint& p) { if (valid()) R().draw_triangle(i(x1), i(y1), i(x2), i(y2), i(x3), i(y3), p.p); }

    static std::vector<fg::RenderPoint> points(const walnut_rt::coll::vector<F>& xs, const walnut_rt::coll::vector<F>& ys) {
        std::vector<fg::RenderPoint> pts;
        const std::size_t n = std::min(xs.size(), ys.size());
        pts.reserve(n);
        for (std::size_t k = 0; k < n; ++k) pts.emplace_back(d(xs[k]), d(ys[k]));
        return pts;
    }

    void polygon(const walnut_rt::coll::vector<F>& xs, const walnut_rt::coll::vector<F>& ys, const paint& p) { if (valid()) { auto pts = points(xs, ys); R().draw_polygon(pts, p.p); } }
    void polyline(const walnut_rt::coll::vector<F>& xs, const walnut_rt::coll::vector<F>& ys, const paint& p, bool closed) { if (valid()) { auto pts = points(xs, ys); R().draw_polyline(pts, p.p, closed); } }
    void draw_path(const path& pth, const paint& p) { if (valid()) R().draw_path(pth.pth, p.p); }

    void draw_image(const image& img, const F& x, const F& y) { if (valid()) R().draw_image(img.img, i(x), i(y)); }
    void draw_image_sized(const image& img, const F& x, const F& y, const F& wd, const F& ht) { if (valid()) R().draw_image(img.img, i(x), i(y), u(wd), u(ht)); }
    void draw_texture(const texture& t, const F& x, const F& y, const F& opacity) { if (valid()) R().draw_texture(t.t, i(x), i(y), static_cast<float>(d(opacity))); }
    void draw_texture_sized(const texture& t, const F& x, const F& y, const F& wd, const F& ht, const F& opacity) { if (valid()) R().draw_texture(t.t, i(x), i(y), u(wd), u(ht), static_cast<float>(d(opacity))); }
    void draw_texture_part(const texture& t, const F& x, const F& y, const F& wd, const F& ht, long long sx, long long sy, long long sw, long long sh, long long tint, const F& opacity) {
        if (valid()) R().draw_texture(t.t, i(x), i(y), u(wd), u(ht), fg::TextureRect(static_cast<int>(sx), static_cast<int>(sy), u(sw), u(sh)), color(tint), static_cast<float>(d(opacity)));
    }
    void draw_sprite(const sprite& sp) { if (valid()) R().draw_sprite(sp.sp); }

    void text(const F& x, const F& y, const std::string& str, const text_style& st) { if (valid()) R().draw_text(i(x), i(y), str, st.st); }
    void text_box(const F& x, const F& y, const F& wd, const F& ht, const std::string& str, const text_style& st) { if (valid()) R().draw_text(i(x), i(y), u(wd), u(ht), str, st.st); }
    long long measure(const std::string& str, const text_style& st, long long max_width, long long which) {
        if (!valid()) return 0;
        const ft::TextMetrics m = max_width > 0 ? R().measure_text(str, st.st, u(max_width)) : R().measure_text(str, st.st);
        switch (which) {
            case 0: return m.width;
            case 1: return m.height;
            case 2: return m.ascent;
            default: return m.descent;
        }
    }
    bool load_font_file(const std::string& file) { return valid() && R().load_font_file(file); }

    bool begin_target(const render_target& t, long long c, bool clear) { return valid() && (clear ? R().begin_target(t.rt, color(c)) : R().begin_target(t.rt)); }
    void end_target() { if (valid()) R().end_target(); }
    void draw_target(const render_target& t, const F& x, const F& y, const F& wd, const F& ht, const F& opacity) { if (valid()) R().draw_target(t.rt, i(x), i(y), u(wd), u(ht), static_cast<float>(d(opacity))); }
    image read_target(const render_target& t) { image out; if (valid()) R().read_target(t.rt, out.img); return out; }
};

class gamepad {
public:
    std::shared_ptr<window_state> s;
    long long index = -1;

    gamepad() = default;
    gamepad(const window& w, long long slot) : s(w.s), index(slot) {}

    fn::Gamepad* g() const {
        if (!s || !s->pads || index < 0) return nullptr;
        return s->pads->get(static_cast<int>(index));
    }

    long long slot() const { return index; }
    bool connected() const { auto* p = g(); return p && p->connected(); }
    bool mapped() const { auto* p = g(); return p && p->mapped(); }
    std::string name() const { auto* p = g(); return p ? p->name() : std::string(); }
    std::string guid() const { auto* p = g(); return p ? p->guid() : std::string(); }
    bool button(long long b) const { auto* p = g(); return p && p->button(static_cast<fn::GamepadButton>(b)); }
    bool pressed(long long b) const { auto* p = g(); return p && p->just_pressed(static_cast<fn::GamepadButton>(b)); }
    bool released(long long b) const { auto* p = g(); return p && p->just_released(static_cast<fn::GamepadButton>(b)); }
    F axis(long long a) const { auto* p = g(); return w(p ? p->axis(static_cast<fn::GamepadAxis>(a)) : 0.0f); }
    F raw_axis(long long a) const { auto* p = g(); return w(p ? p->raw_axis(static_cast<fn::GamepadAxis>(a)) : 0.0f); }
    void set_deadzones(const F& stick, const F& trigger) { auto* p = g(); if (p) p->set_deadzones(static_cast<float>(d(stick)), static_cast<float>(d(trigger))); }
    bool has_rumble() const { auto* p = g(); return p && p->has_rumble(); }
    bool rumble(const F& low, const F& high, long long ms) { auto* p = g(); return p && p->rumble(static_cast<float>(d(low)), static_cast<float>(d(high)), u(ms)); }
    bool stop_rumble() { auto* p = g(); return p && p->stop_rumble(); }
};

inline long long gamepad_count(const window& w) { return w.s && w.s->pads ? static_cast<long long>(w.s->pads->count()) : 0; }
inline long long gamepad_slots(const window& w) { return w.s && w.s->pads ? static_cast<long long>(w.s->pads->slots()) : 0; }
inline gamepad gamepad_at(const window& w, long long slot) { return gamepad(w, slot); }
inline long long primary_gamepad(const window& w) {
    if (!w.s || !w.s->pads) return -1;
    fn::Gamepad* p = w.s->pads->primary();
    return p ? p->slot() : -1;
}
inline bool any_gamepad_button(const window& w, long long b) { return w.s && w.s->pads && w.s->pads->any_button(static_cast<fn::GamepadButton>(b)); }
inline long long add_gamepad_mappings(const window& w, const std::string& text) { return w.s && w.s->pads ? static_cast<long long>(w.s->pads->add_mappings(text)) : 0; }
inline long long load_gamepad_mappings(const window& w, const std::string& path) { return w.s && w.s->pads ? static_cast<long long>(w.s->pads->load_mappings(path)) : 0; }

inline linalg::vector3d v3(const F& x, const F& y, const F& z) { return linalg::vector3d{ d(x), d(y), d(z) }; }

inline linalg::Matrix4d transform_matrix(const F& px, const F& py, const F& pz, const F& rx, const F& ry, const F& rz, const F& sx, const F& sy, const F& sz) {
    const double kRad = 3.14159265358979323846 / 180.0;
    const double ax = d(rx) * kRad, ay = d(ry) * kRad, az = d(rz) * kRad;
    const double cx = std::cos(ax), snx = std::sin(ax), cy = std::cos(ay), sny = std::sin(ay), cz = std::cos(az), snz = std::sin(az);
    const double r00 = cy * cz + sny * snx * snz, r01 = -cy * snz + sny * snx * cz, r02 = sny * cx;
    const double r10 = cx * snz, r11 = cx * cz, r12 = -snx;
    const double r20 = -sny * cz + cy * snx * snz, r21 = sny * snz + cy * snx * cz, r22 = cy * cx;
    const double kx = d(sx), ky = d(sy), kz = d(sz);
    linalg::Matrix4d m = linalg::Matrix4d::identity();
    m.data[0] = r00 * kx; m.data[1] = r01 * ky; m.data[2] = r02 * kz; m.data[3] = d(px);
    m.data[4] = r10 * kx; m.data[5] = r11 * ky; m.data[6] = r12 * kz; m.data[7] = d(py);
    m.data[8] = r20 * kx; m.data[9] = r21 * ky; m.data[10] = r22 * kz; m.data[11] = d(pz);
    m.data[12] = 0.0; m.data[13] = 0.0; m.data[14] = 0.0; m.data[15] = 1.0;
    return m;
}

class camera3d {
public:
    fg::Camera3D c;

    camera3d() : c(linalg::vector3d{ 0.0, 0.0, 0.0 }, 640, 480) {}
    camera3d(const F& x, const F& y, const F& z, const F& fov) : c(v3(x, y, z), 640, 480, d(fov)) {}

    void set_viewport(long long wd, long long ht) { if (wd > 0 && ht > 0) c.set_viewport_size(u(wd), u(ht)); }
    void set_position(const F& x, const F& y, const F& z) { c.set_position(d(x), d(y), d(z)); }
    F x() const { return w(c.position().x); }
    F y() const { return w(c.position().y); }
    F z() const { return w(c.position().z); }
    void move(const F& dx, const F& dy, const F& dz) { c.translate(v3(dx, dy, dz)); }
    void move_local(const F& right, const F& up, const F& forward) { c.move_local(d(right), d(up), d(forward)); }
    void move_flat(const F& forward, const F& right, const F& up) { c.move_flat(d(forward), d(right), d(up)); }
    void set_rotation(const F& yaw, const F& pitch, const F& roll) { c.set_rotation(d(yaw), d(pitch), d(roll)); }
    F yaw() const { return w(c.yaw()); }
    F pitch() const { return w(c.pitch()); }
    F roll() const { return w(c.roll()); }
    void turn(const F& dyaw, const F& dpitch) { c.add_yaw_pitch(d(dyaw), d(dpitch)); }
    void set_pitch_limit(const F& deg) { c.set_pitch_limit(d(deg)); }
    void look_at(const F& x, const F& y, const F& z) { c.look_at(v3(x, y, z)); }
    F axis(long long which, long long comp) const {
        const linalg::vector3d& v = which == 0 ? c.forward() : (which == 1 ? c.right() : c.up());
        return w(comp == 0 ? v.x : (comp == 1 ? v.y : v.z));
    }
    void set_perspective(const F& fov, const F& near_plane, const F& far_plane) { c.set_perspective(d(fov), d(near_plane), d(far_plane)); }
    void set_orthographic(const F& height, const F& near_plane, const F& far_plane) { c.set_orthographic(d(height), d(near_plane), d(far_plane)); }
    F fov() const { return w(c.fov_y()); }
    void set_fov(const F& deg) { c.set_fov_y(d(deg)); }
    F near_plane() const { return w(c.near_plane()); }
    F far_plane() const { return w(c.far_plane()); }
    F project(const F& x, const F& y, const F& z, long long which) const {
        const fg::ScreenPoint3D p = c.world_to_screen(d(x), d(y), d(z));
        switch (which) {
            case 0: return w(p.x);
            case 1: return w(p.y);
            case 2: return w(p.depth);
            case 3: return w(p.in_front ? 1.0 : 0.0);
            default: return w(p.on_screen ? 1.0 : 0.0);
        }
    }
    bool visible_point(const F& x, const F& y, const F& z) const { return c.is_visible(v3(x, y, z)); }
    bool visible_sphere(const F& x, const F& y, const F& z, const F& r) const { return c.is_visible(v3(x, y, z), d(r)); }
};

class mesh {
public:
    fg::Mesh3D m;

    long long add_vertex(const F& x, const F& y, const F& z, const F& nx, const F& ny, const F& nz, long long col, const F& tu, const F& tv) {
        return static_cast<long long>(m.add_vertex(fg::Vertex3D(v3(x, y, z), v3(nx, ny, nz), color(col), static_cast<float>(d(tu)), static_cast<float>(d(tv)))));
    }
    bool add_triangle(long long a, long long b, long long c) {
        const long long n = vertex_count();
        if (a < 0 || b < 0 || c < 0 || a >= n || b >= n || c >= n) return false;
        m.add_triangle(static_cast<std::uint32_t>(a), static_cast<std::uint32_t>(b), static_cast<std::uint32_t>(c));
        return true;
    }
    void clear() { m.clear(); }
    long long vertex_count() const { return static_cast<long long>(m.vertices().size()); }
    long long triangle_count() const { return static_cast<long long>(m.triangle_count()); }
    bool empty() const { return m.empty(); }

    void quad(const linalg::vector3d& a, const linalg::vector3d& b, const linalg::vector3d& c, const linalg::vector3d& e, const linalg::vector3d& n, const fc::Color& col) {
        const std::uint32_t base = static_cast<std::uint32_t>(m.vertices().size());
        m.add_vertex(fg::Vertex3D(a, n, col, 0.0f, 1.0f));
        m.add_vertex(fg::Vertex3D(b, n, col, 1.0f, 1.0f));
        m.add_vertex(fg::Vertex3D(c, n, col, 1.0f, 0.0f));
        m.add_vertex(fg::Vertex3D(e, n, col, 0.0f, 0.0f));
        m.add_triangle(base, base + 1, base + 2);
        m.add_triangle(base, base + 2, base + 3);
    }

    static mesh box(const F& wd, const F& ht, const F& dp, long long col) {
        mesh out;
        const double x = d(wd) / 2.0, y = d(ht) / 2.0, z = d(dp) / 2.0;
        const fc::Color c = color(col);
        using V = linalg::vector3d;
        out.quad(V{-x, -y,  z}, V{ x, -y,  z}, V{ x,  y,  z}, V{-x,  y,  z}, V{ 0,  0,  1}, c);
        out.quad(V{ x, -y, -z}, V{-x, -y, -z}, V{-x,  y, -z}, V{ x,  y, -z}, V{ 0,  0, -1}, c);
        out.quad(V{ x, -y,  z}, V{ x, -y, -z}, V{ x,  y, -z}, V{ x,  y,  z}, V{ 1,  0,  0}, c);
        out.quad(V{-x, -y, -z}, V{-x, -y,  z}, V{-x,  y,  z}, V{-x,  y, -z}, V{-1,  0,  0}, c);
        out.quad(V{-x,  y,  z}, V{ x,  y,  z}, V{ x,  y, -z}, V{-x,  y, -z}, V{ 0,  1,  0}, c);
        out.quad(V{-x, -y, -z}, V{ x, -y, -z}, V{ x, -y,  z}, V{-x, -y,  z}, V{ 0, -1,  0}, c);
        return out;
    }

    static mesh plane(const F& wd, const F& dp, long long divisions, long long col) {
        mesh out;
        const long long n = std::max<long long>(1, divisions);
        const double sx = d(wd), sz = d(dp);
        const fc::Color c = color(col);
        for (long long j = 0; j <= n; ++j)
            for (long long i = 0; i <= n; ++i) {
                const double fu = static_cast<double>(i) / n, fv = static_cast<double>(j) / n;
                out.m.add_vertex(fg::Vertex3D(linalg::vector3d{ (fu - 0.5) * sx, 0.0, (fv - 0.5) * sz }, linalg::vector3d{ 0.0, 1.0, 0.0 }, c, static_cast<float>(fu), static_cast<float>(fv)));
            }
        for (long long j = 0; j < n; ++j)
            for (long long i = 0; i < n; ++i) {
                const auto a = static_cast<std::uint32_t>(j * (n + 1) + i), b = a + 1, e = static_cast<std::uint32_t>((j + 1) * (n + 1) + i), f = e + 1;
                out.m.add_triangle(a, e, f);
                out.m.add_triangle(a, f, b);
            }
        return out;
    }

    static mesh sphere(const F& radius, long long segments, long long rings, long long col) {
        mesh out;
        const long long sg = std::max<long long>(3, segments), rg = std::max<long long>(2, rings);
        const double r = d(radius), pi = 3.14159265358979323846;
        const fc::Color c = color(col);
        for (long long j = 0; j <= rg; ++j) {
            const double v = static_cast<double>(j) / rg, phi = v * pi;
            for (long long i = 0; i <= sg; ++i) {
                const double uu = static_cast<double>(i) / sg, th = uu * 2.0 * pi;
                const linalg::vector3d n{ std::sin(phi) * std::cos(th), std::cos(phi), std::sin(phi) * std::sin(th) };
                out.m.add_vertex(fg::Vertex3D(linalg::vector3d{ n.x * r, n.y * r, n.z * r }, n, c, static_cast<float>(uu), static_cast<float>(v)));
            }
        }
        for (long long j = 0; j < rg; ++j)
            for (long long i = 0; i < sg; ++i) {
                const auto a = static_cast<std::uint32_t>(j * (sg + 1) + i), b = a + 1, e = static_cast<std::uint32_t>((j + 1) * (sg + 1) + i), f = e + 1;
                out.m.add_triangle(a, b, f);
                out.m.add_triangle(a, f, e);
            }
        return out;
    }

    static mesh cylinder(const F& radius, const F& height, long long segments, long long col) {
        mesh out;
        const long long sg = std::max<long long>(3, segments);
        const double r = d(radius), h = d(height) / 2.0, pi = 3.14159265358979323846;
        const fc::Color c = color(col);
        for (long long i = 0; i < sg; ++i) {
            const double t0 = 2.0 * pi * i / sg, t1 = 2.0 * pi * (i + 1) / sg;
            const linalg::vector3d p0{ std::cos(t0) * r, 0.0, std::sin(t0) * r }, p1{ std::cos(t1) * r, 0.0, std::sin(t1) * r };
            const linalg::vector3d n0{ std::cos(t0), 0.0, std::sin(t0) }, n1{ std::cos(t1), 0.0, std::sin(t1) };
            const std::uint32_t base = static_cast<std::uint32_t>(out.m.vertices().size());
            out.m.add_vertex(fg::Vertex3D(linalg::vector3d{ p0.x, -h, p0.z }, n0, c, static_cast<float>(static_cast<double>(i) / sg), 1.0f));
            out.m.add_vertex(fg::Vertex3D(linalg::vector3d{ p1.x, -h, p1.z }, n1, c, static_cast<float>(static_cast<double>(i + 1) / sg), 1.0f));
            out.m.add_vertex(fg::Vertex3D(linalg::vector3d{ p1.x, h, p1.z }, n1, c, static_cast<float>(static_cast<double>(i + 1) / sg), 0.0f));
            out.m.add_vertex(fg::Vertex3D(linalg::vector3d{ p0.x, h, p0.z }, n0, c, static_cast<float>(static_cast<double>(i) / sg), 0.0f));
            out.m.add_triangle(base, base + 2, base + 1);
            out.m.add_triangle(base, base + 3, base + 2);
            const std::uint32_t top = static_cast<std::uint32_t>(out.m.vertices().size());
            out.m.add_vertex(fg::Vertex3D(linalg::vector3d{ 0.0, h, 0.0 }, linalg::vector3d{ 0.0, 1.0, 0.0 }, c));
            out.m.add_vertex(fg::Vertex3D(linalg::vector3d{ p0.x, h, p0.z }, linalg::vector3d{ 0.0, 1.0, 0.0 }, c));
            out.m.add_vertex(fg::Vertex3D(linalg::vector3d{ p1.x, h, p1.z }, linalg::vector3d{ 0.0, 1.0, 0.0 }, c));
            out.m.add_triangle(top, top + 2, top + 1);
            const std::uint32_t bot = static_cast<std::uint32_t>(out.m.vertices().size());
            out.m.add_vertex(fg::Vertex3D(linalg::vector3d{ 0.0, -h, 0.0 }, linalg::vector3d{ 0.0, -1.0, 0.0 }, c));
            out.m.add_vertex(fg::Vertex3D(linalg::vector3d{ p0.x, -h, p0.z }, linalg::vector3d{ 0.0, -1.0, 0.0 }, c));
            out.m.add_vertex(fg::Vertex3D(linalg::vector3d{ p1.x, -h, p1.z }, linalg::vector3d{ 0.0, -1.0, 0.0 }, c));
            out.m.add_triangle(bot, bot + 1, bot + 2);
        }
        return out;
    }
};

class material {
public:
    fg::Material3D m;
    std::shared_ptr<fg::Texture> tex;

    material() = default;
    material(const material& o) : m(o.m), tex(o.tex) { m.texture = tex.get(); }
    material& operator=(const material& o) { m = o.m; tex = o.tex; m.texture = tex.get(); return *this; }

    static material of(const fg::Material3D& mm) { material out; out.m = mm; out.m.texture = nullptr; return out; }
    static material preset(long long which) {
        switch (which) {
            case 1: return of(fg::Material3D::double_sided());
            case 2: return of(fg::Material3D::transparent());
            case 3: return of(fg::Material3D::unlit());
            case 4: return of(fg::Material3D::metal());
            case 5: return of(fg::Material3D::polished_metal());
            case 6: return of(fg::Material3D::brushed_metal());
            case 7: return of(fg::Material3D::plastic());
            case 8: return of(fg::Material3D::glossy_plastic());
            case 9: return of(fg::Material3D::rubber());
            case 10: return of(fg::Material3D::wood());
            case 11: return of(fg::Material3D::stone());
            case 12: return of(fg::Material3D::ceramic());
            case 13: return of(fg::Material3D::fabric());
            default: return of(fg::Material3D::opaque());
        }
    }
    static material glowing(long long col, const F& strength) { return of(fg::Material3D::glowing(color(col), static_cast<float>(d(strength)))); }
    static material physical(const F& metallic, const F& roughness) { return of(fg::Material3D::physical(static_cast<float>(d(metallic)), static_cast<float>(d(roughness)))); }

    void set_texture(const texture& t) { tex = std::make_shared<fg::Texture>(t.t); m.texture = tex.get(); }
    void clear_texture() { tex.reset(); m.texture = nullptr; }
    bool has_texture() const { return static_cast<bool>(tex); }
    void set_cull(long long c) { m.cull = static_cast<fg::Cull3D>(c); }
    void set_blend(long long b) { m.blend = static_cast<fg::Blend3D>(b); }
    void set_lit(bool on) { m.lit = on; }
    void set_depth(bool test, bool write) { m.depth_test = test; m.depth_write = write; }
    void set_shadow(long long s) { m.shadow = static_cast<fg::Shadow3D>(s); }
    void set_fog(bool on) { m.fog = on; }
    void set_pbr(const F& metallic, const F& roughness) { m.with_pbr(static_cast<float>(d(metallic)), static_cast<float>(d(roughness))); }
    void set_emissive(long long col, const F& strength) { m.with_emissive(color(col), static_cast<float>(d(strength))); }
    void set_occlusion(const F& ao) { m.with_occlusion(static_cast<float>(d(ao))); }
    bool lit() const { return m.lit; }
    F metallic() const { return w(m.metallic); }
    F roughness() const { return w(m.roughness); }
};

class lighting {
public:
    fg::SceneLighting3D l;

    static lighting preset(long long which) {
        lighting out;
        switch (which) {
            case 1: out.l = fg::SceneLighting3D::daylight(); break;
            case 2: out.l = fg::SceneLighting3D::golden_hour(); break;
            case 3: out.l = fg::SceneLighting3D::overcast(); break;
            case 4: out.l = fg::SceneLighting3D::night(); break;
            case 5: out.l = fg::SceneLighting3D::indoor(); break;
            case 6: out.l = fg::SceneLighting3D::studio(); break;
            default: out.l = fg::SceneLighting3D::off(); break;
        }
        return out;
    }
    void set_enabled(bool on) { l.enabled = on; }
    bool enabled() const { return l.enabled; }
    void set_sun(const F& dx, const F& dy, const F& dz, long long col, const F& intensity) { l.enabled = true; l.sun_direction = v3(dx, dy, dz); l.sun_color = color(col); l.sun_intensity = static_cast<float>(d(intensity)); }
    void set_sky(long long col, const F& intensity) { l.sky_color = color(col); l.sky_intensity = static_cast<float>(d(intensity)); }
    void set_ambient(const F& a) { l.ambient = static_cast<float>(d(a)); }
    void set_shadows(bool on) { l.sun_shadow.enabled = on; }
    void set_atmosphere(bool on) { l.atmosphere.enabled = on; }
    void set_exposure(const F& e) { l.tone_map.enabled = true; l.tone_map.exposure = static_cast<float>(d(e)); }
    long long add_point_light(const F& x, const F& y, const F& z, long long col, const F& intensity, const F& radius) {
        if (l.point_lights.size() >= fg::SceneLighting3D::MAX_POINT_LIGHTS) return -1;
        fg::PointLight3D p;
        p.position = v3(x, y, z);
        p.color = color(col);
        p.intensity = static_cast<float>(d(intensity));
        p.radius = static_cast<float>(d(radius));
        l.enabled = true;
        l.point_lights.push_back(p);
        return static_cast<long long>(l.point_lights.size()) - 1;
    }
    void move_point_light(long long i, const F& x, const F& y, const F& z) { if (i >= 0 && i < point_light_count()) l.point_lights[static_cast<std::size_t>(i)].position = v3(x, y, z); }
    void clear_point_lights() { l.point_lights.clear(); }
    long long point_light_count() const { return static_cast<long long>(l.point_lights.size()); }
};

class model {
public:
    std::shared_ptr<fg::Model3D> m;

    static std::string& last_error() { static thread_local std::string e; return e; }
    static model load(const std::string& path, const F& scale, bool z_up) {
        model out;
        auto mm = std::make_shared<fg::Model3D>();
        fg::ModelImportOptions o;
        o.scale = d(scale);
        o.z_up = z_up;
        std::string why;
        if (fg::Model3D::load(path, *mm, &why, o)) { out.m = mm; last_error().clear(); }
        else last_error() = why.empty() ? "could not read the model" : why;
        return out;
    }
    static std::string load_error() { return last_error(); }
    bool valid() const { return static_cast<bool>(m); }
    long long vertex_count() const { return m ? static_cast<long long>(m->vertex_count()) : 0; }
    F bound(long long which) const {
        if (!m) return w(0.0);
        linalg::vector3d lo{}, hi{};
        m->bounds(lo, hi);
        const linalg::vector3d& v = which < 3 ? lo : hi;
        const long long k = which % 3;
        return w(k == 0 ? v.x : (k == 1 ? v.y : v.z));
    }
    long long find_animation(const std::string& name) const { return m ? m->find_animation(name) : -1; }
};

class animator {
public:
    std::shared_ptr<fg::Model3D> m;
    std::shared_ptr<fg::ModelAnimator> a;

    animator() = default;
    explicit animator(const model& md) : m(md.m) { if (m) a = std::make_shared<fg::ModelAnimator>(*m); }
    bool valid() const { return static_cast<bool>(a); }
    bool play(const std::string& name, bool loop, const F& fade, const F& speed) { return a && a->play(name, loop, d(fade), d(speed)); }
    bool play_index(long long i, bool loop, const F& fade, const F& speed) { return a && a->play(static_cast<int>(i), loop, d(fade), d(speed)); }
    void stop() { if (a) a->stop(); }
    void set_speed(const F& s) { if (a) a->set_speed(d(s)); }
    void set_time(const F& t) { if (a) a->set_time(d(t)); }
    F time() const { return w(a ? a->time() : 0.0); }
    F duration() const { return w(a ? a->duration() : 0.0); }
    bool finished() const { return !a || a->finished(); }
    long long current() const { return a ? a->current() : -1; }
    void update(const F& dt) { if (a) a->update(d(dt)); }
};

class tilemap {
public:
    fg::TileMap t;

    tilemap() = default;
    tilemap(long long tw, long long th) : t(u(tw), u(th)) {}

    static std::string& last_error() { static thread_local std::string e; return e; }
    static tilemap load_tiled(const std::string& path) {
        tilemap out;
        std::string why;
        if (!fg::TileMap::load_tiled_json(path, out.t, &why)) last_error() = why.empty() ? "could not read the map" : why;
        else last_error().clear();
        return out;
    }
    static std::string load_error() { return last_error(); }

    long long tile_width() const { return t.tile_width(); }
    long long tile_height() const { return t.tile_height(); }
    long long add_tileset(const texture& tx, long long tw, long long th, long long margin, long long spacing, long long columns) {
        fg::TileSet& set = t.add_tileset(fg::TileSet(tx.t, u(tw), u(th), u(margin), u(spacing), u(columns)));
        return static_cast<long long>(set.first_gid());
    }
    long long tileset_count() const { return static_cast<long long>(t.tilesets().size()); }
    long long add_layer(const std::string& name, long long wd, long long ht) { t.add_layer(name, u(wd), u(ht)); return static_cast<long long>(t.layers().size()) - 1; }
    long long add_layer_csv(const std::string& name, const std::string& csv, long long offset) { return t.add_layer_csv(name, csv, offset) ? static_cast<long long>(t.layers().size()) - 1 : -1; }
    long long layer_count() const { return static_cast<long long>(t.layers().size()); }
    long long layer_index(const std::string& name) const {
        for (std::size_t k = 0; k < t.layers().size(); ++k) if (t.layers()[k].name() == name) return static_cast<long long>(k);
        return -1;
    }
    fg::TileLayer* L(long long i) { return i >= 0 && i < layer_count() ? &t.layers()[static_cast<std::size_t>(i)] : nullptr; }
    const fg::TileLayer* L(long long i) const { return i >= 0 && i < layer_count() ? &t.layers()[static_cast<std::size_t>(i)] : nullptr; }
    std::string layer_name(long long i) const { auto* l = L(i); return l ? l->name() : std::string(); }
    long long layer_width(long long i) const { auto* l = L(i); return l ? l->width() : 0; }
    long long layer_height(long long i) const { auto* l = L(i); return l ? l->height() : 0; }
    long long get(long long i, long long x, long long y) const { auto* l = L(i); return l ? static_cast<long long>(l->get(static_cast<int>(x), static_cast<int>(y))) : 0; }
    void set(long long i, long long x, long long y, long long gid) { if (auto* l = L(i)) l->set(static_cast<int>(x), static_cast<int>(y), static_cast<std::uint32_t>(gid)); }
    void fill(long long i, long long gid) { if (auto* l = L(i)) l->fill(static_cast<std::uint32_t>(gid)); }
    void fill_rect(long long i, long long x, long long y, long long wd, long long ht, long long gid) { if (auto* l = L(i)) l->fill_rect(static_cast<int>(x), static_cast<int>(y), static_cast<int>(wd), static_cast<int>(ht), static_cast<std::uint32_t>(gid)); }
    void set_layer_visible(long long i, bool v) { if (auto* l = L(i)) l->set_visible(v); }
    void set_layer_collides(long long i, bool v) { if (auto* l = L(i)) l->set_collides(v); }
    void set_layer_opacity(long long i, const F& o) { if (auto* l = L(i)) l->set_opacity(static_cast<float>(d(o))); }
    void set_layer_tint(long long i, long long c) { if (auto* l = L(i)) l->set_tint(color(c)); }
    void set_layer_offset(long long i, const F& x, const F& y) { if (auto* l = L(i)) l->set_offset(d(x), d(y)); }
    void set_layer_parallax(long long i, const F& x, const F& y) { if (auto* l = L(i)) l->set_parallax(d(x), d(y)); }
    void set_solid(long long gid, bool v) { t.set_solid(static_cast<std::uint32_t>(gid), v); }
    bool solid(long long gid) const { return t.solid(static_cast<std::uint32_t>(gid)); }
    void set_animation(long long gid, const walnut_rt::coll::vector<walnut_rt::i64>& frames, const walnut_rt::coll::vector<F>& seconds) {
        fg::TileSet* set = nullptr;
        for (auto& s : t.tilesets()) if (s.contains(static_cast<std::uint32_t>(gid))) set = &s;
        if (!set) return;
        fg::TileAnimation anim;
        for (std::size_t k = 0; k < frames.size(); ++k) {
            anim.frames.push_back(static_cast<std::uint32_t>(walnut_rt::to_native<long long>(frames[k])) - set->first_gid());
            anim.durations.push_back(k < seconds.size() ? d(seconds[k]) : 0.1);
        }
        set->set_animation(static_cast<std::uint32_t>(gid) - set->first_gid(), anim);
    }
    void update(const F& dt) { t.update(d(dt)); }
    long long world_to_tile_x(const F& x) const { return t.world_to_tile_x(d(x)); }
    long long world_to_tile_y(const F& y) const { return t.world_to_tile_y(d(y)); }
    F tile_to_world_x(long long x) const { return w(t.tile_to_world_x(static_cast<int>(x))); }
    F tile_to_world_y(long long y) const { return w(t.tile_to_world_y(static_cast<int>(y))); }
    bool solid_at_tile(long long x, long long y) const { return t.solid_at_tile(static_cast<int>(x), static_cast<int>(y)); }
    bool solid_at(const F& x, const F& y) const { return t.solid_at(d(x), d(y)); }
    bool overlaps_solid(const F& x, const F& y, const F& wd, const F& ht) const { return t.overlaps_solid(d(x), d(y), d(wd), d(ht)); }
    F pixel_width() const { return w(t.pixel_width()); }
    F pixel_height() const { return w(t.pixel_height()); }

    fg::TileMove last_move{};
    fg::TileHit last_hit{};
    void move(const F& x, const F& y, const F& wd, const F& ht, const F& dx, const F& dy) { last_move = t.move(d(x), d(y), d(wd), d(ht), d(dx), d(dy)); }
    F move_x() const { return w(last_move.x); }
    F move_y() const { return w(last_move.y); }
    long long move_flags() const {
        return (last_move.hit_x ? 1 : 0) | (last_move.hit_y ? 2 : 0) | (last_move.on_ground ? 4 : 0) | (last_move.on_ceiling ? 8 : 0) | (last_move.on_left ? 16 : 0) | (last_move.on_right ? 32 : 0);
    }
    void raycast(const F& ox, const F& oy, const F& dx, const F& dy, const F& max_distance) { last_hit = t.raycast(d(ox), d(oy), d(dx), d(dy), d(max_distance)); }
    bool hit() const { return last_hit.hit; }
    long long hit_tile_x() const { return last_hit.tile_x; }
    long long hit_tile_y() const { return last_hit.tile_y; }
    long long hit_gid() const { return static_cast<long long>(last_hit.gid); }
    F hit_value(long long which) const {
        switch (which) {
            case 0: return w(last_hit.distance);
            case 1: return w(last_hit.x);
            case 2: return w(last_hit.y);
            case 3: return w(last_hit.normal_x);
            default: return w(last_hit.normal_y);
        }
    }
};

inline bool begin_3d(renderer& r, camera3d& cam) {
    if (!r.valid()) return false;
    cam.set_viewport(r.R().viewport_width(), r.R().viewport_height());
    return r.R().begin_3d(cam.c);
}
inline void end_3d(renderer& r) { if (r.valid()) r.R().end_3d(); }
inline void set_light(renderer& r, const F& dx, const F& dy, const F& dz, const F& ambient, const F& diffuse) {
    if (r.valid()) r.R().set_light_3d(fg::Light3D::sun(v3(dx, dy, dz), static_cast<float>(d(ambient)), static_cast<float>(d(diffuse))));
}
inline void set_lighting(renderer& r, const lighting& l) { if (r.valid()) r.R().set_scene_lighting(l.l); }
inline void draw_mesh(renderer& r, const mesh& m, const material& mat) { if (r.valid()) r.R().draw_mesh(m.m, mat.m); }
inline void draw_mesh_at(renderer& r, const mesh& m, const F& px, const F& py, const F& pz, const F& rx, const F& ry, const F& rz, const F& sx, const F& sy, const F& sz, const material& mat) {
    if (r.valid()) r.R().draw_mesh(m.m, transform_matrix(px, py, pz, rx, ry, rz, sx, sy, sz), mat.m);
}
inline void draw_model(renderer& r, const model& m, const F& px, const F& py, const F& pz, const F& rx, const F& ry, const F& rz, const F& sx, const F& sy, const F& sz) {
    if (r.valid() && m.m) m.m->draw(r.R(), transform_matrix(px, py, pz, rx, ry, rz, sx, sy, sz));
}
inline void draw_animated(renderer& r, const animator& a, const F& px, const F& py, const F& pz, const F& rx, const F& ry, const F& rz, const F& sx, const F& sy, const F& sz) {
    if (r.valid() && a.a) a.a->draw(r.R(), transform_matrix(px, py, pz, rx, ry, rz, sx, sy, sz));
}
inline void draw_line_3d(renderer& r, const F& ax, const F& ay, const F& az, const F& bx, const F& by, const F& bz, long long col, const F& width, bool depth_test) {
    if (r.valid()) r.R().draw_line_3d(v3(ax, ay, az), v3(bx, by, bz), color(col), static_cast<float>(d(width)), depth_test);
}
inline void draw_box_3d(renderer& r, const F& ax, const F& ay, const F& az, const F& bx, const F& by, const F& bz, long long col, const F& width, bool depth_test) {
    if (r.valid()) r.R().draw_box_3d(v3(ax, ay, az), v3(bx, by, bz), color(col), static_cast<float>(d(width)), depth_test);
}
inline long long draw_tilemap(renderer& r, const tilemap& m, const F& x, const F& y) { return r.valid() ? static_cast<long long>(r.R().draw_tilemap(m.t, d(x), d(y))) : 0; }


} // namespace gfx
} // namespace walnut_rt

#endif // WALNUT_GRAPHICS_HPP
