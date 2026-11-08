#ifndef WALNUT_PHYSICS_HPP
#define WALNUT_PHYSICS_HPP

#include "walnut_runtime.hpp"
#include "fizmo/../physics.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace walnut_rt {
namespace physics {

namespace fp = fizmo::physics;
using R = fp::Real;
using V3 = fp::RVec3;
using fp::operator""_r;

struct api {};

inline fp::PhysicsFlags kind_flags(const std::string& kind) {
    if (kind == "dynamic" || kind.empty()) return fp::PhysicsFlags::DefaultDynamic;
    if (kind == "static" || kind == "fixed") return fp::PhysicsFlags::DefaultStatic;
    if (kind == "kinematic") return fp::PhysicsFlags::DefaultKinematic;
    fail("a body kind must be dynamic, static or kinematic, not '" + kind + "'");
}

inline fp::PhysicsFlags flag_named(const std::string& name) {
    struct Named {
        const char* name;
        fp::PhysicsFlags flag;
    };
    static const Named all[] = {
        { "gravity", fp::PhysicsFlags::Gravity }, { "forces", fp::PhysicsFlags::LinearForces }, { "torque", fp::PhysicsFlags::Torque },
        { "wind", fp::PhysicsFlags::Wind }, { "buoyancy", fp::PhysicsFlags::Buoyancy }, { "magnetism", fp::PhysicsFlags::Magnetism },
        { "springs", fp::PhysicsFlags::Springs }, { "force-fields", fp::PhysicsFlags::UserForceField }, { "moves", fp::PhysicsFlags::LinearVelocity },
        { "turns", fp::PhysicsFlags::AngularVelocity }, { "drag", fp::PhysicsFlags::LinearDamping }, { "spin-drag", fp::PhysicsFlags::AngularDamping },
        { "speed-cap", fp::PhysicsFlags::VelocityClamping }, { "spin-cap", fp::PhysicsFlags::AngularClamping }, { "pushes", fp::PhysicsFlags::LinearImpulses },
        { "twists", fp::PhysicsFlags::AngularImpulses }, { "contact-impulses", fp::PhysicsFlags::ContactImpulses }, { "joint-impulses", fp::PhysicsFlags::JointImpulses },
        { "lock-rot-x", fp::PhysicsFlags::LockRotationX }, { "lock-rot-y", fp::PhysicsFlags::LockRotationY }, { "lock-rot-z", fp::PhysicsFlags::LockRotationZ },
        { "collisions", fp::PhysicsFlags::CollisionResponse }, { "friction", fp::PhysicsFlags::FrictionResponse }, { "bounce", fp::PhysicsFlags::RestitutionResponse },
        { "contacts", fp::PhysicsFlags::ContactEvents }, { "sensor-events", fp::PhysicsFlags::SensorEvents }, { "pre-solve", fp::PhysicsFlags::PreSolveEvents },
        { "post-solve", fp::PhysicsFlags::PostSolveEvents }, { "broadphase", fp::PhysicsFlags::BroadphaseActive }, { "bullet", fp::PhysicsFlags::Bullet },
        { "sleep", fp::PhysicsFlags::AllowSleep }, { "awake", fp::PhysicsFlags::StartAwake }, { "lock-x", fp::PhysicsFlags::LockPositionX },
        { "lock-y", fp::PhysicsFlags::LockPositionY }, { "lock-z", fp::PhysicsFlags::LockPositionZ }, { "visible", fp::PhysicsFlags::Visible },
        { "debug", fp::PhysicsFlags::DebugDraw }, { "show-velocity", fp::PhysicsFlags::ShowVelocity }, { "show-contacts", fp::PhysicsFlags::ShowContacts },
        { "enabled", fp::PhysicsFlags::Enabled }, { "auto-disable", fp::PhysicsFlags::AutoDisable }, { "destroy-sleep", fp::PhysicsFlags::DestroyOnSleep },
        { "destroy-out", fp::PhysicsFlags::DestroyOffScreen }, { "islands", fp::PhysicsFlags::IslandMember }, { "warm-start", fp::PhysicsFlags::WarmStarting },
        { "correction", fp::PhysicsFlags::PositionCorrection }, { "speculative", fp::PhysicsFlags::SpeculativeContacts }, { "draggable", fp::PhysicsFlags::Draggable },
        { "ghost", fp::PhysicsFlags::Intangible }, { "impact-sounds", fp::PhysicsFlags::ImpactSounds }, { "trail", fp::PhysicsFlags::TrailEffect },
        { "impact-particles", fp::PhysicsFlags::ParticleOnImpact }, { "user-1", fp::PhysicsFlags::UserFlag1 }, { "user-2", fp::PhysicsFlags::UserFlag2 },
        { "user-3", fp::PhysicsFlags::UserFlag3 }, { "user-4", fp::PhysicsFlags::UserFlag4 }, { "user-5", fp::PhysicsFlags::UserFlag5 },
        { "user-6", fp::PhysicsFlags::UserFlag6 }, { "user-7", fp::PhysicsFlags::UserFlag7 }, { "user-8", fp::PhysicsFlags::UserFlag8 },
        { "user-9", fp::PhysicsFlags::UserFlag9 }, { "user-10", fp::PhysicsFlags::UserFlag10 },
    };
    for (const Named& n : all) {
        if (name == n.name) return n.flag;
    }
    fail("there's no physics flag called '" + name + "'");
}

inline fp::BodyQuality quality_named(const std::string& q) {
    const std::optional<fp::BodyQuality> parsed = fp::parse_body_quality(q);
    if (!parsed) fail("quality must be default, arcade, normal, high or ultra, not '" + q + "'");
    return *parsed;
}

inline fp::ContinuousMode continuous_named(const std::string& c) {
    if (c == "default") return fp::ContinuousMode::Default;
    if (c == "on") return fp::ContinuousMode::On;
    if (c == "off") return fp::ContinuousMode::Off;
    fail("fast-move checks must be default, on or off, not '" + c + "'");
}

inline R positive(R v, const char* what) {
    if (!std::isfinite(v) || v <= 0.0_r) fail(std::string(what) + " must be above 0");
    return v;
}

inline fp::FluidLevel fluid_level_named(const std::string& name) {
    if (name == "blocky") return fp::FluidLevel::blocky;
    if (name == "still") return fp::FluidLevel::still;
    if (name == "flowing") return fp::FluidLevel::flowing;
    if (name == "waves") return fp::FluidLevel::waves;
    if (name == "realistic") return fp::FluidLevel::realistic;
    fail("a fluid level must be blocky, still, flowing, waves or realistic, not '" + name + "'");
}

inline std::string fluid_level_name(fp::FluidLevel level) { return fp::to_string(level); }

inline fp::FluidMaterial fluid_preset_named(const std::string& name) {
    if (name == "water" || name.empty()) return fp::FluidMaterial::water();
    if (name == "sea-water" || name == "sea_water") return fp::FluidMaterial::sea_water();
    if (name == "oil") return fp::FluidMaterial::oil();
    if (name == "honey") return fp::FluidMaterial::honey();
    if (name == "lava") return fp::FluidMaterial::lava();
    if (name == "mud") return fp::FluidMaterial::mud();
    if (name == "mercury") return fp::FluidMaterial::mercury();
    fail("a fluid preset must be water, sea-water, oil, honey, lava, mud or mercury, not '" + name + "'");
}

inline fp::FluidQuality fluid_quality_of(long long q) {
    if (q < 0 || q > 3) fail("fluid quality must be 0 (low), 1 (medium), 2 (high) or 3 (ultra), not " + std::to_string(q));
    return static_cast<fp::FluidQuality>(q);
}

inline bool edit_fluid_material(fp::FluidMaterial& m, const std::string& property, R v) {
    if (!std::isfinite(v)) fail("a fluid's " + property + " must be a finite number");
    if (property == "density") m.density = positive(v, "a fluid's density");
    else if (property == "viscosity") m.viscosity = std::max(0.0_r, v);
    else if (property == "surface-tension") m.surface_tension = std::max(0.0_r, v);
    else if (property == "drag") m.linear_drag = std::max(0.0_r, v);
    else if (property == "quadratic-drag") m.quadratic_drag = std::max(0.0_r, v);
    else if (property == "angular-drag") m.angular_drag = std::max(0.0_r, v);
    else if (property == "friction") m.friction = std::max(0.0_r, v);
    else if (property == "bounce") m.restitution = std::clamp(v, 0.0_r, 1.0_r);
    else if (property == "stiffness") m.stiffness = std::clamp(v, 0.0_r, 1.0_r);
    else if (property == "temperature") m.temperature = v;
    else if (property == "opacity") m.color.a = static_cast<float>(std::clamp(v, 0.0_r, 1.0_r));
    else return false;
    m.preset = fp::FluidPreset::custom;
    return true;
}

inline const char* fluid_properties() { return "density, viscosity, surface-tension, drag, quadratic-drag, angular-drag, friction, bounce, stiffness, temperature, opacity or wave-height"; }

inline fp::FluidColor fluid_color(R r, R g, R b, float a) {
    return fp::FluidColor{ static_cast<float>(std::clamp(r, 0.0_r, 1.0_r)), static_cast<float>(std::clamp(g, 0.0_r, 1.0_r)), static_cast<float>(std::clamp(b, 0.0_r, 1.0_r)), a };
}

class world {
public:
    world() : m_sim(std::make_unique<fp::Simulation3D>()) {}

