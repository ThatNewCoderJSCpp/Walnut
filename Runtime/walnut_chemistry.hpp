#ifndef WALNUT_CHEMISTRY_HPP
#define WALNUT_CHEMISTRY_HPP

#include "walnut_runtime.hpp"
#include "fizmo/../chemistry.hpp"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace walnut_rt {
namespace chemistry {

namespace fc = fizmo::chemistry;

struct api {};

inline const fc::Element& element_of(const std::string& text) {
    const fc::Element* e = fc::PeriodicTable::find(text);
    if (!e) fail("there's no element called '" + text + "'");
    return *e;
}

inline const fc::Element& element_of(long long z) {
    if (z < 1 || z > fc::ELEMENT_COUNT) fail("there's no element with atomic number " + std::to_string(z));
    return fc::PeriodicTable::element(static_cast<int>(z));
}

inline std::size_t index_in(long long i, std::size_t size, const char* what) {
    if (i < 0 || static_cast<std::size_t>(i) >= size) fail(std::string(what) + " " + std::to_string(i) + " is out of range (there are " + std::to_string(size) + ")");
    return static_cast<std::size_t>(i);
}

inline fc::BondOrder order_of(long long o) {
    switch (o) {
        case 1: return fc::BondOrder::Single;
        case 2: return fc::BondOrder::Double;
        case 3: return fc::BondOrder::Triple;
        case 4: return fc::BondOrder::Aromatic;
        default: fail("bond order must be 1, 2, 3 or 4 (aromatic)");
    }
}

inline std::string join(const std::vector<std::string>& parts, const std::string& sep) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) out += (i ? sep : std::string()) + parts[i];
    return out;
}

class element {
public:
    element() : m_e(&fc::PeriodicTable::element(1)) {}
    explicit element(long long z) : m_e(&element_of(z)) {}
    explicit element(const std::string& symbol_or_name) : m_e(&element_of(symbol_or_name)) {}

    long long atomic_number() const { return m_e->atomic_number; }
    std::string symbol() const { return m_e->symbol; }
    std::string name() const { return m_e->name; }
    long long group() const { return m_e->group; }
    long long period() const { return m_e->period; }
    std::string block() const { return m_e->block_name(); }
    std::string category() const { return m_e->category_name(); }
    std::string classification() const { return m_e->classification_name(); }
    bool is_metal() const { return m_e->is_metal(); }
    bool is_metalloid() const { return m_e->is_metalloid(); }
    bool is_nonmetal() const { return m_e->is_nonmetal(); }
    bool is_stable() const { return m_e->is_stable; }
    bool is_noble_gas() const { return m_e->is_noble_gas(); }
    double molar_mass() const { return m_e->atomic_weight; }
    double molar_mass_uncertainty() const { return m_e->atomic_weight_uncertainty; }
    long long mass_number() const { return m_e->mass_number(); }
    long long neutrons() const { return m_e->neutrons(); }
    double electronegativity() const { return m_e->electronegativity_pauling; }
    double electron_affinity() const { return m_e->electron_affinity; }
    double ionization_energy(long long n) const { return m_e->ionization_energy(static_cast<int>(n)); }
    long long ionization_energy_count() const { return static_cast<long long>(m_e->ionization_energies().size()); }
    double covalent_radius() const { return m_e->covalent_radius; }
    double atomic_radius() const { return m_e->atomic_radius; }
    double van_der_waals_radius() const { return m_e->van_der_waals_radius; }
    double metallic_radius() const { return m_e->metallic_radius; }
    double ionic_radius(long long charge, long long coordination) const {
        const fc::IonicRadius* r = m_e->ionic_radius(static_cast<int>(charge), static_cast<int>(coordination));
        return r ? r->ionic_radius : fc::NO_VALUE;
    }
    double melting_point() const { return m_e->melting_point; }
    double boiling_point() const { return m_e->boiling_point; }
    double density() const { return m_e->density; }
    double atomization_enthalpy() const { return m_e->atomization_enthalpy; }
    std::string standard_state() const { return fc::to_string(m_e->standard_state); }
    std::string electron_configuration() const { return m_e->electron_configuration; }
    std::string full_electron_configuration() const { return m_e->full_electron_configuration(); }
    long long valence_electrons() const { return m_e->valence_electrons(); }
    long long unpaired_electrons() const { return m_e->unpaired_electrons(); }
    long long most_common_oxidation_state() const { return m_e->most_common_oxidation_state(); }
    std::string oxidation_states() const {
        std::vector<std::string> parts;
        for (const fc::OxidationStateEntry& s : m_e->oxidation_states()) parts.push_back(fc::format_signed(s.state) + (s.common ? "*" : ""));
        return join(parts, " ");
    }
    std::string crystal_structure() const { return m_e->crystal_structure; }
    long long isotope_count() const { return static_cast<long long>(m_e->isotopes().size()); }
    long long isotope_mass_number(long long i) const { return m_e->isotopes()[index_in(i, m_e->isotopes().size(), "isotope")].mass_number; }
    double isotope_mass(long long i) const { return m_e->isotopes()[index_in(i, m_e->isotopes().size(), "isotope")].mass; }
    double isotope_abundance(long long i) const { return m_e->isotopes()[index_in(i, m_e->isotopes().size(), "isotope")].abundance; }
    double isotope_half_life(long long i) const { return m_e->isotopes()[index_in(i, m_e->isotopes().size(), "isotope")].half_life; }
    bool isotope_is_stable(long long i) const { return m_e->isotopes()[index_in(i, m_e->isotopes().size(), "isotope")].is_stable; }
    std::string to_string() const { return std::string(m_e->symbol) + " (" + m_e->name + ", Z = " + std::to_string(m_e->atomic_number) + ")"; }

