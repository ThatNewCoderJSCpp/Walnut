#ifndef WALNUT_GAME_HPP
#define WALNUT_GAME_HPP

#include "walnut_graphics.hpp"
#include "fizmo/../../Source/Game/asset_manager.hpp"
#include "fizmo/../../Source/Game/scene_graph_2d.hpp"
#include "fizmo/../../Source/System/hot_reload.hpp"

#include <memory>
#include <string>
#include <unordered_set>

namespace walnut_rt {
namespace gfx {

namespace fgm = fizmo::game;

struct assets_state {
    fgm::AssetManager                            mgr;
    std::unique_ptr<fizmo::system::HotReloader>  reloader;
    ~assets_state() {
        mgr.disable_hot_reload();
        reloader.reset();
    }
};

class assets {
public:
    std::shared_ptr<assets_state> s = std::make_shared<assets_state>();

    void add_root(const std::string& folder, bool first) { s->mgr.paths().add_root(fizmo::system::paths::from_utf8(folder), first); }
    void clear_roots() { s->mgr.paths().clear(); }
    image load_image(const std::string& name) {
        auto p = s->mgr.image(name);
        return p ? image(*p) : image();
    }
    texture load_texture(const std::string& name, long long filter, long long wrap) {
        texture out;
        out.t = s->mgr.texture(name, static_cast<fg::SampleFilter>(filter), static_cast<fg::WrapMode>(wrap));
        return out;
    }
    bool text_ok(const std::string& name) { return static_cast<bool>(s->mgr.text(name)); }
    std::string text(const std::string& name) { auto p = s->mgr.text(name); return p ? *p : std::string(); }
    bool loaded(const std::string& name) const { return s->mgr.loaded(name); }
    bool unload(const std::string& name) { return s->mgr.unload(name); }
    long long collect() { return static_cast<long long>(s->mgr.collect()); }
    void clear() { s->mgr.clear(); }
    std::string last_error() const { return s->mgr.last_error(); }
    long long stat(long long which) const {
        const fgm::AssetStats st = s->mgr.stats();
        switch (which) {
            case 0: return static_cast<long long>(st.images);
            case 1: return static_cast<long long>(st.texts);
            case 2: return static_cast<long long>(st.image_bytes);
            default: return static_cast<long long>(st.reloads);
        }
    }
    void enable_hot_reload() {
        if (!s->reloader) s->reloader = std::make_unique<fizmo::system::HotReloader>();
        s->mgr.enable_hot_reload(*s->reloader);
    }
    void disable_hot_reload() { s->mgr.disable_hot_reload(); }
    long long check_reloads() { return s->reloader ? static_cast<long long>(s->reloader->update()) : 0; }
};

struct scene_state {
    fgm::SceneGraph2D              graph;
    fg::Camera2D                   camera;
    std::unordered_set<fgm::Node2D*> alive;
    scene_state() { alive.insert(&graph.root()); }
};

class scene_node {
public:
    std::shared_ptr<scene_state> s;
    fgm::Node2D* p = nullptr;

    scene_node() = default;
    scene_node(std::shared_ptr<scene_state> st, fgm::Node2D* node) : s(std::move(st)), p(node) {}

    bool valid() const { return s && p && s->alive.count(p) > 0; }
    fgm::Node2D* N() const { return valid() ? p : nullptr; }

    scene_node wrap(fgm::Node2D& node) const {
        s->alive.insert(&node);
        return scene_node(s, &node);
    }

    scene_node add_group(const std::string& name) { auto* n = N(); return n ? wrap(n->add_child<fgm::Node2D>(name)) : scene_node(); }
    scene_node add_sprite(const texture& t, const std::string& name) { auto* n = N(); return n ? wrap(n->add_child<fgm::SpriteNode>(t.t, name)) : scene_node(); }
    scene_node add_shape(long long kind, const F& wd, const F& ht, const F& r, const paint& pt, const std::string& name) {
        auto* n = N();
        if (!n) return scene_node();
        std::unique_ptr<fgm::ShapeNode> sh;
        if (kind == 1) sh = fgm::ShapeNode::rounded(d(wd), d(ht), d(r), pt.p);
        else if (kind == 2) sh = fgm::ShapeNode::ellipse(d(wd), d(ht), pt.p);
        else sh = fgm::ShapeNode::rect(d(wd), d(ht), pt.p);
        sh->set_name(name);
        fgm::Node2D& ref = n->adopt(std::move(sh));
        return wrap(ref);
    }
    scene_node add_path(const path& pth, const paint& pt, const std::string& name) {
        auto* n = N();
        if (!n) return scene_node();
        auto sh = fgm::ShapeNode::path(pth.pth, pt.p);
        sh->set_name(name);
        return wrap(n->adopt(std::move(sh)));
    }
    scene_node add_text(const std::string& str, const text_style& st, const std::string& name) {
        auto* n = N();
        if (!n) return scene_node();
        fgm::TextNode& t = n->add_child<fgm::TextNode>(str, st.st);
        t.set_name(name);
        return wrap(t);
    }