    void set_gravity(R x, R y, R z) { m_sim->config().gravity = V3{ x, y, z }; }
    void set_time_step(R dt) { m_sim->config().fixed_dt = positive(dt, "the time step"); }
    void set_substeps(long long n) { m_sim->config().substeps = static_cast<int>(std::clamp<long long>(n, 1, 64)); }
    void set_iterations(long long velocity, long long position) {
        m_sim->config().velocity_iters = static_cast<int>(std::clamp<long long>(velocity, 1, 256));
        m_sim->config().position_iters = static_cast<int>(std::clamp<long long>(position, 0, 256));
    }
    void set_solver(const std::string& kind) {
        if (kind == "classic") m_sim->config().solver = fp::Solver3D::Classic;
        else if (kind == "soft" || kind == "soft-step") m_sim->config().solver = fp::Solver3D::SoftStep;
        else fail("the solver must be classic or soft, not '" + kind + "'");
    }
    void set_soft(long long substeps, R contact_hertz, R damping, R push_speed) {
        fp::Simulation3D::Config& c = m_sim->config();
        c.soft_substeps = static_cast<int>(std::clamp<long long>(substeps, 1, 32));
        c.contact_hertz = std::max(0.0_r, contact_hertz);
        c.contact_damping_ratio = std::max(0.0_r, damping);
        c.max_push_speed = std::max(0.0_r, push_speed);
    }
    void set_broadphase(const std::string& kind) {
        if (kind == "tree") m_sim->config().broadphase = fp::Broadphase3D::DynamicTree;
        else if (kind == "octree") m_sim->config().broadphase = fp::Broadphase3D::Octree;
        else fail("the broadphase must be tree or octree, not '" + kind + "'");
    }
    void set_threads(long long n) {
        m_sim->config().threads = static_cast<int>(std::clamp<long long>(n, 1, 64));
        if (m_fluids) m_fluids->set_threads(m_sim->config().threads);
    }
    void set_wide_solver(bool on) { m_sim->config().simd = on; }
    void set_deterministic(bool on) { m_sim->config().deterministic = on; }
    void set_continuous(bool on, bool everything) {
        m_sim->config().continuous = on;
        m_sim->config().continuous_all = on && everything;
    }
    void set_sleeping(bool on) { m_sim->config().allow_sleeping = on; }
    void set_reuse_contacts(bool on) { m_sim->config().reuse_contacts = on; }
    void set_event_callbacks(bool on) { m_sim->config().event_callbacks = on; }

    void step() {
        step_fluids(m_sim->config().fixed_dt);
        m_sim->step();
    }
    void step_by(R dt) {
        positive(dt, "the time step");
        step_fluids(dt);
        m_sim->step(dt);
    }