    const fc::Element& raw() const { return *m_e; }

private:
    const fc::Element* m_e;
};

class formula {
public:
    formula() = default;
    explicit formula(const std::string& text) {
        try {
            m_f = fc::Formula::parse(text);
        } catch (const std::exception& ex) {
            fail(ex.what());
        }
    }

    double molar_mass() const { return m_f.molar_mass(); }
    long long charge() const { return m_f.charge(); }
    double atom_count() const { return m_f.atom_count(); }
    long long element_count() const { return m_f.element_count(); }
    double count(const std::string& symbol) const { return m_f.count(element_of(symbol).atomic_number); }
    double mass_percent(const std::string& symbol) const { return m_f.mass_percent(element_of(symbol).atomic_number); }
    double electrons() const { return m_f.electrons(); }
    double degree_of_unsaturation() const { return m_f.degree_of_unsaturation(); }
    bool is_ion() const { return m_f.is_ion(); }
    bool is_organic() const { return m_f.is_organic(); }
    std::string phase() const { return fc::to_string(m_f.phase()); }
    std::string to_string() const { return m_f.display_string(false); }
    std::string unicode() const { return m_f.display_string(true); }
    std::string hill() const { return m_f.hill(); }
    std::string flat() const { return m_f.to_string(); }
    formula empirical() const {
        formula out;
        out.m_f = m_f.empirical();
        return out;
    }
    std::string name() const { return fc::name_compound(m_f.display_string(false), m_f.phase()).name; }
    double moles(double grams) const { return m_f.moles(grams); }
    double grams(double moles) const { return m_f.grams(moles); }

    const fc::Formula& raw() const { return m_f; }

private:
    fc::Formula m_f;
};

class reaction {
public:
    reaction() = default;
    explicit reaction(const std::string& equation) {
        try {
            m_r = fc::Reaction::parse(equation);
        } catch (const std::exception& ex) {
            fail(ex.what());
        }
    }