    std::string name() const { auto* n = N(); return n ? n->name() : std::string(); }
    void set_name(const std::string& v) { if (auto* n = N()) n->set_name(v); }
    F x() const { auto* n = N(); return w(n ? n->x() : 0.0); }
    F y() const { auto* n = N(); return w(n ? n->y() : 0.0); }
    void set_position(const F& x, const F& y) { if (auto* n = N()) n->set_position(d(x), d(y)); }
    void move(const F& dx, const F& dy) { if (auto* n = N()) n->translate(d(dx), d(dy)); }
    F rotation() const { auto* n = N(); return w(n ? n->rotation() : 0.0); }
    void set_rotation(const F& deg) { if (auto* n = N()) n->set_rotation(d(deg)); }
    void rotate(const F& deg) { if (auto* n = N()) n->rotate(d(deg)); }
    F scale_x() const { auto* n = N(); return w(n ? n->scale_x() : 1.0); }
    F scale_y() const { auto* n = N(); return w(n ? n->scale_y() : 1.0); }
    void set_scale(const F& sx, const F& sy) { if (auto* n = N()) n->set_scale(d(sx), d(sy)); }
    void set_origin(const F& ox, const F& oy) { if (auto* n = N()) n->set_origin(d(ox), d(oy)); }
    long long z_order() const { auto* n = N(); return n ? n->z_order() : 0; }
    void set_z_order(long long z) { if (auto* n = N()) n->set_z_order(static_cast<int>(z)); }
    bool visible() const { auto* n = N(); return n && n->visible(); }
    void set_visible(bool v) { if (auto* n = N()) n->set_visible(v); }
    bool active() const { auto* n = N(); return n && n->active(); }
    void set_active(bool v) { if (auto* n = N()) n->set_active(v); }
    F opacity() const { auto* n = N(); return w(n ? n->opacity() : 0.0f); }
    void set_opacity(const F& o) { if (auto* n = N()) n->set_opacity(static_cast<float>(d(o))); }
    F world_x() const { auto* n = N(); return w(n ? n->world_position().x : 0.0); }
    F world_y() const { auto* n = N(); return w(n ? n->world_position().y : 0.0); }
    F world_rotation() const { auto* n = N(); return w(n ? n->world_rotation() : 0.0); }
    bool hit(const F& wx, const F& wy) const { auto* n = N(); return n && n->hit(d(wx), d(wy)); }
    long long child_count() const { auto* n = N(); return n ? static_cast<long long>(n->child_count()) : 0; }
    scene_node child(long long i) const {
        auto* n = N();
        if (!n || i < 0 || i >= child_count()) return scene_node();
        return wrap(*n->children()[static_cast<std::size_t>(i)]);
    }
    scene_node parent() const { auto* n = N(); return n && n->parent() ? wrap(*n->parent()) : scene_node(); }
    scene_node find(const std::string& name) const { auto* n = N(); fgm::Node2D* f = n ? n->find(name) : nullptr; return f ? wrap(*f) : scene_node(); }
    bool reparent(const scene_node& to) { auto* n = N(); auto* t = to.N(); return n && t && n != t && n->reparent(*t, true); }
    void remove() {
        auto* n = N();
        if (!n || !n->parent()) return;
        n->for_each([this](fgm::Node2D& x) { s->alive.erase(&x); });
        n->parent()->remove_child(n);
    }

    sprite get_sprite() const { sprite out; if (auto* sn = dynamic_cast<fgm::SpriteNode*>(N())) out.sp = sn->sprite(); return out; }
    void set_sprite(const sprite& v) { if (auto* sn = dynamic_cast<fgm::SpriteNode*>(N())) sn->sprite() = v.sp; }
    void set_paint(const paint& pt) { if (auto* sh = dynamic_cast<fgm::ShapeNode*>(N())) sh->paint() = pt.p; }
    void set_size(const F& wd, const F& ht) { if (auto* sh = dynamic_cast<fgm::ShapeNode*>(N())) sh->set_size(d(wd), d(ht)); }
    void set_text(const std::string& str) { if (auto* t = dynamic_cast<fgm::TextNode*>(N())) t->set_text(str); }
    std::string get_text() const { auto* t = dynamic_cast<fgm::TextNode*>(N()); return t ? t->text() : std::string(); }
    void set_style(const text_style& st) { if (auto* t = dynamic_cast<fgm::TextNode*>(N())) t->style() = st.st; }
};

class scene {
public:
    std::shared_ptr<scene_state> s = std::make_shared<scene_state>();

    scene_node root() const { return scene_node(s, &s->graph.root()); }
    void update(const F& dt) { s->graph.update(d(dt)); }
    void draw(renderer& r) {
        if (!r.valid()) return;
        s->graph.set_camera(nullptr);
        s->graph.draw(r.R());
    }
    void draw_camera(renderer& r, const F& x, const F& y, const F& zoom, const F& rotation) {
        if (!r.valid()) return;
        s->camera = fg::Camera2D(d(x), d(y), r.R().viewport_width(), r.R().viewport_height());
        s->camera.set_zoom(d(zoom));
        s->camera.set_rotation(d(rotation));
        s->graph.set_camera(&s->camera);
        s->graph.draw(r.R());
    }
    scene_node pick(const F& sx, const F& sy) {
        fgm::Node2D* n = s->graph.pick_screen(d(sx), d(sy));
        return n && s->alive.count(n) ? scene_node(s, n) : scene_node();
    }
};

struct game_api {};

} // namespace gfx
} // namespace walnut_rt

#endif // WALNUT_GAME_HPP
