#ifndef WALNUT_FLUIDS_HPP
#define WALNUT_FLUIDS_HPP

#include "walnut_runtime.hpp"
#include "walnut_physics.hpp"
#include "walnut_graphics.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace walnut_rt {
namespace fluids {

namespace fp = fizmo::physics;
using R = fp::Real;
using V2 = fp::RVec2;
using fp::operator""_r;

inline bool sand_cell_named(const std::string& name, fp::SandCell2D& out) {
    for (int c = 0; c < static_cast<int>(fp::SandCell2D::count); ++c) {
        if (name == fp::to_string(static_cast<fp::SandCell2D>(c))) {
            out = static_cast<fp::SandCell2D>(c);
            return true;
        }
    }
    return false;
}

inline fp::SandCell2D sand_cell_of(const std::string& name) {
    fp::SandCell2D c = fp::SandCell2D::empty;
    if (sand_cell_named(name, c)) return c;
    fail("a sand cell must be sand, water, oil, lava, steam, smoke, fire, ice, stone, wood, acid, wall or empty, not '" + name + "'");
}

inline fp::SandCell2D blocky_fill_of(const std::string& preset) {
    if (preset.empty() || preset == "sea-water" || preset == "sea_water") return fp::SandCell2D::water;
    if (preset == "mud") return fp::SandCell2D::sand;
    fp::SandCell2D c = fp::SandCell2D::empty;
    if (sand_cell_named(preset, c) && c != fp::SandCell2D::empty && c != fp::SandCell2D::wall) return c;
    fail("a blocky fluid is filled with a cell: sand, water, oil, lava, steam, smoke, fire, ice, stone, wood or acid (sea-water and mud work too), not '" + preset + "'");
}

inline bool is_fluid_preset(const std::string& name) {
    return name.empty() || name == "water" || name == "sea-water" || name == "sea_water" || name == "oil" || name == "honey" || name == "lava" || name == "mud" || name == "mercury";
}

inline long long walnut_color(std::uint32_t rgba) {
    const std::uint32_t r = rgba & 255u, g = (rgba >> 8) & 255u, b = (rgba >> 16) & 255u, a = (rgba >> 24) & 255u;
    return static_cast<long long>((r << 24) | (g << 16) | (b << 8) | a);
}

class fluid2d {
public:
    struct state {
        std::unique_ptr<fp::FluidWorld2D> world;
        fp::FluidVolume2D* v = nullptr;
        fp::FluidRenderer2D painter;
        std::vector<std::pair<std::string, std::uint8_t>> materials;
        long long quality = 1;
    };

    std::shared_ptr<state> s;

    fluid2d() { build("blocky", 0.0_r, 0.0_r, 1.28_r, 1.28_r, "water", 0.0_r, 0.01_r, false); }
    fluid2d(const std::string& level, R x0, R y0, R x1, R y1, const std::string& preset, R fill, R cell, bool heat) { build(level, x0, y0, x1, y1, preset, fill, cell, heat); }

    void step(R dt) {
        if (!std::isfinite(dt) || dt <= 0.0_r) fail("the time step must be above 0");
        s->world->step(dt);
    }
    void step_frame() { s->world->step(s->world->config().fixed_dt); }

    std::string level() const { return physics::fluid_level_name(v().level()); }
    long long cells_x() const { return v().cells_x(); }
    long long cells_y() const { return v().cells_y(); }
    R cell_size() const { return v().cell_size(); }
    long long columns() const { return v().columns(); }
    R column_size() const { return v().column_size(); }
    R min_x() const { return v().bounds().min.x; }
    R min_y() const { return v().bounds().min.y; }
    R max_x() const { return v().bounds().max.x; }
    R max_y() const { return v().bounds().max.y; }