    void balance() {
        try {
            m_r.balance();
        } catch (const std::exception& ex) {
            fail(ex.what());
        }
    }
    bool is_balanced() const { return m_r.is_balanced(); }
    bool is_reversible() const { return m_r.reversible(); }
    bool is_redox() const { return m_r.is_redox(); }
    std::string type() const { return fc::to_string(m_r.classify()); }
    long long reactant_count() const { return static_cast<long long>(m_r.reactants().size()); }
    long long product_count() const { return static_cast<long long>(m_r.products().size()); }
    double reactant_coefficient(long long i) const { return m_r.reactants()[index_in(i, m_r.reactants().size(), "reactant")].coefficient; }
    double product_coefficient(long long i) const { return m_r.products()[index_in(i, m_r.products().size(), "product")].coefficient; }
    std::string reactant(long long i) const { return m_r.reactants()[index_in(i, m_r.reactants().size(), "reactant")].formula.display_string(false); }
    std::string product(long long i) const { return m_r.products()[index_in(i, m_r.products().size(), "product")].formula.display_string(false); }
    bool has_thermo_data() const { return m_r.has_thermo_data(); }
    double delta_h(double t) const { return m_r.delta_h(t); }
    double delta_s(double t) const { return m_r.delta_s(t); }
    double delta_g(double t) const { return m_r.delta_g(t); }
    double equilibrium_constant(double t) const { return m_r.equilibrium_constant(t); }
    double electrons_transferred() const { return m_r.electrons_transferred(); }
    std::string net_ionic() const { return fc::ionic_equations(m_r).net; }
    std::string complete_ionic() const { return fc::ionic_equations(m_r).complete; }
    std::string to_string() const { return m_r.to_string(); }
    std::string unicode() const { return m_r.to_unicode_string(); }

    const fc::Reaction& raw() const { return m_r; }

private:
    fc::Reaction m_r;
};

class prediction {
public:
    prediction() = default;
    explicit prediction(const std::string& reactants) : m_p(fc::predict_products(reactants)) {}

    bool ok() const { return m_p.ok; }
    bool no_reaction() const { return m_p.no_reaction; }
    std::string equation() const { return m_p.equation(false); }
    std::string unicode() const { return m_p.equation(true); }
    std::string kind() const { return m_p.kind; }
    std::string explanation() const { return m_p.explanation; }
    std::string net_ionic() const { return m_p.net_ionic; }
    std::string complete_ionic() const { return m_p.complete_ionic; }
    std::string spectators() const { return join(m_p.spectators, ", "); }
    std::string notes() const { return join(m_p.notes, "\n"); }

private:
    fc::PredictedReaction m_p;
};

class molecule {
public:
    molecule() = default;
    static molecule from_smiles(const std::string& smiles) {
        molecule m;
        try {
            m.m_m = fc::Molecule::from_smiles(smiles);
        } catch (const std::exception& ex) {
            fail(ex.what());
        }
        return m;
    }

