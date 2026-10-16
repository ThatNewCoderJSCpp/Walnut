#ifndef WALNUT_UI_HPP
#define WALNUT_UI_HPP

#include "walnut_graphics.hpp"
#include "fizmo/../../Source/UI/ui.hpp"

#include <memory>
#include <string>
#include <vector>

namespace walnut_rt {
namespace gfx {

namespace fu = fizmo::ui;

struct ui_events {
    long long clicks = 0;
    bool      changed = false;
    bool      released = false;
    bool      submitted = false;
};

struct ui_state {
    std::shared_ptr<window_state> win;
    std::unique_ptr<fu::Ui>       ui;
    ~ui_state() { ui.reset(); }
};

struct ui_node {
    std::shared_ptr<ui_state>   ui;
    std::shared_ptr<ui_node>    parent;
    std::unique_ptr<fu::Widget> owned;
    fu::Widget*                 w = nullptr;
    std::shared_ptr<ui_events>  ev = std::make_shared<ui_events>();
    std::shared_ptr<fu::RadioGroup> group;
};

class ui_widget {
public:
    std::shared_ptr<ui_node> n;

    ui_widget() = default;

    template <typename T>
    static ui_widget make(std::unique_ptr<T> p) {
        ui_widget out;
        out.n = std::make_shared<ui_node>();
        out.n->w = p.get();
        out.n->owned = std::move(p);
        return out;
    }

    template <typename T>
    T* as() const { return n && n->w ? dynamic_cast<T*>(n->w) : nullptr; }

    bool valid() const { return n && n->w; }
    ui_widget node() const { return *this; }

    void set_size(const F& wd, const F& ht) { if (valid()) n->w->set_size(static_cast<float>(d(wd)), static_cast<float>(d(ht))); }
    void set_width(const F& wd) { if (valid()) n->w->set_width(static_cast<float>(d(wd))); }
    void set_height(const F& ht) { if (valid()) n->w->set_height(static_cast<float>(d(ht))); }
    void set_min_size(const F& wd, const F& ht) { if (valid()) n->w->set_min_size(static_cast<float>(d(wd)), static_cast<float>(d(ht))); }
    void set_max_size(const F& wd, const F& ht) { if (valid()) n->w->set_max_size(static_cast<float>(d(wd)), static_cast<float>(d(ht))); }
    void set_flex(const F& f) { if (valid()) n->w->set_flex(static_cast<float>(d(f))); }
    void set_visible(bool v) { if (valid()) n->w->set_visible(v); }
    bool visible() const { return valid() && n->w->visible(); }
    void set_enabled(bool e) { if (valid()) n->w->set_enabled(e); }
    bool enabled() const { return valid() && n->w->enabled(); }
    void set_tooltip(const std::string& t) { if (valid()) n->w->set_tooltip(t); }
    void set_id(const std::string& id) { if (valid()) n->w->set_id(id); }
    std::string id() const { return valid() ? n->w->id() : std::string(); }
    bool hovered() const { return valid() && n->w->hovered(); }
    bool focused() const { return valid() && n->w->focused(); }
    void focus() { if (valid()) n->w->focus(); }
    F rect(long long which) const {
        if (!valid()) return w(0.0);
        const fu::Rect r = n->w->screen_rect();
        return w(which == 0 ? r.x : (which == 1 ? r.y : (which == 2 ? r.w : r.h)));
    }

    bool take_clicked() { if (!n || n->ev->clicks == 0) return false; --n->ev->clicks; return true; }
    bool take_changed() { if (!n || !n->ev->changed) return false; n->ev->changed = false; return true; }
    bool take_released() { if (!n || !n->ev->released) return false; n->ev->released = false; return true; }
    bool take_submitted() { if (!n || !n->ev->submitted) return false; n->ev->submitted = false; return true; }

    bool add(const ui_widget& child) {
        if (!valid() || !child.valid() || child.n.get() == n.get()) return false;
        if (child.n->owned) n->w->adopt(std::move(child.n->owned));
        else if (child.n->w->parent() != n->w) n->w->adopt(child.n->w->parent()->release(child.n->w));
        child.n->parent = n;
        child.n->ui = n->ui;
        return true;
    }