    long long pour(R x, R y, R radius, const std::string& preset) {
        fp::FluidVolume2D& f = v();
        const V2 c{ x, y };
        const R r = positive(radius);
        if (f.level() == fp::FluidLevel::blocky) {
            if (preset.empty()) return static_cast<long long>(f.add_fluid_circle(c, r));
            return static_cast<long long>(f.paint_cells(c, r, blocky_fill_of(preset), 1.0_r, false));
        }
        if (f.level() == fp::FluidLevel::realistic) return static_cast<long long>(f.add_fluid_circle(c, r, V2{}, material_id(preset)));
        if (!is_fluid_preset(preset)) physics::fluid_preset_named(preset);
        return static_cast<long long>(f.add_fluid_circle(c, r));
    }
    long long erase(R x, R y, R radius) { return static_cast<long long>(v().remove_fluid_circle(V2{ x, y }, positive(radius))); }
    void clear() {
        fp::FluidVolume2D& f = v();
        const fp::AABB b = f.bounds();
        const R rx = b.max.x - b.min.x, ry = b.max.y - b.min.y;
        f.remove_fluid_circle(V2{ (b.min.x + b.max.x) * 0.5_r, (b.min.y + b.max.y) * 0.5_r }, std::sqrt(rx * rx + ry * ry));
        f.clear_particles();
    }
    void push(R x, R y, R radius, R vx, R vy, R strength) { v().push(V2{ x, y }, positive(radius), V2{ vx, vy }, std::max(0.0_r, strength)); }
    void swirl(R x, R y, R radius, R spin, R strength) { v().swirl(V2{ x, y }, positive(radius), spin, std::max(0.0_r, strength)); }
    void explode(R x, R y, R radius, R strength) { v().explode(V2{ x, y }, positive(radius), std::max(0.0_r, strength)); }
    long long paint_sand(R x, R y, R radius, const std::string& cell, R probability) {
        fp::FluidVolume2D& f = blocky("paint_sand");
        return static_cast<long long>(f.paint_cells(V2{ x, y }, positive(radius), sand_cell_of(cell), std::clamp(probability, 0.0_r, 1.0_r), true));
    }
    void paint_wall(R x, R y, R radius, bool on) { v().paint_solid(V2{ x, y }, positive(radius), on); }
    void add_heat(R x, R y, R radius, R degrees) { blocky("add_heat").add_heat(V2{ x, y }, positive(radius), degrees); }
    long long count_cells(const std::string& cell) const { return static_cast<long long>(cblocky("count_cells").count_cells(sand_cell_of(cell))); }
    bool heat() const { return v().def().sand.heat; }
    R temperature(long long i, long long j) const {
        const fp::FluidVolume2D& f = cblocky("temperature");
        check_cell(i, j);
        return f.temperature(static_cast<int>(i), static_cast<int>(j));
    }

    std::string cell(long long i, long long j) const {
        const long long id = cell_id(i, j);
        return id == 13 ? std::string("fluid") : std::string(fp::to_string(static_cast<fp::SandCell2D>(id)));
    }
    long long cell_id(long long i, long long j) const {
        const fp::FluidVolume2D& f = v();
        check_cell(i, j);
        const int ci = static_cast<int>(i), cj = static_cast<int>(j);
        if (f.level() == fp::FluidLevel::blocky) return static_cast<long long>(f.sand_cell(ci, cj));
        if (f.solid(ci, cj)) return 1;
        return fill_at(f, ci, cj) > 0.05_r ? 13 : 0;
    }
    R cell_fill(long long i, long long j) const {
        const fp::FluidVolume2D& f = v();
        check_cell(i, j);
        return fill_at(f, static_cast<int>(i), static_cast<int>(j));
    }

    long long particle_count() const { return static_cast<long long>(v().particle_count()); }
    R particle_x(long long i) const { return particle(i).x; }
    R particle_y(long long i) const { return particle(i).y; }
    long long particle_color(long long i) const {
        particle(i);
        return walnut_color(v().colors()[static_cast<std::size_t>(i)]);
    }
    long long particle_material(long long i) const {
        particle(i);
        return static_cast<long long>(v().material_ids()[static_cast<std::size_t>(i)]);
    }
    R particle_radius() const { return v().particle_radius(); }

    R surface_height(R x) const { return v().surface_height(x); }
    R depth_at(R x, R y) const { return v().depth_at(V2{ x, y }); }
    bool inside(R x, R y) const { return v().inside(V2{ x, y }); }
    R velocity_x(R x, R y) const { return v().velocity_at(V2{ x, y }).x; }
    R velocity_y(R x, R y) const { return v().velocity_at(V2{ x, y }).y; }
    R density_at(R x, R y) const { return v().density_at(V2{ x, y }); }
    R total_volume() const { return v().total_volume(); }
    bool sleeping() const { return v().sleeping(); }
    R step_ms() const { return static_cast<R>(s->world->stats().milliseconds); }