    long long add_atom(const std::string& symbol) { return static_cast<long long>(m_m.add_atom(element_of(symbol))); }
    long long add_bond(long long a, long long b, long long order) {
        try {
            return static_cast<long long>(m_m.add_bond(index_in(a, m_m.atom_count(), "atom"), index_in(b, m_m.atom_count(), "atom"), order_of(order)));
        } catch (const std::exception& ex) {
            fail(ex.what());
        }
    }
    void set_formal_charge(long long atom, long long charge) { m_m.set_formal_charge(index_in(atom, m_m.atom_count(), "atom"), static_cast<int>(charge)); }
    void set_position(long long atom, double x, double y, double z) { m_m.set_position(index_in(atom, m_m.atom_count(), "atom"), x, y, z); }
    long long add_hydrogens() { return static_cast<long long>(m_m.add_implicit_hydrogens()); }
    long long atom_count() const { return static_cast<long long>(m_m.atom_count()); }
    long long bond_count() const { return static_cast<long long>(m_m.bond_count()); }
    std::string atom_symbol(long long i) const { return m_m.atoms()[index_in(i, m_m.atom_count(), "atom")].element->symbol; }
    long long formal_charge(long long i) const { return m_m.atoms()[index_in(i, m_m.atom_count(), "atom")].formal_charge; }
    double x(long long i) const { return m_m.atoms()[index_in(i, m_m.atom_count(), "atom")].x; }
    double y(long long i) const { return m_m.atoms()[index_in(i, m_m.atom_count(), "atom")].y; }
    double z(long long i) const { return m_m.atoms()[index_in(i, m_m.atom_count(), "atom")].z; }
    long long bond_atom_a(long long i) const { return static_cast<long long>(m_m.bonds()[index_in(i, m_m.bond_count(), "bond")].a); }
    long long bond_atom_b(long long i) const { return static_cast<long long>(m_m.bonds()[index_in(i, m_m.bond_count(), "bond")].b); }
    double bond_order(long long i) const { return fc::numeric_bond_order(m_m.bonds()[index_in(i, m_m.bond_count(), "bond")].order); }
    double bond_length(long long i) const { return m_m.bond_length(m_m.bonds()[index_in(i, m_m.bond_count(), "bond")]); }
    double bond_energy(long long i) const { return m_m.bond_energy(m_m.bonds()[index_in(i, m_m.bond_count(), "bond")]); }
    long long lone_pairs(long long i) const { return m_m.lone_pairs(index_in(i, m_m.atom_count(), "atom")); }
    std::string geometry_at(long long i) const { return fc::to_string(m_m.geometry_at(index_in(i, m_m.atom_count(), "atom")).molecular_geometry); }
    std::string hybridization_at(long long i) const { return fc::to_string(m_m.hybridization_at(index_in(i, m_m.atom_count(), "atom"))); }
    long long charge() const { return m_m.charge(); }
    std::string formula() const { return m_m.formula().hill(); }
    double molar_mass() const { return m_m.molar_mass(); }
    long long ring_count() const { return m_m.ring_count(); }
    bool is_valid() const { return m_m.valid(); }
    double total_bond_energy() const { return m_m.total_bond_energy(); }
    double estimated_enthalpy_of_formation() const { return m_m.estimated_enthalpy_of_formation(); }
    void fix_lewis_structure(long long total_charge) { m_m.apply_lewis(m_m.best_lewis_structure(static_cast<int>(total_charge))); }
    long long resonance_count(long long total_charge) const { return static_cast<long long>(m_m.resonance_structures(static_cast<int>(total_charge)).count()); }
    void layout_2d() { m_m.layout_2d(); }
    void layout_3d() { m_m.layout_3d(); }
    double dipole_moment() const { return m_m.dipole_moment(); }
    bool is_polar() const {
        fc::Molecule copy = m_m;
        if (!copy.has_positions()) copy.layout_3d();
        return copy.dipole_moment() > 0.1;
    }
    std::string name() const { return fc::name_compound(m_m.formula().hill()).name; }

    const fc::Molecule& raw() const { return m_m; }

private:
    fc::Molecule m_m;
};

class ionic_crystal {
public:
    ionic_crystal() = default;
    ionic_crystal(const std::string& cation, const std::string& anion) {
        const auto c = fc::parse_ion(cation);
        const auto a = fc::parse_ion(anion);
        if (c && a && c->charge > 0 && a->charge < 0) {
            m_c = fc::ionic_crystal(*c, *a);
            return;
        }
        const fc::Element* m = fc::PeriodicTable::find(cation);
        const fc::Element* n = fc::PeriodicTable::find(anion);
        if (m && n) {
            m_c = fc::ionic_crystal(*m, *n);
            return;
        }
        fail("couldn't make ions from '" + cation + "' and '" + anion + "'");
    }

    std::string formula() const { return m_c.formula; }
    std::string name() const { return m_c.name; }
    std::string structure() const { return fc::to_string(m_c.structure); }
    std::string structure_reason() const { return m_c.structure_reason; }
    double lattice_energy() const { return m_c.lattice_energy(); }
    double born_lande() const { return m_c.lattice_energy_born_lande; }
    double born_mayer() const { return m_c.lattice_energy_born_mayer; }
    double kapustinskii() const { return m_c.lattice_energy_kapustinskii; }
    double born_haber() const { return m_c.lattice_energy_born_haber; }
    double lattice_constant() const { return m_c.cell.a; }
    double density() const { return m_c.density; }
    double radius_ratio() const { return m_c.radius_ratio; }
    long long cation_coordination() const { return m_c.cation_coordination; }
    long long anion_coordination() const { return m_c.anion_coordination; }
    double percent_ionic() const { return m_c.percent_ionic; }
    std::string solubility() const { return fc::to_string(m_c.solubility.solubility); }
    std::string steps() const { return join(m_c.steps, "\n"); }

private:
    fc::IonicCrystal m_c;
};