    bool remove(const ui_widget& child) {
        if (!valid() || !child.valid() || child.n->w->parent() != n->w) return false;
        child.n->owned = n->w->release(child.n->w);
        child.n->parent.reset();
        return true;
    }

    long long child_count() const { return valid() ? static_cast<long long>(n->w->child_count()) : 0; }

    void set_spacing(const F& s) { if (auto* p = as<fu::Stack>()) p->set_spacing(static_cast<float>(d(s))); }
    void set_cross_align(long long a) { if (auto* p = as<fu::Stack>()) p->set_cross_align(static_cast<fu::Align>(a)); }
    void set_main_align(long long a) { if (auto* p = as<fu::Stack>()) p->set_main_align(static_cast<fu::Align>(a)); }
    void set_background(long long c) { if (auto* p = as<fu::Container>()) p->set_background(color(c)); }
    void set_border(long long c) { if (auto* p = as<fu::Container>()) p->set_border(color(c)); }
    void set_radius(const F& r) { if (auto* p = as<fu::Container>()) p->set_radius(static_cast<float>(d(r))); }
    void set_padding(const F& pd) { if (auto* p = as<fu::Container>()) p->set_padding(static_cast<float>(d(pd))); }

    void set_text(const std::string& t) {
        if (auto* p = as<fu::Label>()) p->set_text(t);
        else if (auto* b = as<fu::Button>()) b->set_text(t);
        else if (auto* c = as<fu::Checkbox>()) c->set_text(t);
        else if (auto* g = as<fu::Toggle>()) g->set_text(t);
        else if (auto* f = as<fu::TextField>()) f->set_text(t);
        else if (auto* pb = as<fu::ProgressBar>()) pb->set_text(t);
    }
    std::string text() const {
        if (auto* p = as<fu::Label>()) return p->text();
        if (auto* b = as<fu::Button>()) return b->text();
        if (auto* c = as<fu::Checkbox>()) return c->text();
        if (auto* f = as<fu::TextField>()) return f->text();
        return std::string();
    }
    void set_color(long long c) {
        if (auto* p = as<fu::Label>()) p->set_color(color(c));
        else if (auto* pb = as<fu::ProgressBar>()) pb->set_color(color(c));
        else if (auto* sp = as<fu::BusySpinner>()) sp->set_color(color(c));
        else if (auto* cp = as<fu::ColorPicker>()) cp->set_color(color(c));
    }
    long long color_value() const { if (auto* cp = as<fu::ColorPicker>()) return packed(cp->color()); return 0; }
    void set_font_size(const F& s) { if (auto* p = as<fu::Label>()) p->set_font_size(d(s)); }
    void set_bold(bool b) { if (auto* p = as<fu::Label>()) p->set_bold(b); }
    void set_muted(bool m) { if (auto* p = as<fu::Label>()) p->set_muted(m); }
    void set_align(long long a) { if (auto* p = as<fu::Label>()) p->set_align(static_cast<fu::Align>(a)); }
    void set_button_style(long long s) { if (auto* b = as<fu::Button>()) b->set_style(static_cast<fu::ButtonStyle>(s)); }
    void click() { if (auto* b = as<fu::Button>()) b->click(); }

    bool checked() const {
        if (auto* c = as<fu::Checkbox>()) return c->checked();
        if (auto* g = as<fu::Toggle>()) return g->checked();
        if (auto* r = as<fu::RadioButton>()) return r->selected();
        return false;
    }
    void set_checked(bool v) {
        if (auto* c = as<fu::Checkbox>()) c->set_checked(v);
        else if (auto* g = as<fu::Toggle>()) g->set_checked(v);
        else if (auto* r = as<fu::RadioButton>()) { if (v) r->select(); }
    }