    void set_gravity(R x, R y) {
        s->world->set_gravity(V2{ x, y });
        v().wake();
    }
    void set_quality(long long q) {
        s->quality = static_cast<long long>(physics::fluid_quality_of(q));
        s->world->set_quality(static_cast<fp::FluidQuality>(s->quality));
        v().wake();
    }
    void set_threads(long long n) { s->world->set_threads(static_cast<int>(std::clamp<long long>(n, 0, 64))); }
    void set_current(R x, R y) {
        v().set_current(V2{ x, y });
        v().wake();
    }
    void set_property(const std::string& property, R value) {
        fp::FluidVolume2D& f = v();
        if (property == "wave-height") {
            fp::FluidVolumeDef2D& d = f.edit_def();
            d.waves.swell_amplitude = std::max(0.0_r, value);
            d.still.bob_amplitude = std::max(0.0_r, value);
            return;
        }
        fp::FluidMaterial m = f.material();
        if (!physics::edit_fluid_material(m, property, value)) fail("a fluid property must be " + std::string(physics::fluid_properties()) + ", not '" + property + "'");
        f.set_material(m);
    }
    void set_color(R r, R g, R b) {
        fp::FluidVolume2D& f = v();
        fp::FluidMaterial m = f.material();
        m.color = physics::fluid_color(r, g, b, m.color.a);
        f.set_material(m);
    }

    long long add_emitter(R x, R y, R radius, R vx, R vy, R rate, const std::string& preset) {
        fp::FluidVolume2D& f = v();
        fp::FluidEmitter2D e;
        e.position = V2{ x, y };
        e.velocity = V2{ vx, vy };
        e.radius = positive(radius);
        e.rate = std::max(0.0_r, rate);
        if (f.level() == fp::FluidLevel::blocky) e.cell = preset.empty() ? f.def().sand.fill_cell : blocky_fill_of(preset);
        else if (f.level() == fp::FluidLevel::realistic) e.material = material_id(preset);
        return static_cast<long long>(f.add_emitter(e));
    }
    void set_emitter_active(long long i, bool on) {
        fp::FluidVolume2D& f = v();
        if (i < 0 || static_cast<std::size_t>(i) >= f.emitter_count()) fail("there's no emitter " + std::to_string(i));
        f.emitter(static_cast<std::size_t>(i)).active = on;
        f.wake();
    }
    long long emitter_count() const { return static_cast<long long>(v().emitter_count()); }
    void clear_emitters() { v().clear_emitters(); }