class metal {
public:
    metal() = default;
    explicit metal(const std::string& composition) {
        std::vector<fc::AlloyComponent> parts;
        std::size_t start = 0;
        while (start <= composition.size()) {
            std::size_t end = composition.find_first_of(",;", start);
            if (end == std::string::npos) end = composition.size();
            const std::string item(fc::trim(std::string_view(composition).substr(start, end - start)));
            if (!item.empty()) {
                const std::size_t colon = item.find_first_of(":=");
                const std::string sym(fc::trim(std::string_view(item).substr(0, colon)));
                const double frac = colon == std::string::npos ? 1.0 : std::strtod(item.c_str() + colon + 1, nullptr);
                parts.push_back({ &element_of(sym), frac });
            }
            start = end + 1;
        }
        if (parts.empty()) fail("give at least one metal, like \"Cu:0.7, Zn:0.3\"");
        m_c = fc::metallic_crystal(parts);
    }

    std::string name() const { return m_c.name; }
    std::string formula() const { return m_c.formula; }
    std::string structure() const { return fc::to_string(m_c.structure); }
    std::string type() const { return fc::to_string(m_c.type); }
    double lattice_constant() const { return m_c.lattice_constant; }
    double density() const { return m_c.density; }
    double packing_fraction() const { return m_c.packing_fraction; }
    long long coordination() const { return m_c.coordination; }
    double fermi_energy() const { return m_c.fermi_energy; }
    double melting_point() const { return m_c.melting_point; }
    double valence_electrons_per_atom() const { return m_c.valence_electrons_per_atom; }
    std::string notes() const { return join(m_c.notes, "\n"); }

private:
    fc::MetallicCrystal m_c;
};

class phase_diagram {
public:
    phase_diagram() = default;
    explicit phase_diagram(const std::string& substance) : m_d(fc::phase_diagram(fc::find_phase_properties(substance))) {}

    bool ok() const { return m_d.ok; }
    std::string name() const { return m_d.properties.name; }
    std::string phase_at(double t, double p) const { return fc::to_string(m_d.phase_at(t, p)); }
    std::string describe(double t, double p) const { return m_d.describe(t, p); }
    double vapor_pressure(double t) const { return m_d.vapor_pressure(t); }
    double sublimation_pressure(double t) const { return m_d.sublimation_pressure(t); }
    double melting_pressure(double t) const { return m_d.melting_pressure(t); }
    double boiling_temperature(double p) const { return m_d.boiling_temperature(p); }
    double melting_temperature(double p) const { return m_d.melting_temperature(p); }
    double triple_temperature() const { return m_d.triple.t; }
    double triple_pressure() const { return m_d.triple.p; }
    double critical_temperature() const { return m_d.critical.t; }
    double critical_pressure() const { return m_d.critical.p; }
    std::string notes() const { return join(m_d.notes, "\n"); }

private:
    fc::PhaseDiagram m_d;
};

class gas {
public:
    gas() : m_eos(std::make_unique<fc::IdealGas>()) {}
    gas(const std::string& model, const std::string& substance) {
        const fc::Substance* s = fc::find_substance(substance);
        if (!s) fail("no data for '" + substance + "'");
        for (fc::EosKind k : fc::equation_of_state_kinds()) {
            if (fc::equals_ignore_case(model, fc::to_string(k))) {
                try {
                    m_eos = fc::make_equation_of_state(k, *s);
                } catch (const std::exception& ex) {
                    fail(ex.what());
                }
                return;
            }
        }
        fail("unknown equation of state '" + model + "'");
    }
    gas(const gas& o) : m_eos(o.m_eos ? o.m_eos->clone() : nullptr) {}
    gas& operator=(const gas& o) {
        if (this != &o) m_eos = o.m_eos ? o.m_eos->clone() : nullptr;
        return *this;
    }