    F value() const {
        if (auto* s = as<fu::Slider>()) return w(s->value());
        if (auto* p = as<fu::ProgressBar>()) return w(p->value());
        if (auto* i = as<fu::NumberField<long long>>()) return w(static_cast<double>(i->value()));
        if (auto* f = as<fu::NumberField<double>>()) return w(f->value());
        return w(0.0);
    }
    void set_value(const F& v) {
        if (auto* s = as<fu::Slider>()) s->set_value(d(v));
        else if (auto* p = as<fu::ProgressBar>()) p->set_value(d(v));
        else if (auto* i = as<fu::NumberField<long long>>()) i->set_value(static_cast<long long>(std::llround(d(v))));
        else if (auto* f = as<fu::NumberField<double>>()) f->set_value(d(v));
    }
    void set_range(const F& lo, const F& hi) {
        if (auto* s = as<fu::Slider>()) s->set_range(d(lo), d(hi));
        else if (auto* i = as<fu::NumberField<long long>>()) i->set_range(static_cast<long long>(std::llround(d(lo))), static_cast<long long>(std::llround(d(hi))));
        else if (auto* f = as<fu::NumberField<double>>()) f->set_range(d(lo), d(hi));
    }
    void set_step(const F& st) {
        if (auto* s = as<fu::Slider>()) s->set_step(d(st));
        else if (auto* i = as<fu::NumberField<long long>>()) i->set_step(static_cast<long long>(std::llround(d(st))));
        else if (auto* f = as<fu::NumberField<double>>()) f->set_step(d(st));
    }
    void set_show_value(bool on, long long decimals) { if (auto* s = as<fu::Slider>()) s->set_show_value(on, static_cast<int>(decimals)); }
    void set_decimals(long long dd) { if (auto* f = as<fu::NumberField<double>>()) f->set_decimals(static_cast<int>(dd)); }
    void set_spin_buttons(bool on) {
        if (auto* i = as<fu::NumberField<long long>>()) i->set_spin_buttons(on);
        else if (auto* f = as<fu::NumberField<double>>()) f->set_spin_buttons(on);
    }
    bool dragging() const { if (auto* s = as<fu::Slider>()) return s->dragging(); return false; }
    void set_indeterminate(bool on) { if (auto* p = as<fu::ProgressBar>()) p->set_indeterminate(on); }
    void set_show_text(bool on) { if (auto* p = as<fu::ProgressBar>()) p->set_show_text(on); }

    void add_item(const std::string& item) { if (auto* dd = as<fu::Dropdown>()) dd->add_item(item); }
    void set_items(const walnut_rt::coll::vector<std::string>& items) { if (auto* dd = as<fu::Dropdown>()) dd->set_items(std::vector<std::string>(items.begin(), items.end())); }
    long long item_count() const { if (auto* dd = as<fu::Dropdown>()) return static_cast<long long>(dd->items().size()); return 0; }
    long long selected() const {
        if (auto* dd = as<fu::Dropdown>()) return dd->selected();
        if (auto* t = as<fu::TabView>()) return t->selected();
        if (n && n->group) return n->group->selected();
        return -1;
    }
    std::string selected_text() const { if (auto* dd = as<fu::Dropdown>()) return dd->selected_text(); return std::string(); }
    void set_selected(long long i) {
        if (auto* dd = as<fu::Dropdown>()) dd->set_selected(static_cast<int>(i));
        else if (auto* t = as<fu::TabView>()) t->select(static_cast<int>(i), false);
        else if (n && n->group) n->group->set_selected(static_cast<int>(i));
    }
    void set_placeholder(const std::string& p) {
        if (auto* dd = as<fu::Dropdown>()) dd->set_placeholder(p);
        else if (auto* f = as<fu::TextField>()) f->set_placeholder(p);
    }
    void set_searchable(bool s) { if (auto* dd = as<fu::Dropdown>()) dd->set_searchable(s); }

    void set_max_length(long long len) { if (auto* f = as<fu::TextField>()) f->set_max_length(static_cast<std::size_t>(len < 0 ? 0 : len)); }
    void set_password(bool on) { if (auto* f = as<fu::TextField>()) f->set_password(on); }
    void set_read_only(bool on) { if (auto* f = as<fu::TextField>()) f->set_read_only(on); }
    void set_allowed_chars(const std::string& chars) { if (auto* f = as<fu::TextField>()) f->set_allowed_chars(chars); }
    void select_all() { if (auto* f = as<fu::TextField>()) f->select_all(); }

    void set_alpha_enabled(bool on) { if (auto* cp = as<fu::ColorPicker>()) cp->set_alpha_enabled(on); }