    gfx::image to_image(long long width, long long height) {
        if (width < 0 || height < 0 || width > 16384 || height > 16384) fail("a fluid image must be from 0 to 16384 pixels across, not " + std::to_string(width) + " by " + std::to_string(height));
        fp::FluidImageSettings2D settings;
        settings.width = static_cast<int>(width);
        settings.height = static_cast<int>(height);
        const std::vector<std::uint8_t>& px = s->painter.render(v(), settings);
        return gfx::image::from_rgba(s->painter.width(), s->painter.height(), px);
    }

private:
    void build(const std::string& level, R x0, R y0, R x1, R y1, const std::string& preset, R fill, R cell, bool heat) {
        const fp::FluidLevel lv = physics::fluid_level_named(level);
        if (!(x1 > x0) || !(y1 > y0) || !std::isfinite(x0) || !std::isfinite(y0) || !std::isfinite(x1) || !std::isfinite(y1)) fail("a fluid box needs x1 above x0 and y1 above y0");
        if (!(fill >= 0.0_r && fill <= 1.0_r)) fail("a fluid's fill must be from 0 to 1");
        if (!(cell >= 0.0_r) || !std::isfinite(cell)) fail("a fluid's cell size must be 0 (pick one) or above");
        fp::FluidMaterial mat = fp::FluidMaterial::water();
        fp::SandCell2D fill_cell = fp::SandCell2D::water;
        if (lv == fp::FluidLevel::blocky) {
            fill_cell = blocky_fill_of(preset);
            if (is_fluid_preset(preset)) mat = physics::fluid_preset_named(preset);
        } else {
            mat = physics::fluid_preset_named(preset);
        }
        s = std::make_shared<state>();
        fp::FluidWorld2D::Config cfg;
        cfg.gravity = V2{ 0.0_r, -9.81_r };
        cfg.fixed_dt = 1.0_r / 60.0_r;
        cfg.quality = static_cast<fp::FluidQuality>(s->quality);
        s->world = std::make_unique<fp::FluidWorld2D>(cfg);
        s->world->set_quality(cfg.quality);
        fp::FluidVolumeDef2D def = fp::FluidVolumeDef2D::box(lv, fp::AABB(V2{ x0, y0 }, V2{ x1, y1 }), mat, fill);
        def.sand.fill_cell = fill_cell;
        def.sand.heat = heat;
        const R longest = std::max(x1 - x0, y1 - y0);
        if (cell > 0.0_r) def.cell_size = cell;
        else if (lv == fp::FluidLevel::blocky) def.cell_size = std::max(1e-3_r, longest / 256.0_r);
        else def.cell_size = std::max(1e-3_r, longest / 128.0_r);
        const double cells = std::ceil(static_cast<double>(x1 - x0) / def.cell_size) * std::ceil(static_cast<double>(y1 - y0) / def.cell_size);
        if (cells > 4.0e6) fail("that fluid would have " + std::to_string(static_cast<long long>(cells)) + " cells; give it a bigger cell size (at most 4 million cells)");
        s->v = &s->world->add_volume(def);
        s->materials.emplace_back(preset.empty() ? std::string("water") : preset, static_cast<std::uint8_t>(0));
    }
    std::uint8_t material_id(const std::string& preset) {
        if (preset.empty()) return 0;
        for (const auto& m : s->materials) {
            if (m.first == preset) return m.second;
        }
        const fp::FluidMaterial mat = physics::fluid_preset_named(preset);
        if (v().material_count() >= 255) fail("a fluid can hold at most 255 different presets");
        const std::uint8_t id = v().add_material(mat);
        s->materials.emplace_back(preset, id);
        return id;
    }
    static R positive(R r) {
        if (!std::isfinite(r) || r <= 0.0_r) fail("a brush radius must be above 0");
        return r;
    }
    fp::FluidVolume2D& v() { return *s->v; }
    const fp::FluidVolume2D& v() const { return *s->v; }
    fp::FluidVolume2D& blocky(const char* what) {
        if (v().level() != fp::FluidLevel::blocky) fail(std::string(what) + " works on blocky fluids, and this one is " + level());
        return v();
    }
    const fp::FluidVolume2D& cblocky(const char* what) const {
        if (v().level() != fp::FluidLevel::blocky) fail(std::string(what) + " works on blocky fluids, and this one is " + level());
        return v();
    }
    void check_cell(long long i, long long j) const {
        if (i < 0 || j < 0 || i >= v().cells_x() || j >= v().cells_y()) fail("cell (" + std::to_string(i) + ", " + std::to_string(j) + ") is outside the fluid's " + std::to_string(v().cells_x()) + " by " + std::to_string(v().cells_y()) + " cells");
    }
    const V2& particle(long long i) const {
        if (i < 0 || static_cast<std::size_t>(i) >= v().particle_count()) fail("fluid particle " + std::to_string(i) + " is out of range (there are " + std::to_string(v().particle_count()) + ")");
        return v().positions()[static_cast<std::size_t>(i)];
    }
    static R fill_at(const fp::FluidVolume2D& f, int i, int j) {
        const std::size_t c = f.cell_index(i, j);
        switch (f.level()) {
        case fp::FluidLevel::blocky: {
            const fp::SandKind2D k = fp::sand_cell_info(f.sand_cell(i, j)).kind;
            return k == fp::SandKind2D::empty || k == fp::SandKind2D::solid ? 0.0_r : 1.0_r;
        }
        case fp::FluidLevel::flowing:
            return c < f.cell_fill().size() ? f.cell_fill()[c] : 0.0_r;
        case fp::FluidLevel::realistic:
            return c < f.cell_densities().size() ? f.cell_densities()[c] / std::max(f.rest_density(), 1e-6_r) : 0.0_r;
        default:
            return f.inside(f.cell_center(i, j)) ? 1.0_r : 0.0_r;
        }
    }
};

} // namespace fluids
} // namespace walnut_rt

#endif // WALNUT_FLUIDS_HPP