    std::string name() const { return m_eos->name(); }
    double pressure(double t, double molar_volume) const { return m_eos->pressure(t, molar_volume); }
    double molar_volume(double t, double p) const { return m_eos->molar_volume(t, p); }
    double compressibility(double t, double p) const { return m_eos->compressibility(t, p); }
    double fugacity_coefficient(double t, double p) const { return m_eos->fugacity_coefficient(t, p); }
    double saturation_pressure(double t) const { return m_eos->saturation_pressure(t); }
    std::string phase(double t, double p) const { return fc::to_string(m_eos->phase(t, p)); }

private:
    std::unique_ptr<fc::EquationOfState> m_eos;
};

inline long long element_count() { return fc::ELEMENT_COUNT; }
inline std::string name_of(const std::string& f) { return fc::name_compound(f).name; }
inline std::string kind_of(const std::string& f) { return fc::to_string(fc::name_compound(f).kind); }
inline std::string formula_of(const std::string& n) { return fc::formula_from_name(n).formula; }
inline std::string balance(const std::string& eq) {
    try {
        fc::Reaction r = fc::Reaction::parse(eq);
        r.balance();
        return r.to_string();
    } catch (const std::exception& ex) {
        fail(ex.what());
    }
}
inline std::string predict(const std::string& reactants) { return fc::predict_products(reactants).equation(false); }
inline long long polyatomic_ion_count() { return static_cast<long long>(fc::polyatomic_ions().size()); }
inline const fc::PolyatomicIonEntry& ion_at(long long i) { return fc::polyatomic_ions()[index_in(i, fc::polyatomic_ions().size(), "ion")]; }
inline std::string polyatomic_ion_name(long long i) { return ion_at(i).name; }
inline std::string polyatomic_ion_formula(long long i) { return fc::ion_formula_text(ion_at(i), false); }
inline long long polyatomic_ion_charge(long long i) { return ion_at(i).charge; }
inline std::string polyatomic_ion_category(long long i) { return ion_at(i).category; }
inline std::string polyatomic_ion_structure(long long i) { return ion_at(i).smiles; }
inline std::string polyatomic_ion_acid(long long i) { return ion_at(i).acid; }
inline long long find_polyatomic_ion(const std::string& text) {
    const fc::PolyatomicIonEntry* p = fc::find_polyatomic_ion(text);
    return p ? static_cast<long long>(p - fc::polyatomic_ions().data()) : -1;
}
inline std::string ion_name(const std::string& text) {
    const auto d = fc::parse_ion(text);
    return d ? d->name : std::string();
}
inline long long ion_charge(const std::string& text) {
    const auto d = fc::parse_ion(text);
    return d ? d->charge : 0;
}
inline std::string ionic_formula(const std::string& cation, const std::string& anion) {
    const auto c = fc::parse_ion(cation);
    const auto a = fc::parse_ion(anion);
    if (!c || !a) fail("couldn't read the ions '" + cation + "' and '" + anion + "'");
    return fc::combine_ions(*c, *a).formula;
}
inline std::string solubility(const std::string& f) { return fc::to_string(fc::solubility_of(f).solubility); }
inline double bond_energy(const std::string& a, const std::string& b, long long order) { return fc::bond_energy(element_of(a), element_of(b), order_of(order)); }
inline double bond_length(const std::string& a, const std::string& b, long long order) { return fc::bond_length(element_of(a), element_of(b), order_of(order)); }
inline double ionic_character(const std::string& a, const std::string& b) { return fc::Bond(element_of(a), element_of(b)).ionic_character(); }
inline std::string vsepr_shape(long long bonded, long long lone) { return fc::to_string(fc::vsepr(static_cast<int>(bonded), static_cast<int>(lone)).molecular_geometry); }
inline double convert(double v, const std::string& from, const std::string& to) {
    try {
        return fc::convert(v, from, to);
    } catch (const std::exception& ex) {
        fail(ex.what());
    }
}
inline std::string units_for(const std::string& dimension) { return join(fc::unit_symbols(fc::dimension_named(dimension)), ","); }
inline double radial_probability(long long n, long long l, double z_eff, double r_pm) { return fc::radial_probability(static_cast<int>(n), static_cast<int>(l), z_eff, r_pm * fc::PICOMETER / fc::BOHR_RADIUS) / (fc::BOHR_RADIUS / fc::PICOMETER); }
inline double effective_nuclear_charge(const std::string& e, long long n, long long l) { return fc::effective_nuclear_charge(element_of(e), static_cast<int>(n), static_cast<int>(l)); }
inline std::string orbital_name(long long n, long long l, long long m) { return fc::orbital_name(static_cast<int>(n), static_cast<int>(l), static_cast<int>(m)); }
inline double most_probable_radius(long long n, long long l, double z_eff) { return fc::most_probable_radius_bohr(static_cast<int>(n), static_cast<int>(l), z_eff) * fc::BOHR_RADIUS / fc::PICOMETER; }