    bool set_content(const ui_widget& child) {
        auto* sv = as<fu::ScrollView>();
        if (!sv || !child.valid()) return false;
        sv->clear_children();
        if (child.n->owned) sv->adopt(std::move(child.n->owned));
        else sv->adopt(child.n->w->parent()->release(child.n->w));
        child.n->parent = n;
        child.n->ui = n->ui;
        return true;
    }
    void set_scroll_directions(bool horizontal, bool vertical) { if (auto* sv = as<fu::ScrollView>()) { sv->set_horizontal(horizontal); sv->set_vertical(vertical); } }
    void scroll_to(const F& x, const F& y) { if (auto* sv = as<fu::ScrollView>()) sv->scroll_to(static_cast<float>(d(x)), static_cast<float>(d(y)), true); }
    F scroll_x() const { if (auto* sv = as<fu::ScrollView>()) return w(sv->scroll_x()); return w(0.0); }
    F scroll_y() const { if (auto* sv = as<fu::ScrollView>()) return w(sv->scroll_y()); return w(0.0); }

    bool add_tab(const std::string& title, const ui_widget& page) {
        auto* t = as<fu::TabView>();
        if (!t || !page.valid()) return false;
        std::unique_ptr<fu::Widget> p = page.n->owned ? std::move(page.n->owned) : page.n->w->parent()->release(page.n->w);
        t->add_tab(title, std::move(p));
        page.n->parent = n;
        page.n->ui = n->ui;
        return true;
    }
    long long tab_count() const { if (auto* t = as<fu::TabView>()) return t->tab_count(); return 0; }

    ui_widget radio(const std::string& label, long long value) {
        ui_widget out;
        if (!n || !n->group) return out;
        auto p = std::make_unique<fu::RadioButton>(label, n->group, static_cast<int>(value));
        out = make(std::move(p));
        return out;
    }
};

inline void wire(ui_widget& wd) {
    auto ev = wd.n->ev;
    if (auto* b = wd.as<fu::Button>()) b->on_click([ev] { ++ev->clicks; });
    else if (auto* c = wd.as<fu::Checkbox>()) c->on_change([ev](bool) { ev->changed = true; });
    else if (auto* g = wd.as<fu::Toggle>()) g->on_change([ev](bool) { ev->changed = true; });
    else if (auto* s = wd.as<fu::Slider>()) { s->on_change([ev](double) { ev->changed = true; }); s->on_release([ev](double) { ev->released = true; }); }
    else if (auto* dd = wd.as<fu::Dropdown>()) dd->on_change([ev](int) { ev->changed = true; });
    else if (auto* t = wd.as<fu::TabView>()) t->on_change([ev](int) { ev->changed = true; });
    else if (auto* cp = wd.as<fu::ColorPicker>()) cp->on_change([ev](const fc::Color&) { ev->changed = true; });
    else if (auto* i = wd.as<fu::NumberField<long long>>()) { i->on_value_change([ev](long long) { ev->changed = true; }); i->on_submit([ev](const std::string&) { ev->submitted = true; }); }
    else if (auto* f = wd.as<fu::NumberField<double>>()) { f->on_value_change([ev](double) { ev->changed = true; }); f->on_submit([ev](const std::string&) { ev->submitted = true; }); }
    else if (auto* tf = wd.as<fu::TextField>()) { tf->on_change([ev](const std::string&) { ev->changed = true; }); tf->on_submit([ev](const std::string&) { ev->submitted = true; }); }
}

template <typename T, typename... A>
inline ui_widget make_widget(A&&... args) {
    ui_widget out = ui_widget::make(std::make_unique<T>(std::forward<A>(args)...));
    wire(out);
    return out;
}

inline ui_widget ui_label(const std::string& t) { return make_widget<fu::Label>(t); }
inline ui_widget ui_button(const std::string& t) { return make_widget<fu::Button>(t); }
inline ui_widget ui_checkbox(const std::string& t, bool on) { return make_widget<fu::Checkbox>(t, on); }
inline ui_widget ui_toggle(const std::string& t, bool on) { return make_widget<fu::Toggle>(t, on); }
inline ui_widget ui_slider(const F& v, const F& lo, const F& hi, const F& step) { return make_widget<fu::Slider>(d(v), d(lo), d(hi), d(step)); }
inline ui_widget ui_progress(const F& v) { return make_widget<fu::ProgressBar>(d(v)); }
inline ui_widget ui_spinner(const F& size) { return make_widget<fu::BusySpinner>(static_cast<float>(d(size))); }
inline ui_widget ui_dropdown(const walnut_rt::coll::vector<std::string>& items) { return make_widget<fu::Dropdown>(std::vector<std::string>(items.begin(), items.end())); }
inline ui_widget ui_text_field(const std::string& t, const std::string& placeholder) { return make_widget<fu::TextField>(t, placeholder); }
inline ui_widget ui_int_field(long long v, long long lo, long long hi, long long step) { return make_widget<fu::IntField>(v, lo, hi, step); }
inline ui_widget ui_float_field(const F& v, const F& lo, const F& hi, const F& step) { return make_widget<fu::FloatField>(d(v), d(lo), d(hi), d(step)); }
inline ui_widget ui_color_picker(long long c, bool alpha) { return make_widget<fu::ColorPicker>(color(c), alpha); }
inline ui_widget ui_separator(bool vertical) { return make_widget<fu::Separator>(vertical ? fu::Orientation::Vertical : fu::Orientation::Horizontal); }
inline ui_widget ui_spacer(const F& flex) { return make_widget<fu::Spacer>(static_cast<float>(d(flex))); }
inline ui_widget ui_vstack(const F& spacing) { return make_widget<fu::VStack>(static_cast<float>(d(spacing))); }
inline ui_widget ui_hstack(const F& spacing) { return make_widget<fu::HStack>(static_cast<float>(d(spacing))); }
inline ui_widget ui_panel(bool horizontal) { return make_widget<fu::Panel>(horizontal ? fu::Orientation::Horizontal : fu::Orientation::Vertical); }
inline ui_widget ui_scroll_view() { return make_widget<fu::ScrollView>(); }
inline ui_widget ui_tab_view() { return make_widget<fu::TabView>(); }
inline ui_widget ui_radio_group() {
    ui_widget out;
    out.n = std::make_shared<ui_node>();
    out.n->group = std::make_shared<fu::RadioGroup>();
    auto ev = out.n->ev;
    out.n->group->on_change([ev](int) { ev->changed = true; });
    return out;
}

class ui {
public:
    std::shared_ptr<ui_state> s;
    std::shared_ptr<ui_node>  root_node;

