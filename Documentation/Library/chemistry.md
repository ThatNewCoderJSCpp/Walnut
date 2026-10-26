# chemistry

`chemistry` is Walnut's front end to Fizmo's chemistry component: the periodic table and isotopes, formulas, naming, reactions (balancing and predicting products), molecules and Lewis structures, ionic and metallic crystals, phase diagrams, gases, orbitals, units and an equation solver. It's the same code Sapling's chemistry section runs on.

```walnut
import module.chemistry
```

Everything is in the namespace `std::chemistry`. Numbers cross as `short float` (C++ `double`) and counts as `long int`. Energies are in kJ/mol unless they say otherwise, lengths in picometres, temperatures in kelvin and pressures in pascals.

## Contents

1. [Elements](#elements)
2. [Formulas and names](#formulas-and-names)
3. [Polyatomic ions](#polyatomic-ions)
4. [Reactions](#reactions)
5. [Molecules](#molecules)
6. [Bonds](#bonds)
7. [Crystals and alloys](#crystals-and-alloys)
8. [Phases and gases](#phases-and-gases)
9. [Orbitals](#orbitals)
10. [Units](#units)
11. [Solving equations](#solving-equations)
12. [Stoichiometry and acids](#stoichiometry-and-acids)
13. [Errors](#errors)

## Elements

```walnut
import module.chemistry
import module.io

function main() -> int {
    const auto: fe = std::chemistry::element("Fe");
    std::io::println(`${fe.name()}: Z = ${fe.atomic_number()}, ${fe.molar_mass()} g/mol, ${fe.electron_configuration()}`);
    std::io::println(`first ionization energy ${fe.ionization_energy(1l)} eV, electronegativity ${fe.electronegativity()}`);
    return 0;
} end function main
```

`element(symbol_or_name)` and `element(atomic_number)` give an `Element` (also `Element("Fe")`, `Element(26l)`). It has:

| Group | Methods |
|---|---|
| Identity | `atomic_number`, `symbol`, `name`, `group`, `period`, `block`, `category`, `classification`, `is_metal`, `is_metalloid`, `is_nonmetal`, `is_noble_gas`, `is_stable` |
| Mass | `molar_mass` (CIAAW; the most stable isotope for radioactive elements), `molar_mass_uncertainty`, `mass_number`, `neutrons` |
| Electrons | `electron_configuration` (noble-gas form), `full_electron_configuration`, `valence_electrons`, `unpaired_electrons`, `ionization_energy(n)` (eV), `ionization_energy_count`, `electron_affinity` (eV), `electronegativity` (Pauling) |
| Oxidation | `most_common_oxidation_state`, `oxidation_states` (like `"-2 +2 +3* +6"`, `*` marks common ones) |
| Size | `covalent_radius`, `atomic_radius`, `van_der_waals_radius`, `metallic_radius`, `ionic_radius(charge, coordination)` (pm) |
| Bulk | `melting_point`, `boiling_point` (K), `density` (g/cm³), `atomization_enthalpy` (kJ/mol), `standard_state`, `crystal_structure` |
| Isotopes | `isotope_count`, `isotope_mass_number(i)`, `isotope_mass(i)` (u), `isotope_abundance(i)`, `isotope_half_life(i)` (s), `isotope_is_stable(i)` |

`element_count()` is 118.

## Formulas and names

`Formula("Ca(NO3)2")` parses a formula (groups, hydrates like `CuSO4*5H2O`, charges like `SO4^2-`, phases like `(aq)`). It has `molar_mass`, `charge`, `atom_count`, `element_count`, `count(symbol)`, `mass_percent(symbol)`, `electrons`, `degree_of_unsaturation`, `is_ion`, `is_organic`, `phase`, `to_string` (as typed), `unicode` (with subscripts), `hill`, `flat` (no groups), `empirical`, `name`, `moles(grams)` and `grams(moles)`. `molar_mass(formula)` is a shortcut.

| Function | |
|---|---|
| `name_of(formula)` | `"Fe2O3"` → `"iron(III) oxide"`, `"CuSO4*5H2O"` → `"copper(II) sulfate pentahydrate"`, `"N2O5"` → `"dinitrogen pentoxide"`, `"HCl(aq)"` → `"hydrochloric acid"`, `"C3H8"` → `"propane"`, `"C6H12O6"` → `"glucose"` |
| `kind_of(formula)` | `"ionic compound"`, `"acid"`, `"binary molecular compound"`, `"organic compound"`, `"carbohydrate"`, ... |
| `formula_of(name)` | the reverse: `"aluminum sulfate"` → `"Al2(SO4)3"`, `"ferric chloride"` → `"FeCl3"`, `"sulfuric acid"` → `"H2SO4"`, `"methyl ethanoate"` → `"CH3COOCH3"` |

Ionic names use Stock numerals for metals with more than one common charge; classical names (ferric, cuprous, ...) are understood when reading names.

## Polyatomic ions

Fizmo keeps a table of over 150 polyatomic ions: cations (hydronium, ammonium, mercury(I), nitronium, uranyl, ...), oxyanions of every common family, organic anions, metal oxyanions and common complex ions. Each has a name, other names, its parent acid and a structure (SMILES).

```walnut
for (long int: i = 0l; i < std::chemistry::polyatomic_ion_count(); i += 1l) {
    std::io::println(`${std::chemistry::polyatomic_ion_formula(i)}  ${std::chemistry::polyatomic_ion_name(i)}`);
}
```

| Function | |
|---|---|
| `polyatomic_ion_count()`, `polyatomic_ion_name(i)`, `polyatomic_ion_formula(i)`, `polyatomic_ion_charge(i)`, `polyatomic_ion_category(i)`, `polyatomic_ion_structure(i)`, `polyatomic_ion_acid(i)` | the table |
| `find_polyatomic_ion(name_or_formula)` | its index, or -1 |
| `ion_name(text)`, `ion_charge(text)` | reads `"Fe3+"`, `"SO42-"`, `"SO4^2-"`, `"iron(III)"`, `"sulfate"` |
| `ionic_formula(cation, anion)` | `ionic_formula("Al3+", "sulfate")` is `"Al2(SO4)3"` |
| `solubility(formula)` | `"soluble"`, `"slightly soluble"`, `"insoluble"` or `"reacts with water"`, from the solubility rules |

## Reactions

```walnut
std::io::println(std::chemistry::balance("Fe + O2 -> Fe2O3"));
std::io::println(std::chemistry::predict("AgNO3(aq) + NaCl(aq)"));
```

`balance(equation)` returns the balanced equation. `predict(reactants)` takes only the left side and works out the products from the reaction rules: combustion, synthesis, decomposition, single replacement (activity series and halogen reactivity), double replacement (solubility rules, gas and water formation), neutralization, oxides with water, acid ionization, esterification. Reversible reactions get `<=>`.

`Prediction("Zn + CuSO4")` gives the details: `ok`, `no_reaction`, `equation`, `unicode`, `kind`, `explanation`, `net_ionic`, `complete_ionic`, `spectators` and `notes`.

`Reaction("CH4 + O2 -> CO2 + H2O")` has `balance`, `is_balanced`, `is_reversible`, `is_redox`, `type`, `reactant_count`, `product_count`, `reactant(i)`, `product(i)`, `reactant_coefficient(i)`, `product_coefficient(i)`, `has_thermo_data`, `delta_h(T)`, `delta_s(T)`, `delta_g(T)` (J/mol), `equilibrium_constant(T)`, `electrons_transferred`, `net_ionic`, `complete_ionic`, `to_string` and `unicode`.

## Molecules

A `Molecule` is a graph of atoms and bonds. Build one atom by atom, or from SMILES:

```walnut
std::chemistry::Molecule: m;
const long int: c = m.add_atom("C");
const long int: o = m.add_atom("O");
m.add_bond(c, o, 2l);
m.add_hydrogens();
std::io::println(`${m.formula()} ${m.name()}`);

auto: benzene = std::chemistry::Molecule::from_smiles("c1ccccc1");
std::io::println(`resonance structures: ${benzene.resonance_count(0l)}`);
```

| Method | |
|---|---|
| `add_atom(symbol)`, `add_bond(a, b, order)` (1, 2, 3 or 4 for aromatic), `add_hydrogens()`, `set_formal_charge(atom, q)`, `set_position(atom, x, y, z)` | building |
| `atom_count`, `bond_count`, `atom_symbol(i)`, `formal_charge(i)`, `x(i)`, `y(i)`, `z(i)`, `bond_atom_a(i)`, `bond_atom_b(i)`, `bond_order(i)`, `bond_length(i)`, `bond_energy(i)` | reading |
| `lone_pairs(i)`, `geometry_at(i)`, `hybridization_at(i)` | VSEPR at an atom |
| `fix_lewis_structure(charge)` | picks the bond orders and formal charges with the lowest formal charges (the textbook Lewis structure) |
| `resonance_count(charge)` | how many equivalent best Lewis structures there are |
| `layout_2d()`, `layout_3d()` | coordinates in pm; 3D uses VSEPR electron domains |
| `formula`, `name`, `charge`, `molar_mass`, `ring_count`, `is_valid`, `total_bond_energy`, `estimated_enthalpy_of_formation`, `dipole_moment` (D), `is_polar` | the molecule |

## Bonds

`bond_energy(a, b, order = 1)` (kJ/mol) and `bond_length(a, b, order = 1)` (pm) use tables of average bond energies and lengths, falling back to Pauling's equation and covalent radii. `ionic_character(a, b)` is the fraction of ionic character from the electronegativity difference. `vsepr_shape(bonded, lone_pairs)` names the shape (`vsepr_shape(2l, 2l)` is `"bent"`).

`bond_dissociation_gibbs(a, b, order, T, P)` is ΔG for breaking the bond into gas-phase atoms at T and P (kJ/mol), and `bond_fraction_broken(a, b, order, T, P)` is the equilibrium fraction broken, from statistical mechanics (translational, rotational and electronic entropy).

## Crystals and alloys

`IonicCrystal("Na", "Cl")` (or ions: `IonicCrystal("NH4+", "NO3-")`) picks charges, the formula and a crystal structure (measured structures for common compounds, radius-ratio rules otherwise) and computes `lattice_energy` (Born–Haber when the data exists, then Born–Landé, then Kapustinskii), `born_lande`, `born_mayer`, `kapustinskii`, `born_haber`, `lattice_constant` (pm), `density` (g/cm³), `radius_ratio`, `cation_coordination`, `anion_coordination`, `percent_ionic`, `solubility`, plus `structure`, `structure_reason` and `steps` (how the ions form).

`Metal("Cu:0.7, Zn:0.3")` is a pure metal or an alloy given by atom fractions. It has `name` (brass, bronze, steel, sterling silver, solder, ...), `formula`, `structure`, `type` (substitutional or interstitial solid solution, amalgam, ...), `lattice_constant` (Vegard's law), `density`, `packing_fraction`, `coordination`, `fermi_energy` (eV, free-electron model), `melting_point` and `valence_electrons_per_atom`. `notes` lists the Hume-Rothery results.

## Phases and gases

`PhaseDiagram("water")` (any name or formula Fizmo knows, or an estimate from structure for unknown molecules) has `phase_at(T, P)`, `describe(T, P)`, `vapor_pressure(T)`, `sublimation_pressure(T)`, `melting_pressure(T)`, `boiling_temperature(P)`, `melting_temperature(P)`, `triple_temperature`, `triple_pressure`, `critical_temperature`, `critical_pressure` and `notes` (which values were estimated).

`Gas(model, substance)` is an equation of state for a substance: model is any of `"ideal gas"`, `"van der Waals"`, `"Redlich-Kwong"`, `"Soave-Redlich-Kwong"`, `"Peng-Robinson"`, `"Lee-Kesler"` and the rest of Fizmo's list. It has `pressure(T, molar_volume)`, `molar_volume(T, P)`, `compressibility(T, P)`, `fugacity_coefficient(T, P)`, `saturation_pressure(T)` and `phase(T, P)`. Molar volumes are in m³/mol.

## Orbitals

`radial_probability(n, l, z_eff, r_pm)` is the radial probability density r²R² per pm for a hydrogen-like orbital with effective nuclear charge `z_eff`. `effective_nuclear_charge(element, n, l)` uses Slater's rules, `most_probable_radius(n, l, z_eff)` is the peak in pm, and `orbital_name(n, l, m)` names a real orbital (`orbital_name(3l, 2l, 2l)` is `"3d_x²-y²"`).

## Units

`convert(value, from, to)` converts between any two units of the same kind: temperatures (`"K"`, `"C"`, `"F"`, `"R"`), pressures (`"Pa"`, `"kPa"`, `"bar"`, `"atm"`, `"torr"`, `"mmHg"`, `"psi"`, ...), masses, volumes, amounts (`"mol"`, `"particles"`), energies, molar energies (`"kJ/mol"`, `"kcal/mol"`, `"eV"`, `"cm-1"`), lengths (`"pm"`, `"A"`), concentrations (`"M"`, `"mM"`), densities, specific and molar volumes, heat capacities and more. `units_for(kind)` lists the units of a kind, comma separated (`units_for("pressure")`).

## Solving equations

`solve(equation, knowns, unknown, unit)` solves one of Fizmo's chemistry equations for whichever variable you leave out. The knowns are `name = value unit` pairs separated by `;`, in any units of the right kind:

```walnut
const short float: v = std::chemistry::solve("ideal_gas", "P = 1 atm; n = 1 mol; T = 0 C", "V", "L");
const short float: t = std::chemistry::solve("gibbs", "dG = 0 kJ/mol; dH = -92.2 kJ/mol; dS = -198.7 J/(mol*K)", "T", "C");
```

`equation_ids()` lists them (comma separated). They cover gas laws (ideal, combined, Boyle, Charles, Gay-Lussac, Avogadro, density, van der Waals, Dalton, Graham, RMS speed), stoichiometry (moles from mass, particles, percent yield, density), solutions (molarity, dilution, molality, mass percent, mole fraction, ppm, osmotic pressure, boiling and freezing point changes, Raoult, Henry), acids and bases (pH, pOH, pK_w, weak acid and base, Henderson–Hasselbalch, pK_a, titration), thermochemistry (q = mcΔT, phase changes, ΔG = ΔH − TΔS, ΔG° and K, ΔG and Q, van 't Hoff, Clausius–Clapeyron), kinetics (Arrhenius, zero, first and second order, half-life, radioactive decay), electrochemistry (cell potential, Nernst, ΔG = −nFE, Faraday's law, E° and K) and light (photon energy, wavelength and frequency, de Broglie, Rydberg).

## Stoichiometry and acids

`theoretical_yield(equation, knowns, target, unit)` balances the equation, turns the given amounts into moles, finds the limiting reactant and gives how much of `target` forms (or is used up). `limiting_reactant(equation, knowns)` names the limiting reactant.

```walnut
const short float: g = std::chemistry::theoretical_yield("N2 + H2 -> NH3", "N2 = 28 g; H2 = 10 g", "NH3", "g");
```

`strong_acid_ph(c)`, `weak_acid_ph(c, ka)`, `strong_base_ph(c)`, `weak_base_ph(c, kb)` and `buffer_ph(pka, base, acid)` take concentrations in mol/L.

## Errors

Bad input (an unknown element, a formula that doesn't parse, an equation that can't be balanced, a unit of the wrong kind) is a runtime error with a message saying what was wrong.