inline double solve(const std::string& equation_id, const std::string& knowns, const std::string& unknown, const std::string& unit) {
    const fc::ChemistryEquation* eq = fc::find_equation(equation_id);
    if (!eq) fail("unknown equation '" + equation_id + "'");
    std::vector<double> values(eq->variables.size(), fc::NO_VALUE);
    std::size_t start = 0;
    while (start <= knowns.size()) {
        std::size_t end = knowns.find(';', start);
        if (end == std::string::npos) end = knowns.size();
        const std::string item(fc::trim(std::string_view(knowns).substr(start, end - start)));
        if (!item.empty()) {
            const std::size_t eqp = item.find('=');
            if (eqp == std::string::npos) fail("write knowns like \"P = 1 atm; T = 300 K\"");
            const std::string id(fc::trim(std::string_view(item).substr(0, eqp)));
            const int idx = eq->index_of(id);
            if (idx < 0) fail("'" + id + "' isn't a variable of " + eq->name);
            const fc::SolverVariable& var = eq->variables[static_cast<std::size_t>(idx)];
            const fc::Quantity q = fc::Quantity::parse(item.substr(eqp + 1), var.dimension, var.unit);
            if (!q.valid()) fail("couldn't read the value of " + id);
            values[static_cast<std::size_t>(idx)] = q.si();
        }
        start = end + 1;
    }
    const int target = eq->index_of(unknown);
    if (target < 0) fail("'" + unknown + "' isn't a variable of " + eq->name);
    if (values[static_cast<std::size_t>(target)] == values[static_cast<std::size_t>(target)]) fail(unknown + " was already given");
    const fc::EquationSolution s = fc::solve_equation(*eq, values, target);
    if (!s.ok) fail(s.message);
    const fc::SolverVariable& var = eq->variables[static_cast<std::size_t>(target)];
    if (unit.empty()) return s.value;
    return fc::from_si(s.value, unit, var.dimension);
}

inline std::string equation_ids() {
    std::vector<std::string> ids;
    for (const fc::ChemistryEquation& e : fc::chemistry_equations()) ids.push_back(e.id);
    return join(ids, ",");
}