    ui() = default;
    explicit ui(const window& wnd) : s(std::make_shared<ui_state>()) {
        s->win = wnd.s;
        s->ui = std::make_unique<fu::Ui>();
        if (wnd.valid()) s->ui->attach(*wnd.s->win);
        root_node = std::make_shared<ui_node>();
        root_node->ui = s;
        root_node->w = &s->ui->root();
    }

    bool valid() const { return s && s->ui; }
    ui_widget root() const { ui_widget out; out.n = root_node; return out; }
    bool add(const ui_widget& child) { return root().add(child); }
    bool remove(const ui_widget& child) { return root().remove(child); }
    void update(const F& dt) { if (valid()) s->ui->update(d(dt)); }
    void draw(renderer& r) {
        if (!valid() || !r.valid()) return;
        s->ui->layout(r.R());
        s->ui->draw(r.R());
    }
    void set_dark() { if (valid()) s->ui->set_theme(fu::Theme::dark()); }
    void set_light() { if (valid()) s->ui->set_theme(fu::Theme::light()); }
    void set_font_size(const F& px) { if (valid()) s->ui->theme().font_size = d(px); }
    void set_font(const std::string& name) { if (valid()) s->ui->theme().font = font_named(name); }
    void set_accent(long long c) { if (valid()) { auto& th = s->ui->theme(); th.accent = color(c); th.focus = color(c); } }
    void set_scale(const F& sc) { if (valid()) s->ui->set_scale(static_cast<float>(d(sc))); }
    F scale() const { return w(valid() ? s->ui->scale() : 1.0f); }
    bool wants_mouse() const { return valid() && s->ui->wants_mouse(); }
    bool wants_keyboard() const { return valid() && s->ui->wants_keyboard(); }
    void alert(const std::string& title, const std::string& message) { if (valid()) s->ui->alert(title, message); }
    bool modal_open() const { return valid() && s->ui->modal_open(); }
    void clear_focus() { if (valid()) s->ui->clear_focus(); }
};

struct ui_api {};

} // namespace gfx
} // namespace walnut_rt

#endif // WALNUT_UI_HPP