    long long add_box(R x, R y, R z, R hx, R hy, R hz, const std::string& kind) {
        return add(x, y, z, fp::Shape3D::make_box(positive(hx, "a half size"), positive(hy, "a half size"), positive(hz, "a half size")), kind);
    }
    long long add_sphere(R x, R y, R z, R r, const std::string& kind) {
        return add(x, y, z, fp::Shape3D::make_sphere(V3{}, positive(r, "a radius")), kind);
    }
    long long add_capsule(R x, R y, R z, R half_height, R r, const std::string& kind) {
        return add(x, y, z, fp::Shape3D::make_capsule(V3{ 0.0_r, -std::max(0.0_r, half_height), 0.0_r }, V3{ 0.0_r, std::max(0.0_r, half_height), 0.0_r }, positive(r, "a radius")), kind);
    }
    long long add_cylinder(R x, R y, R z, R half_height, R r, const std::string& kind) {
        return add(x, y, z, fp::Shape3D::make_cylinder(positive(half_height, "a half height"), positive(r, "a radius")), kind);
    }
    long long add_cone(R x, R y, R z, R half_height, R r, const std::string& kind) {
        return add(x, y, z, fp::Shape3D::make_cone(positive(half_height, "a half height"), positive(r, "a radius")), kind);
    }
    long long add_rounded_box(R x, R y, R z, R hx, R hy, R hz, R r, const std::string& kind) {
        const R lim = std::min({ hx, hy, hz });
        if (!(r >= 0.0_r) || r >= lim) fail("the rounding must be at least 0 and less than the smallest half size");
        return add(x, y, z, fp::Shape3D::make_rounded_box(positive(hx, "a half size"), positive(hy, "a half size"), positive(hz, "a half size"), r), kind);
    }
    long long add_rounded_box_core(R x, R y, R z, R hx, R hy, R hz, R r, const std::string& kind) {
        if (!(r >= 0.0_r)) fail("the rounding can't be below 0");
        return add(x, y, z, fp::Shape3D::make_rounded_box_core(positive(hx, "a half size"), positive(hy, "a half size"), positive(hz, "a half size"), r), kind);
    }
    long long add_ground(R height) {
        return add(0.0_r, 0.0_r, 0.0_r, fp::Shape3D::make_plane(V3{ 0.0_r, 1.0_r, 0.0_r }, height), "static");
    }
    long long add_terrain(R x, R y, R z, long long columns, long long rows, R cell, const std::string& heights) {
        if (columns < 2 || rows < 2) fail("a terrain needs at least 2 columns and 2 rows");
        std::vector<R> h;
        std::size_t pos = 0;
        while (pos <= heights.size()) {
            const std::size_t comma = heights.find(',', pos);
            const std::string part = heights.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
            if (!part.empty()) h.push_back(static_cast<R>(std::atof(part.c_str())));
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
        if (h.size() != static_cast<std::size_t>(columns * rows)) fail("a terrain of " + std::to_string(columns) + " by " + std::to_string(rows) + " needs " + std::to_string(columns * rows) + " heights, not " + std::to_string(h.size()));
        return add(x, y, z, fp::Shape3D::make_heightfield(h, static_cast<int>(columns), static_cast<int>(rows), positive(cell, "a cell size"), cell), "static");
    }

    long long add_mesh(R x, R y, R z, const std::string& points, const std::string& triangles, bool two_sided, const std::string& kind) {
        const std::vector<R> p = numbers(points);
        const std::vector<R> t = numbers(triangles);
        if (p.size() < 9 || p.size() % 3 != 0) fail("a mesh needs its points as x,y,z numbers, at least 3 points");
        if (t.empty() || t.size() % 3 != 0) fail("a mesh needs its triangles as 3 point numbers each");
        std::vector<V3> verts;
        for (std::size_t i = 0; i < p.size(); i += 3) verts.push_back(V3{ p[i], p[i + 1], p[i + 2] });
        std::vector<std::array<std::uint32_t, 3>> tris;
        for (std::size_t i = 0; i < t.size(); i += 3) {
            std::array<std::uint32_t, 3> tri{};
            for (std::size_t k = 0; k < 3; ++k) {
                const R v = t[i + k];
                if (!(v >= 0.0_r) || static_cast<std::size_t>(v) >= verts.size() || std::floor(v) != v) fail("triangle corner " + std::to_string(static_cast<long long>(v)) + " isn't one of the mesh's points");
                tri[k] = static_cast<std::uint32_t>(v);
            }
            tris.push_back(tri);
        }
        return add(x, y, z, fp::Shape3D::make_mesh(std::move(verts), std::move(tris), two_sided), kind);
    }
    long long add_hemisphere(R x, R y, R z, R r, const std::string& kind) {
        return add(x, y, z, fp::Shape3D::make_hemisphere(positive(r, "a radius")), kind);
    }
    long long add_sphere_section(R x, R y, R z, R r, R polar_min, R polar_max, R azimuth_min, R azimuth_max, R inner_radius, const std::string& kind) {
        fp::SphereSection3D s;
        s.radius = positive(r, "a radius");
        if (!(inner_radius >= 0.0_r) || inner_radius >= r) fail("the inner radius must be at least 0 and less than the radius");
        if (!(polar_min >= 0.0_r && polar_max <= 180.0_r && polar_min < polar_max)) fail("the polar range must go up from 0 to at most 180 degrees");
        if (!(azimuth_min < azimuth_max)) fail("the azimuth range must go up, like 0 to 360 degrees");
        s.polar_min = polar_min;
        s.polar_max = polar_max;
        s.azimuth_min = azimuth_min;
        s.azimuth_max = azimuth_max;
        s.inner_radius = inner_radius;
        if (s.is_full_sphere() || s.is_convex()) return add(x, y, z, fp::Shape3D::make_sphere_section(s), kind);
        return add_parts(x, y, z, fp::Shape3D::make_sphere_section_parts(s), kind);
    }

    void remove(long long id) {
        fp::RigidBody3D& b = body(id);
        for (std::size_t j = 0; j < m_joints.size(); ++j) {
            if (m_joints[j] && (m_joints[j]->body_a() == &b || m_joints[j]->body_b() == &b)) m_joints[j] = nullptr;
        }
        m_sim->remove_body(b);
        m_bodies[static_cast<std::size_t>(id)] = nullptr;
        m_colliders[static_cast<std::size_t>(id)] = nullptr;
    }
    bool exists(long long id) const { return id >= 0 && static_cast<std::size_t>(id) < m_bodies.size() && m_bodies[static_cast<std::size_t>(id)]; }
    long long body_count() const { return static_cast<long long>(m_sim->bodies().size()); }

    R x(long long id) const { return cbody(id).position().x; }
    R y(long long id) const { return cbody(id).position().y; }
    R z(long long id) const { return cbody(id).position().z; }
    R vx(long long id) const { return cbody(id).linear_velocity().x; }
    R vy(long long id) const { return cbody(id).linear_velocity().y; }
    R vz(long long id) const { return cbody(id).linear_velocity().z; }
    R qx(long long id) const { return cbody(id).orientation().x(); }
    R qy(long long id) const { return cbody(id).orientation().y(); }
    R qz(long long id) const { return cbody(id).orientation().z(); }
    R qw(long long id) const { return cbody(id).orientation().w(); }
    R mass(long long id) const { return cbody(id).mass(); }

    void set_position(long long id, R px, R py, R pz) {
        body(id).set_position(V3{ px, py, pz });
        body(id).wake();
    }
    void set_velocity(long long id, R x, R y, R z) {
        body(id).set_linear_velocity(V3{ x, y, z });
        body(id).wake();
    }
    void set_spin(long long id, R x, R y, R z) {
        body(id).set_angular_velocity(V3{ x, y, z });
        body(id).wake();
    }
    void push(long long id, R x, R y, R z) {
        fp::RigidBody3D& b = body(id);
        b.apply_linear_impulse(V3{ x, y, z });
        b.wake();
    }
    void set_damping(long long id, R linear, R angular) {
        body(id).set_linear_damping(std::max(0.0_r, linear));
        body(id).set_angular_damping(std::max(0.0_r, angular));
    }
    void set_gravity_scale(long long id, R s) { body(id).set_gravity_scale(s); }
    void set_density(long long id, R kg_per_m3) {
        fp::RigidBody3D& b = body(id);
        b.set_density(positive(kg_per_m3, "a density"));
        b.wake();
    }
    void set_mass(long long id, R kg) {
        fp::RigidBody3D& b = body(id);
        b.set_mass(positive(kg, "a mass"));
        b.wake();
    }
    void set_speed_caps(long long id, R speed, R spin) {
        fp::RigidBody3D& b = body(id);
        if (speed > 0.0_r) {
            b.set_max_linear_speed(speed);
            b.set_flag(fp::PhysicsFlags::VelocityClamping);
        }
        if (spin > 0.0_r) {
            b.set_max_angular_speed(spin);
            b.set_flag(fp::PhysicsFlags::AngularClamping);
        }
    }

    void set_friction(long long id, R f) { each_collider(id, [&](fp::Collider3D& c) { c.material.friction = std::max(0.0_r, f); }); }
    void set_bounce(long long id, R e) { each_collider(id, [&](fp::Collider3D& c) { c.material.restitution = std::clamp(e, 0.0_r, 1.0_r); }); }
    void set_rolling_friction(long long id, R f) { each_collider(id, [&](fp::Collider3D& c) { c.material.rolling_friction = std::max(0.0_r, f); }); }
    void set_spinning_friction(long long id, R f) { each_collider(id, [&](fp::Collider3D& c) { c.material.spinning_friction = std::max(0.0_r, f); }); }
    void set_grip(long long id, R gx, R gy, R gz) {
        each_collider(id, [&](fp::Collider3D& c) {
            fp::Material3D& m = c.material;
            m.anisotropic_x = std::max(0.0_r, gx);
            m.anisotropic_y = std::max(0.0_r, gy);
            m.anisotropic_z = std::max(0.0_r, gz);
        });
    }
    void set_sensor(long long id, bool on) { each_collider(id, [&](fp::Collider3D& c) { c.is_sensor = on; }); }

    void set_layer(long long id, long long layer) { body(id).set_collision_layer(static_cast<std::uint64_t>(layer)); }
    void set_mask(long long id, long long mask) { body(id).set_collision_mask(static_cast<std::uint64_t>(mask)); }
    long long layer(long long id) const { return static_cast<long long>(cbody(id).collision_layer()); }
    long long mask(long long id) const { return static_cast<long long>(cbody(id).collision_mask()); }
    bool can_collide(long long a, long long b) const { return m_sim->can_collide(cbody(a), cbody(b)); }

    void set_quality(long long id, const std::string& q) { body(id).set_quality(quality_named(q)); }
    void set_iterations_for(long long id, long long velocity, long long position) {
        body(id).set_velocity_iterations(static_cast<int>(std::clamp<long long>(velocity, 0, 256)));
        body(id).set_position_iterations(static_cast<int>(std::clamp<long long>(position, 0, 256)));
    }
    void set_fast_moves(long long id, const std::string& mode) { body(id).set_continuous_mode(continuous_named(mode)); }
    void set_sleep(long long id, R speed, R spin, R delay) {
        body(id).set_sleep_tolerances(std::max(0.0_r, speed), std::max(0.0_r, spin));
        body(id).set_sleep_time(std::max(0.0_r, delay));
    }
    bool awake(long long id) const { return cbody(id).is_awake(); }
    void wake(long long id) { body(id).wake(); }
    void set_flag(long long id, const std::string& name, bool on) {
        fp::RigidBody3D& b = body(id);
        const fp::PhysicsFlags f = flag_named(name);
        b.set_flag_to(f, on);
        b.wake();
    }
    bool flag(long long id, const std::string& name) const { return has_flag(cbody(id).flags(), flag_named(name)); }

    bool raycast(R ox, R oy, R oz, R dx, R dy, R dz, R max_distance, long long layers) {
        fp::QueryFilter3D f;
        f.layers = static_cast<std::uint64_t>(layers);
        const auto hit = m_sim->raycast(V3{ ox, oy, oz }, V3{ dx, dy, dz }, max_distance, f);
        m_hit = hit ? *hit : fp::RayHit3D{};
        m_has_hit = hit.has_value();
        return m_has_hit;
    }
    long long hit_body() const { return m_has_hit ? id_of(m_hit.body) : -1; }
    R hit_distance() const { return m_has_hit ? m_hit.distance : 0.0_r; }
    R hit_x() const { return m_hit.point.x; }
    R hit_y() const { return m_hit.point.y; }
    R hit_z() const { return m_hit.point.z; }
    R hit_nx() const { return m_hit.normal.x; }
    R hit_ny() const { return m_hit.normal.y; }
    R hit_nz() const { return m_hit.normal.z; }

    long long overlap_sphere(R x, R y, R z, R r, long long layers) {
        fp::QueryFilter3D f;
        f.layers = static_cast<std::uint64_t>(layers);
        f.include_sensors = true;
        m_found.clear();
        for (fp::RigidBody3D* b : m_sim->overlap_sphere(V3{ x, y, z }, positive(r, "a radius"), f)) m_found.push_back(id_of(b));
        return static_cast<long long>(m_found.size());
    }
    long long overlap_box(R x, R y, R z, R hx, R hy, R hz, long long layers) {
        fp::QueryFilter3D f;
        f.layers = static_cast<std::uint64_t>(layers);
        f.include_sensors = true;
        m_found.clear();
        for (fp::RigidBody3D* b : m_sim->overlap_box(V3{ x, y, z }, V3{ positive(hx, "a half size"), positive(hy, "a half size"), positive(hz, "a half size") }, fp::RQuat(1.0_r, 0.0_r, 0.0_r, 0.0_r), f)) m_found.push_back(id_of(b));
        return static_cast<long long>(m_found.size());
    }
    long long found(long long i) const {
        if (i < 0 || static_cast<std::size_t>(i) >= m_found.size()) fail("found index " + std::to_string(i) + " is out of range (there are " + std::to_string(m_found.size()) + ")");
        return m_found[static_cast<std::size_t>(i)];
    }
    R sphere_cast(R x, R y, R z, R r, R dx, R dy, R dz, long long layers) {
        fp::QueryFilter3D f;
        f.layers = static_cast<std::uint64_t>(layers);
        const auto hit = m_sim->shape_cast(fp::Shape3D::make_sphere(V3{}, positive(r, "a radius")), fp::Transform3D(V3{ x, y, z }, fp::RQuat(1.0_r, 0.0_r, 0.0_r, 0.0_r)), V3{ dx, dy, dz }, f);
        if (!hit) {
            m_has_hit = false;
            return 1.0_r;
        }
        m_has_hit = true;
        m_hit = fp::RayHit3D{};
        m_hit.body = hit->body;
        m_hit.point = hit->point;
        m_hit.normal = hit->normal;
        m_hit.fraction = hit->fraction;
        m_hit.distance = hit->fraction * std::sqrt(dx * dx + dy * dy + dz * dz);
        return hit->fraction;
    }

    long long add_joint(const std::string& kind, long long a, long long b, R ax, R ay, R az, R dx, R dy, R dz) {
        fp::RigidBody3D& ba = body(a);
        fp::RigidBody3D* bb = b < 0 ? nullptr : &body(b);
        const V3 anchor{ ax, ay, az }, axis{ dx, dy, dz };
        fp::Joint3D* j = nullptr;
        if (kind == "ball") j = &m_sim->add_ball_joint(ba, bb, anchor);
        else if (kind == "hinge") j = &m_sim->add_hinge_joint(ba, bb, anchor, axis);
        else if (kind == "slider") j = &m_sim->add_slider_joint(ba, bb, anchor, axis);
        else if (kind == "fixed") j = &m_sim->add_fixed_joint(ba, bb);
        else if (kind == "cone-twist") j = &m_sim->add_cone_twist_joint(ba, bb, anchor, axis, 0.7_r, -0.5_r, 0.5_r);
        else if (kind == "distance") j = &m_sim->add_distance_joint(ba, bb, ba.position(), bb ? bb->position() : anchor);
        else fail("a joint kind must be ball, hinge, slider, fixed, cone-twist or distance, not '" + kind + "'");
        m_joints.push_back(j);
        return static_cast<long long>(m_joints.size()) - 1;
    }
    long long add_spring(long long a, long long b, R ax, R ay, R az, R rest, R hertz, R damping) {
        fp::RigidBody3D& ba = body(a);
        fp::RigidBody3D* bb = b < 0 ? nullptr : &body(b);
        m_joints.push_back(&m_sim->add_spring_joint(ba, bb, ba.position(), V3{ ax, ay, az }, std::max(0.0_r, rest), positive(hertz, "a spring's hertz"), std::max(0.0_r, damping)));
        return static_cast<long long>(m_joints.size()) - 1;
    }
    void set_motor(long long j, bool on, R speed, R max_force) { joint(j).set_motor(on, speed, std::max(0.0_r, max_force)); }
    void set_limits(long long j, bool on, R lower, R upper) {
        if (on && upper < lower) fail("a joint's upper limit can't be below its lower limit");
        joint(j).set_limits(on, lower, upper);
    }
    void set_joint_spring(long long j, R hertz, R damping, R target) { joint(j).set_spring(std::max(0.0_r, hertz), std::max(0.0_r, damping), target); }
    void set_break_force(long long j, R force, R torque) {
        fp::JointDef3D& d = joint(j).def_mut();
        d.break_force = force;
        d.break_torque = torque;
    }
    R joint_angle(long long j) const { return cjoint(j).angle(); }
    R joint_translation(long long j) const { return cjoint(j).translation(); }
    bool joint_broken(long long j) const { return cjoint(j).broken(); }
    void remove_joint(long long j) {
        m_sim->remove_joint(joint(j));
        m_joints[static_cast<std::size_t>(j)] = nullptr;
    }

    long long begin_count() const { return static_cast<long long>(m_sim->begin_events().size()); }
    long long end_count() const { return static_cast<long long>(m_sim->end_events().size()); }
    long long begin_a(long long i) const { return id_of(event(m_sim->begin_events(), i).body_a); }
    long long begin_b(long long i) const { return id_of(event(m_sim->begin_events(), i).body_b); }
    long long end_a(long long i) const { return id_of(event(m_sim->end_events(), i).body_a); }
    long long end_b(long long i) const { return id_of(event(m_sim->end_events(), i).body_b); }

    long long add_cloth(R x, R y, R z, long long columns, long long rows, R spacing, R mass) {
        if (columns < 1 || rows < 1 || columns * rows > 1000000) fail("a cloth needs 1 to a million points");
        fp::ClothDef3D d;
        d.columns = static_cast<int>(columns);
        d.rows = static_cast<int>(rows);
        d.spacing = positive(spacing, "the spacing");
        d.mass = positive(mass, "the mass");
        d.origin = V3{ x, y, z };
        m_cloths.push_back(&m_sim->add_cloth(d));
        return static_cast<long long>(m_cloths.size()) - 1;
    }
    long long add_cloth_sheet(R x, R y, R z, R width, R height, long long columns, long long rows, R mass) {
        if (columns < 2 || rows < 2 || columns * rows > 1000000) fail("a cloth sheet needs at least 2 by 2 points and at most a million");
        fp::ClothDef3D d = fp::ClothDef3D::sheet(V3{ x, y, z }, V3{ 1.0_r, 0.0_r, 0.0_r }, V3{ 0.0_r, 0.0_r, 1.0_r }, positive(width, "the width"), positive(height, "the height"), static_cast<int>(columns), static_cast<int>(rows));
        d.mass = positive(mass, "the mass");
        m_cloths.push_back(&m_sim->add_cloth(d));
        return static_cast<long long>(m_cloths.size()) - 1;
    }
    long long add_cloth_triangles(R x, R y, R z, R width, R height, long long triangles, R mass) {
        if (triangles < 2 || triangles > 2000000) fail("a cloth sheet needs 2 to two million triangles");
        fp::ClothDef3D d = fp::ClothDef3D::sheet_with_triangles(V3{ x, y, z }, V3{ 1.0_r, 0.0_r, 0.0_r }, V3{ 0.0_r, 0.0_r, 1.0_r }, positive(width, "the width"), positive(height, "the height"), static_cast<std::size_t>(triangles));
        d.mass = positive(mass, "the mass");
        m_cloths.push_back(&m_sim->add_cloth(d));
        return static_cast<long long>(m_cloths.size()) - 1;
    }
    long long triangle_count(long long c) { return static_cast<long long>(cloth(c).triangle_count()); }
    long long cloth_columns(long long c) { return static_cast<long long>(cloth(c).columns()); }
    long long cloth_rows(long long c) { return static_cast<long long>(cloth(c).rows()); }
    long long add_rope(R x, R y, R z, R dx, R dy, R dz, long long segments, R length) {
        if (segments < 1) fail("a rope needs at least 1 segment");
        m_cloths.push_back(&m_sim->add_rope(V3{ x, y, z }, V3{ dx, dy, dz }, static_cast<int>(segments), positive(length, "the length")));
        return static_cast<long long>(m_cloths.size()) - 1;
    }
    void set_cloth_stiffness(long long c, R stretch, R shear, R bend) {
        fp::ClothDef3D& d = cloth(c).edit_def();
        d.stretch_compliance = std::max(0.0_r, stretch);
        d.shear_compliance = std::max(0.0_r, shear);
        d.bend_compliance = std::max(0.0_r, bend);
    }
    void set_cloth_air(long long c, R drag, R lift, R wx, R wy, R wz) {
        fp::ClothDef3D& d = cloth(c).edit_def();
        d.drag = std::max(0.0_r, drag);
        d.lift = std::max(0.0_r, lift);
        d.wind = V3{ wx, wy, wz };
    }
    void pin_point(long long c, long long i) { cloth(c).pin(point(c, i)); }
    void unpin_point(long long c, long long i) { cloth(c).unpin(point(c, i)); }
    void attach_point(long long c, long long i, long long body_id) { cloth(c).attach(point(c, i), body(body_id)); }
    long long point_count(long long c) { return static_cast<long long>(cloth(c).particle_count()); }
    long long point_at(long long c, long long column, long long row) {
        fp::Cloth3D& cl = cloth(c);
        if (column < 0 || row < 0 || column >= cl.columns() || row >= cl.rows()) fail("that point is outside the cloth");
        return static_cast<long long>(cl.index(static_cast<int>(column), static_cast<int>(row)));
    }
    R point_x(long long c, long long i) { return cloth(c).position(point(c, i)).x; }
    R point_y(long long c, long long i) { return cloth(c).position(point(c, i)).y; }
    R point_z(long long c, long long i) { return cloth(c).position(point(c, i)).z; }
    void remove_cloth(long long c) {
        m_sim->remove_cloth(cloth(c));
        m_cloths[static_cast<std::size_t>(c)] = nullptr;
    }

    void set_rebase_distance(R meters) { m_sim->config().rebase_distance = std::max(0.0_r, meters); }
    void set_focus(R x, R y, R z) { m_sim->set_focus(V3{ x, y, z }); }
    R origin_x() const { return m_sim->origin().x; }
    R origin_y() const { return m_sim->origin().y; }
    R origin_z() const { return m_sim->origin().z; }

    void shift_origin(R x, R y, R z) { m_sim->shift_origin(V3{ x, y, z }); }
    long long awake_count() const { return static_cast<long long>(m_sim->stats().awake); }
    long long pair_count() const { return static_cast<long long>(m_sim->stats().pairs); }
    long long island_count() const { return static_cast<long long>(m_sim->stats().islands); }
    long long layer_rejects() const { return static_cast<long long>(m_sim->stats().layer_rejects); }
    long long reused_contacts() const { return static_cast<long long>(m_sim->stats().reused_manifolds); }

    long long add_fluid(const std::string& level, R x, R y, R z, R hx, R hy, R hz, const std::string& preset, R fill, R cell) {
        const fp::FluidLevel lv = fluid_level_named(level);
        const fp::FluidMaterial mat = fluid_preset_named(preset);
        positive(hx, "a half size");
        positive(hy, "a half size");
        positive(hz, "a half size");
        if (!(fill >= 0.0_r && fill <= 1.0_r)) fail("a fluid's fill must be from 0 to 1");
        if (!(cell >= 0.0_r) || !std::isfinite(cell)) fail("a fluid's cell size must be 0 (pick one) or above");
        fp::FluidVolumeDef def = fp::FluidVolumeDef::box(lv, fp::AABB3D(V3{ x - hx, y - hy, z - hz }, V3{ x + hx, y + hy, z + hz }), mat, fill);
        const R longest = std::max({ hx, hy, hz }) * 2.0_r;
        if (cell > 0.0_r) {
            def.cell_size = cell;
            def.particles.radius = cell * 0.5_r;
        } else {
            def.cell_size = std::clamp(longest / 40.0_r, 0.1_r, 1.0_r);
            def.particles.radius = std::clamp(longest / 80.0_r, 0.04_r, 0.25_r);
            if (lv == fp::FluidLevel::blocky) def.cell_size = std::max(def.cell_size, 0.5_r);
        }
        if (lv == fp::FluidLevel::blocky || lv == fp::FluidLevel::flowing) {
            const double cells = std::ceil(2.0 * hx / def.cell_size) * std::ceil(2.0 * hy / def.cell_size) * std::ceil(2.0 * hz / def.cell_size);
            if (cells > 16.0e6) fail("that fluid would have " + std::to_string(static_cast<long long>(cells)) + " cells; give it a bigger cell size (at most 16 million cells)");
        }
        fp::FluidVolume3D& v = fluids().add_volume(def);
        m_volumes.push_back(&v);
        m_voxelized.push_back(m_static_version);
        if (voxelizes(lv)) v.voxelize(*m_sim);
        return static_cast<long long>(m_volumes.size()) - 1;
    }
    void set_fluid(long long id, const std::string& property, R value) {
        fp::FluidVolume3D& v = volume(id);
        if (property == "wave-height") {
            fp::FluidVolumeDef& d = v.edit_def();
            d.waves.swell_amplitude = std::max(0.0_r, value);
            d.still.bob_amplitude = std::max(0.0_r, value);
            return;
        }
        fp::FluidMaterial m = v.material();
        if (!edit_fluid_material(m, property, value)) fail("a fluid property must be " + std::string(fluid_properties()) + ", not '" + property + "'");
        v.set_material(m);
    }
    void set_fluid_color(long long id, R r, R g, R b) {
        fp::FluidVolume3D& v = volume(id);
        fp::FluidMaterial m = v.material();
        m.color = fluid_color(r, g, b, m.color.a);
        v.set_material(m);
    }
    void set_fluid_current(long long id, R x, R y, R z) {
        fp::FluidVolume3D& v = volume(id);
        v.set_current(V3{ x, y, z });
        v.wake();
    }
    void set_fluid_quality(long long q) {
        m_fluid_quality = static_cast<long long>(fluid_quality_of(q));
        if (m_fluids) m_fluids->set_quality(static_cast<fp::FluidQuality>(m_fluid_quality));
    }
    void remove_fluid(long long id) {
        fp::FluidVolume3D& v = volume(id);
        m_fluids->remove_volume(v);
        m_volumes[static_cast<std::size_t>(id)] = nullptr;
    }
    bool fluid_exists(long long id) const { return id >= 0 && static_cast<std::size_t>(id) < m_volumes.size() && m_volumes[static_cast<std::size_t>(id)]; }
    long long fluid_count() const { return m_fluids ? static_cast<long long>(m_fluids->volume_count()) : 0; }
    std::string fluid_level(long long id) const { return fluid_level_name(cvolume(id).level()); }
    R fluid_surface(R x, R y, R z) const { return m_fluids ? m_fluids->surface_height(V3{ x, y, z }) : -std::numeric_limits<R>::max(); }
    R fluid_depth(R x, R y, R z) const { return m_fluids ? m_fluids->depth_at(V3{ x, y, z }) : -std::numeric_limits<R>::max(); }
    bool fluid_inside(R x, R y, R z) const { return m_fluids && m_fluids->inside(V3{ x, y, z }); }
    R fluid_velocity_x(R x, R y, R z) const { return m_fluids ? m_fluids->velocity_at(V3{ x, y, z }).x : 0.0_r; }
    R fluid_velocity_y(R x, R y, R z) const { return m_fluids ? m_fluids->velocity_at(V3{ x, y, z }).y : 0.0_r; }
    R fluid_velocity_z(R x, R y, R z) const { return m_fluids ? m_fluids->velocity_at(V3{ x, y, z }).z : 0.0_r; }
    R fluid_volume(long long id) const { return cvolume(id).total_volume(); }
    bool fluid_sleeping(long long id) const { return cvolume(id).sleeping(); }

    long long fluid_particle_count(long long id) const { return static_cast<long long>(cvolume(id).particle_count()); }
    R fluid_particle_x(long long id, long long i) const { return particle(id, i).x; }
    R fluid_particle_y(long long id, long long i) const { return particle(id, i).y; }
    R fluid_particle_z(long long id, long long i) const { return particle(id, i).z; }
    R fluid_particle_radius(long long id) const { return cvolume(id).particle_radius(); }
    long long fluid_cells_x(long long id) const { return cvolume(id).cells_x(); }
    long long fluid_cells_y(long long id) const { return cvolume(id).cells_y(); }
    long long fluid_cells_z(long long id) const { return cvolume(id).cells_z(); }
    R fluid_cell_size(long long id) const { return cvolume(id).cell_size(); }
    R fluid_cell(long long id, long long i, long long j, long long k) const {
        const fp::FluidVolume3D& v = cvolume(id);
        const std::size_t c = cell_at(v, i, j, k);
        return c < v.cell_fill().size() ? v.cell_fill()[c] : 0.0_r;
    }
    bool fluid_solid(long long id, long long i, long long j, long long k) const {
        const fp::FluidVolume3D& v = cvolume(id);
        cell_at(v, i, j, k);
        return v.solid(static_cast<int>(i), static_cast<int>(j), static_cast<int>(k));
    }
    long long fluid_columns_x(long long id) const { return cvolume(id).columns_x(); }
    long long fluid_columns_z(long long id) const { return cvolume(id).columns_z(); }
    R fluid_column_size(long long id) const { return cvolume(id).column_size(); }
    R fluid_height(long long id, long long i, long long k) const {
        const fp::FluidVolume3D& v = cvolume(id);
        if (i < 0 || k < 0 || i >= v.columns_x() || k >= v.columns_z()) fail("column (" + std::to_string(i) + ", " + std::to_string(k) + ") is outside the fluid's " + std::to_string(v.columns_x()) + " by " + std::to_string(v.columns_z()) + " columns");
        const std::vector<R>& h = v.heights();
        const std::size_t c = v.column_index(static_cast<int>(i), static_cast<int>(k));
        return c < h.size() ? h[c] : 0.0_r;
    }

private:
    static bool voxelizes(fp::FluidLevel lv) { return lv == fp::FluidLevel::blocky || lv == fp::FluidLevel::flowing || lv == fp::FluidLevel::waves; }
    fp::FluidWorld3D& fluids() {
        if (!m_fluids) {
            fp::FluidWorld3D::Config cfg;
            cfg.gravity = m_sim->config().gravity;
            cfg.fixed_dt = m_sim->config().fixed_dt;
            cfg.threads = std::max(1, m_sim->config().threads);
            cfg.quality = static_cast<fp::FluidQuality>(m_fluid_quality);
            m_fluids = std::make_unique<fp::FluidWorld3D>(cfg);
            m_fluids->set_quality(cfg.quality);
            m_fluids->set_threads(cfg.threads);
        }
        m_fluids->set_gravity(m_sim->config().gravity);
        return *m_fluids;
    }
    void step_fluids(R dt) {
        if (!m_fluids) return;
        for (std::size_t i = 0; i < m_volumes.size(); ++i) {
            if (!m_volumes[i] || m_voxelized[i] == m_static_version) continue;
            m_voxelized[i] = m_static_version;
            if (voxelizes(m_volumes[i]->level())) m_volumes[i]->voxelize(*m_sim);
        }
        m_fluids->set_gravity(m_sim->config().gravity);
        m_fluids->step(*m_sim, dt);
    }
    fp::FluidVolume3D& volume(long long id) {
        if (!fluid_exists(id)) fail("there's no fluid " + std::to_string(id));
        return *m_volumes[static_cast<std::size_t>(id)];
    }
    const fp::FluidVolume3D& cvolume(long long id) const {
        if (!fluid_exists(id)) fail("there's no fluid " + std::to_string(id));
        return *m_volumes[static_cast<std::size_t>(id)];
    }
    const V3& particle(long long id, long long i) const {
        const fp::FluidVolume3D& v = cvolume(id);
        if (i < 0 || static_cast<std::size_t>(i) >= v.particle_count()) fail("fluid particle " + std::to_string(i) + " is out of range (there are " + std::to_string(v.particle_count()) + ")");
        return v.positions()[static_cast<std::size_t>(i)];
    }
    static std::size_t cell_at(const fp::FluidVolume3D& v, long long i, long long j, long long k) {
        if (!v.valid_cell(static_cast<int>(i), static_cast<int>(j), static_cast<int>(k)) || i > 1000000 || j > 1000000 || k > 1000000) fail("cell (" + std::to_string(i) + ", " + std::to_string(j) + ", " + std::to_string(k) + ") is outside the fluid's " + std::to_string(v.cells_x()) + " by " + std::to_string(v.cells_y()) + " by " + std::to_string(v.cells_z()) + " cells");
        return v.cell_index(static_cast<int>(i), static_cast<int>(j), static_cast<int>(k));
    }
    long long add(R x, R y, R z, const fp::Shape3D& shape, const std::string& kind) {
        fp::RigidBody3D& b = m_sim->add_body(V3{ x, y, z }, kind_flags(kind));
        fp::Collider3D& c = b.add_collider(shape);
        if (kind == "static" || kind == "fixed") ++m_static_version;
        m_bodies.push_back(&b);
        m_colliders.push_back(&c);
        return static_cast<long long>(m_bodies.size()) - 1;
    }
    long long add_parts(R x, R y, R z, const std::vector<fp::Shape3D>& parts, const std::string& kind) {
        if (parts.empty()) fail("that sphere section has nothing left in it");
        fp::RigidBody3D& b = m_sim->add_body(V3{ x, y, z }, kind_flags(kind));
        for (const auto& p : parts) b.add_collider(p);
        if (kind == "static" || kind == "fixed") ++m_static_version;
        m_bodies.push_back(&b);
        m_colliders.push_back(&b.colliders_mut()[0]);
        return static_cast<long long>(m_bodies.size()) - 1;
    }
    template <class F>
    void each_collider(long long id, F&& f) {
        for (auto& c : body(id).colliders_mut()) f(c);
    }
    fp::RigidBody3D& body(long long id) {
        if (!exists(id)) fail("there's no body " + std::to_string(id));
        return *m_bodies[static_cast<std::size_t>(id)];
    }
    const fp::RigidBody3D& cbody(long long id) const {
        if (!exists(id)) fail("there's no body " + std::to_string(id));
        return *m_bodies[static_cast<std::size_t>(id)];
    }
    fp::Collider3D& collider(long long id) {
        body(id);
        return *m_colliders[static_cast<std::size_t>(id)];
    }
    fp::Joint3D& joint(long long j) {
        if (j < 0 || static_cast<std::size_t>(j) >= m_joints.size() || !m_joints[static_cast<std::size_t>(j)]) fail("there's no joint " + std::to_string(j));
        return *m_joints[static_cast<std::size_t>(j)];
    }
    const fp::Joint3D& cjoint(long long j) const {
        if (j < 0 || static_cast<std::size_t>(j) >= m_joints.size() || !m_joints[static_cast<std::size_t>(j)]) fail("there's no joint " + std::to_string(j));
        return *m_joints[static_cast<std::size_t>(j)];
    }
    long long id_of(const fp::RigidBody3D* b) const {
        for (std::size_t i = 0; i < m_bodies.size(); ++i) {
            if (m_bodies[i] == b) return static_cast<long long>(i);
        }
        return -1;
    }
    static std::vector<R> numbers(const std::string& text) {
        std::vector<R> out;
        std::size_t pos = 0;
        while (pos <= text.size()) {
            const std::size_t comma = text.find(',', pos);
            const std::string part = text.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
            if (part.find_first_not_of(" \t\r\n") != std::string::npos) out.push_back(static_cast<R>(std::atof(part.c_str())));
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
        return out;
    }
    fp::Cloth3D& cloth(long long c) {
        if (c < 0 || static_cast<std::size_t>(c) >= m_cloths.size() || !m_cloths[static_cast<std::size_t>(c)]) fail("there's no cloth " + std::to_string(c));
        return *m_cloths[static_cast<std::size_t>(c)];
    }
    std::size_t point(long long c, long long i) {
        if (i < 0 || static_cast<std::size_t>(i) >= cloth(c).particle_count()) fail("point " + std::to_string(i) + " is out of range (there are " + std::to_string(cloth(c).particle_count()) + ")");
        return static_cast<std::size_t>(i);
    }
    static const fp::ContactEvent3D& event(const std::vector<fp::ContactEvent3D>& list, long long i) {
        if (i < 0 || static_cast<std::size_t>(i) >= list.size()) fail("event " + std::to_string(i) + " is out of range (there are " + std::to_string(list.size()) + ")");
        return list[static_cast<std::size_t>(i)];
    }

    std::unique_ptr<fp::Simulation3D> m_sim;
    std::unique_ptr<fp::FluidWorld3D> m_fluids;
    std::vector<fp::FluidVolume3D*> m_volumes;
    std::vector<unsigned long long> m_voxelized;
    unsigned long long m_static_version = 0;
    long long m_fluid_quality = 1;
    std::vector<fp::RigidBody3D*> m_bodies;
    std::vector<fp::Collider3D*> m_colliders;
    std::vector<fp::Joint3D*> m_joints;
    std::vector<fp::Cloth3D*> m_cloths;
    std::vector<long long> m_found;
    fp::RayHit3D m_hit;
    bool m_has_hit = false;
};

} // namespace physics
} // namespace walnut_rt

#endif // WALNUT_PHYSICS_HPP
