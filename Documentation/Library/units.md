# units

`units` converts between units of measurement and keeps quantities with units attached, with the units checked while compiling. Meters can't be converted to seconds, and an energy can't be mixed up with a torque, even though both are newton meters. Composite units are made by multiplying and dividing units, so any unit can be built, even an unusual one. It's Fizmo's `fizmo::units`.

```walnut
import module.units
```

Everything is in the namespace `std::units`. Values are `float`.

## A first example

```walnut
import module.units
import module.io

using namespace std::units;

function main() -> int {
    std::io::println(convert(10.0, distance::foot, distance::meter));
    std::io::println(convert(100.0, temperature::celsius, temperature::fahrenheit));

    const EnergyUnit: newton_meter = force::newton * distance::meter;
    std::io::println(convert(1.0, energy::kilowatt_hour, newton_meter));

    EnergyQuantity: work = quantity(10.0, force::newton) * quantity(2.0, distance::meter);
    std::io::println(work.value_in(energy::calorie));
    return 0;
} end function main
```

This prints `3.048`, `212`, `3600000` and `4.77691793254991879239514665138053`.

## Units

A unit is a `Unit<L, M, T, I, K, N, J, A, Kind>`. The eight numbers are the powers of length, mass, time, electric current, temperature, amount of substance, luminous intensity and angle, so a newton is `Unit<1, 1, -2, 0, 0, 0, 0, 0, kind::force>`. The kind says what the unit measures (see [Kinds](#kinds)). Named types such as `LengthUnit`, `ForceUnit` and `EnergyUnit` are easier to write.

| Written | Gives |
|---|---|
| `a * b`, `a / b` | A composite unit, such as `distance::meter / time::second`. Its kind is `kind::generic` |
| `2.0 * u`, `u * 2.0`, `u / 2.0` | The same kind of unit, scaled |
| `1.0 / u` | A unit with the opposite powers, such as hertz from seconds |
| `u.pow<2>()` | A power of a unit. `distance::meter.pow<2>()` is an area and `pow<3>()` a volume; other powers are `kind::generic` |
| `prefixed(0.001, u)` | A scaled unit, the same as `0.001 * u` |
| `u.as_generic()` | The unit with its kind dropped |
| `kind_cast<kind::energy>(u)` | The unit as a different kind with the same dimension, when you mean it |
| `Unit::dimension_text()` | The dimension in SI symbols, such as `m^2·kg·s^-2` for `EnergyUnit` |
| `u.scale`, `u.offset` | The unit in SI base units: a value `x` in `u` is `x * scale + offset` |

A new unit can be built and given a name in one line. Made-up units work the same way:

```walnut
const VelocityUnit: furlongs_per_fortnight = distance::furlong / time::fortnight;
const Unit<0, 3, -1, 0, 0, 0, 0, 1>: odd = mass::kilogram.pow<3>() * angle::degree / time::hour;
```

## Converting

| Function | Does |
|---|---|
| `convert(value, from, to)` | Converts a value between two units. The units must have the same dimension and compatible kinds, or the program doesn't compile |
| `to_base(value, unit)`, `from_base(value, unit)` | Converts to and from SI base units |
| `scale_ratio(from, to)` | How many `to` make one `from` |
| `same_dimension(a, b)`, `same_kind(a, b)` | Whether two units could be converted |

The results are rounded to 33 significant digits, so that conversions such as 100 °C to °F come out as `212` instead of showing the last bits of float rounding.

## Quantities

A `Quantity` is a value with a unit, stored in SI base units. `quantity(value, unit)` makes one, and `q.value_in(unit)` (or `value_in(q, unit)`) reads it in any compatible unit. Each named unit type has a matching quantity type: `LengthQuantity`, `EnergyQuantity`, `TorqueQuantity` and so on, and `QuantityOf<SomeUnit>` names the quantity type for any unit.

| Written | Gives |
|---|---|
| `a + b`, `a - b` | The sum or difference. The quantities must have the same dimension and compatible kinds |
| `a * b`, `a / b` | A quantity of the composite dimension, with `kind::generic` |
| `q * 2.0`, `2.0 * q`, `q / 2.0` | A scaled quantity of the same kind |
| `1.0 / q`, `reciprocal(q)` | A quantity with the opposite powers |
| `==`, `!=`, `<`, `>`, `<=`, `>=` | Comparisons between compatible quantities |
| `q.base` | The value in SI base units |
| `q.to_string()` | The base value and its dimension, such as `20 m^2·kg·s^-2` |
| `q.as_generic()`, `kind_cast<kind::torque>(q)` | The same as for units |

```walnut
import module.units
import module.io

using namespace std::units;

function main() -> int {
    VelocityQuantity: speed = quantity(72.0, velocity::kilometers_per_hour);
    TimeQuantity: trip = quantity(30.0, time::minute);
    LengthQuantity: gone = speed * trip;
    std::io::println(gone.value_in(distance::mile), " ", (speed * 2.0).value_in(velocity::meters_per_second));
    return 0;
} end function main
```

This prints `22.3693629205440229062276306370795 40`.

## Kinds

Some quantities share a dimension but are different things. Energy and torque are both newton meters; hertz and becquerels are both 1/s; grays and sieverts are both J/kg. Every named unit has a kind, a struct in `std::units::kind`, and units or quantities of two different kinds can't be converted, added or compared, even with the same dimension. Each of these is a compile error:

```walnut
const EnergyUnit: e = torque::newton_meter;
float: x = convert(1.0, radiation::becquerel, frequency::hertz);
auto: y = quantity(1.0, energy::joule) + quantity(1.0, torque::newton_meter);
```

The rules are:
- **Composite units have no particular kind.** `force::newton * distance::meter` is `kind::generic`, which can be stored as any kind with the same dimension. That is how both `EnergyUnit` and `TorqueUnit` can be made from newtons and meters.
- **A named kind never turns into a different one.** Torque can't become energy, and the other way round, except through `kind_cast`.
- **A sub-kind mixes with its parent.** Impulse is a sub-kind of momentum, so newton seconds and kilogram meters per second can be added and converted.
- **Generic mixes with everything.** Converting between `energy::joule` and `force::newton * distance::meter` works.

The kinds are `dimensionless`, `length`, `mass`, `time`, `current`, `temperature`, `temperature_interval`, `temperature_point`, `amount`, `luminous`, `angle`, `frequency`, `activity`, `velocity`, `acceleration`, `angular_velocity`, `area`, `volume`, `density_2d`, `density_3d`, `momentum`, `impulse`, `force`, `energy`, `torque`, `power`, `pressure`, `absorbed_dose`, `equivalent_dose`, `charge`, `voltage`, `resistance`, `conductance`, `capacitance`, `inductance`, `magnetic_flux`, `mass_flow_rate`, `volume_flow_rate`, `area_flow_rate`, `molar_flow_rate`, `molarity_volume`, `molarity_mass`, `specific_area` and `specific_volume`, plus `generic`. Each has a unit type and a quantity type named after it: `kind::mass_flow_rate` has `MassFlowRateUnit` and `MassFlowRateQuantity`.

**Your own kinds.** A kind is a struct. `kind_of<Self, L, M, T, I, K, N, J, A>` gives it a dimension, `kind_like<Self, Other>` copies another kind's dimension, and `subkind_of<Self, Parent>` makes a sub-kind. `KindUnit<K>` and `KindQuantity<K>` are its unit and quantity types:

```walnut
import module.units
import module.io

using namespace std::units;

struct spin_rate inherits kind_like<spin_rate, kind::angular_velocity> {
} end struct spin_rate

using SpinRateUnit = KindUnit<spin_rate>;

function main() -> int {
    const SpinRateUnit: rps = angle::revolution / time::second;
    std::io::println(convert(1.0, rps, angle::radian / time::second));
    return 0;
} end function main
```

This prints `6.28318530717958647692528676655901`. A `SpinRateUnit` can't be converted to or from units of `kind::angular_velocity`, but generic units such as `angle::radian / time::second` work with both.

## Temperatures

Celsius, Fahrenheit and the other scales with a zero point of their own are temperature points (`TemperaturePointUnit`). A point can be converted to another point scale or to kelvin, but multiplying or dividing one has no meaning, so `energy::joule / temperature::celsius` is a compile error that says to use an interval unit. The interval units (`kelvin_interval`, `celsius_interval`, `fahrenheit_interval`) measure differences and work in composites such as J/(kg·°C). Kelvin and Rankine start at absolute zero, so they work both ways.

Quantities follow the same rules: two points can't be added, the difference of two points is an interval, and a point plus an interval is a point.

```walnut
import module.units
import module.io

using namespace std::units;

function main() -> int {
    TemperaturePointQuantity: morning = quantity(20.0, temperature::celsius);
    TemperaturePointQuantity: noon = quantity(30.0, temperature::celsius);
    TemperatureIntervalQuantity: rise = noon - morning;
    TemperaturePointQuantity: later = noon + quantity(9.0, temperature::fahrenheit_interval);
    std::io::println(rise.value_in(temperature::fahrenheit_interval), " ", later.value_in(temperature::celsius));
    return 0;
} end function main
```

This prints `18 35`.

## The units

The units are in namespaces named after what they measure. Most metric units also come with every SI prefix from quecto to quetta (`distance::kilometer`, `time::millisecond`, `energy::megaelectronvolt`, `area::square_centimeter`, `volume::cubic_millimeter`).

| Namespace | Some of its units |
|---|---|
| `distance` | `meter`, `angstrom`, `micron`, `inch`, `foot`, `yard`, `mile`, `nautical_mile`, `furlong`, `fathom`, `au`, `light_year`, `parsec`, `point`, `pica`; `light_time(seconds)` makes a light-distance unit |
| `time` | `second`, `minute`, `hour`, `day`, `week`, `fortnight`, `year`, `century`, `millennium`, `planck_time` |
| `mass` | `gram`, `tonne`, `grain`, `ounce`, `pound`, `stone`, `ton_us`, `slug` |
| `angle` | `radian`, `degree`, `gradian`, `arcminute`, `arcsecond`, `turn`, `revolution` |
| `temperature` | `kelvin`, `celsius`, `fahrenheit`, `rankine`, `reaumur`, `romer`, `delisle`, `newton`, `kelvin_interval`, `celsius_interval`, `fahrenheit_interval` |
| `amount` | `mole` |
| `area` | `square_meter` and the other `square_` units, `acre`, `hectare` |
| `volume` | `cubic_meter` and the other `cubic_` units, `liter`, US and imperial gallons, quarts, pints, cups and spoons |
| `velocity` | `meters_per_second`, `kilometers_per_hour`, `miles_per_hour`, `feet_per_second`, `knot`, `light_speed` |
| `acceleration` | `meters_per_second_squared`, `feet_per_second_squared`, `gal`, `standard_gravity`; `g_force(n)` |
| `force` | `newton`, `dyne`, `gram_force`, `pound_force`, `poundal`, `sthene`, `ton_force` |
| `energy` | `joule`, `watt_hour`, `erg`, `calorie`, `food_calorie`, `btu`, `therm`, `electronvolt`, `foot_pound_force` |
| `power` | `watt`, `horsepower`, `metric_horsepower`, `electric_horsepower` |
| `torque` | `newton_meter`, `pound_foot`, `pound_inch` |
| `pressure` | `pascal_`, `bar`, `atmosphere`, `torr`, `mmHg`, `psi` |
| `frequency` | `hertz`, `rpm`, `bpm`; `period_second`, `period_minute`, `period_hour` and `period_day` take a period and give a frequency |
| `momentum`, `impulse` | `kilogram_meter_per_second`, `gram_centimeter_per_second`; `newton_second`, `pound_force_second` |
| `current`, `charge`, `voltage`, `resistance`, `capacitance` | `ampere`, `coulomb`, `ampere_hour`, `faraday`, `volt`, `ohm`, `farad`, and the CGS units |
| `radiation` | `gray`, `rad` (absorbed dose), `sievert`, `rem` (equivalent dose), `becquerel`, `curie`, `rutherford` (activity) |
| `density` | mass per area and mass per volume, such as `gram_per_cubic_centimeter`, `pound_per_cubic_foot` |
| `mass_flow`, `volume_flow`, `area_flow`, `molar_flow` | flow rates, such as `liter_per_minute`, `us_gallon_per_hour` |
| `concentration` | `molar` and other moles per volume or mass |
| `dosage` | volume and area per mass, such as `liter_per_kilogram` |

The values come from Fizmo, with 36 significant digits, and a program only includes the units it uses.