inline double theoretical_yield(const std::string& equation, const std::string& knowns, const std::string& target, const std::string& unit) {
    fc::Reaction r;
    try {
        r = fc::Reaction::parse(equation);
        r.balance();
    } catch (const std::exception& ex) {
        fail(ex.what());
    }
    std::vector<std::string> names;
    for (const auto* side : { &r.reactants(), &r.products() }) {
        for (const fc::ReactionTerm& t : *side) names.push_back(t.formula.display_string(false));
    }
    std::vector<fc::StoichiometryKnown> list;
    std::size_t start = 0;
    while (start <= knowns.size()) {
        std::size_t end = knowns.find(';', start);
        if (end == std::string::npos) end = knowns.size();
        const std::string item(fc::trim(std::string_view(knowns).substr(start, end - start)));
        if (!item.empty()) {
            const std::size_t eqp = item.find('=');
            if (eqp == std::string::npos) fail("write amounts like \"N2 = 28 g; H2 = 10 g\"");
            const std::string who(fc::trim(std::string_view(item).substr(0, eqp)));
            const auto it = std::find(names.begin(), names.end(), who);
            if (it == names.end()) fail("'" + who + "' isn't in the reaction");
            const fc::Quantity q = fc::Quantity::parse(item.substr(eqp + 1));
            if (!q.valid()) fail("couldn't read the amount of " + who);
            fc::StoichiometryKnown k;
            k.species = static_cast<std::size_t>(it - names.begin());
            k.value = q.si();
            switch (q.dimension()) {
                case fc::Dimension::Mass: k.kind = fc::AmountKind::Mass; break;
                case fc::Dimension::Volume: k.kind = fc::AmountKind::GasVolume; break;
                default: k.kind = fc::AmountKind::Moles; break;
            }
            list.push_back(k);
        }
        start = end + 1;
    }
    const fc::StoichiometryResult res = fc::solve_stoichiometry(r, list);
    if (!res.ok) fail(res.message);
    const auto it = std::find(names.begin(), names.end(), target);
    if (it == names.end()) fail("'" + target + "' isn't in the reaction");
    const fc::StoichiometrySpecies& sp = res.species[static_cast<std::size_t>(it - names.begin())];
    const double n = sp.reactant ? sp.reacted_moles : sp.final_moles;
    const fc::UnitInfo* u = fc::find_unit(unit);
    if (!u || u->dimension == fc::Dimension::Amount) return u ? u->from_si(n) : n;
    if (u->dimension == fc::Dimension::Mass) return u->from_si(n * sp.molar_mass / 1000.0);
    if (u->dimension == fc::Dimension::Volume) return u->from_si(n * fc::GAS_CONSTANT * fc::STANDARD_TEMPERATURE / fc::ATMOSPHERE);
    fail("give the answer unit as an amount, mass or gas volume");
}

inline std::string limiting_reactant(const std::string& equation, const std::string& knowns) {
    fc::Reaction r;
    try {
        r = fc::Reaction::parse(equation);
        r.balance();
    } catch (const std::exception& ex) {
        fail(ex.what());
    }
    std::vector<fc::StoichiometryKnown> list;
    std::size_t start = 0;
    while (start <= knowns.size()) {
        std::size_t end = knowns.find(';', start);
        if (end == std::string::npos) end = knowns.size();
        const std::string item(fc::trim(std::string_view(knowns).substr(start, end - start)));
        if (!item.empty()) {
            const std::size_t eqp = item.find('=');
            const std::string who(fc::trim(std::string_view(item).substr(0, eqp)));
            for (std::size_t i = 0; i < r.reactants().size(); ++i) {
                if (r.reactants()[i].formula.display_string(false) != who) continue;
                const fc::Quantity q = fc::Quantity::parse(item.substr(eqp + 1));
                fc::StoichiometryKnown k;
                k.species = i;
                k.value = q.si();
                k.kind = q.dimension() == fc::Dimension::Mass ? fc::AmountKind::Mass : (q.dimension() == fc::Dimension::Volume ? fc::AmountKind::GasVolume : fc::AmountKind::Moles);
                list.push_back(k);
            }
        }
        start = end + 1;
    }
    const fc::StoichiometryResult res = fc::solve_stoichiometry(r, list);
    if (!res.ok || res.limiting < 0) return std::string();
    return res.species[static_cast<std::size_t>(res.limiting)].formula;
}

inline double strong_acid_ph(double c) { return fc::strong_acid_ph(c); }
inline double weak_acid_ph(double c, double ka) { return fc::weak_acid_ph(c, ka); }
inline double strong_base_ph(double c) { return fc::strong_base_ph(c); }
inline double weak_base_ph(double c, double kb) { return fc::weak_base_ph(c, kb); }
inline double buffer_ph(double pka, double base, double acid) { return fc::henderson_hasselbalch(pka, base, acid); }
inline double bond_dissociation_gibbs(const std::string& a, const std::string& b, long long order, double t, double p) { return fc::bond_dissociation(element_of(a), element_of(b), order_of(order), t, p).gibbs; }
inline double bond_fraction_broken(const std::string& a, const std::string& b, long long order, double t, double p) { return fc::bond_dissociation(element_of(a), element_of(b), order_of(order), t, p).fraction_broken; }

} // namespace chemistry
} // namespace walnut_rt

#endif // WALNUT_CHEMISTRY_HPP
