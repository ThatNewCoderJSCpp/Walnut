# statistics

`statistics` has descriptive statistics, probability distributions, hypothesis tests, confidence intervals, power and sample sizes. The work is done by Fizmo's statistics library, which computes every result with extra precision and rounds it once at the end, so the answers are correct to the last digit of the float type you use.

```walnut
import module.statistics
```

Everything is in the namespace `std::statistics`. Data is passed as a `std::collections::Vector<T>`, and `T` has to be a float type (`float`, `short float`, `double` and so on). Results come back as the same `T`.

## A first example

```walnut
import module.io
import module.statistics

using std::collections::Vector;

function main() -> int {
    Vector<float>: scores(2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0);
    std::io::println(std::statistics::mean(scores), " ", std::statistics::median(scores), " ", std::statistics::population_standard_deviation(scores));

    auto: test = std::statistics::one_mean_t_test(scores, 4.0, std::statistics::Tail::upper);
    std::io::println(test.name, ": p < 0.12? ", test.p_value < 0.12, ", reject at 5%? ", test.reject(0.05));

    std::statistics::Normal<float>: z;
    std::io::println(z.quantile(0.975) > 1.959, " ", z.quantile(0.975) < 1.960);
    return 0;
} end function main
```

This prints `5 4.5 2`, then `One-sample t-test for a mean: p < 0.12? 1, reject at 5%? 0`, then `1 1`.

## How things are named

The names follow one pattern, so they are easy to guess:

- Tests are `<groups>_<parameter>_<distribution>_test`: `one_mean_t_test`, `two_mean_z_test`, `one_proportion_z_test`, `paired_t_test`, `slope_t_test`.
- Intervals are the same with `_interval`: `one_mean_t_interval`, `two_proportion_z_interval`.
- Power is the test's name with `_power`: `one_mean_t_test_power`.
- Sample data goes in as a list of values, or as a [`Sample`](#samples-and-proportions) when you only have the summary numbers. Counts of successes go in as a [`Proportion`](#samples-and-proportions).
- Choices are enums, not `bool` flags: `Tail::lower`, `Tail::upper` or `Tail::two` (the default) for the alternative hypothesis, and `Variances::unequal` (Welch, the default) or `Variances::equal` (pooled) for two-sample t procedures.

## Descriptive statistics

Each takes a `Vector<T>`.

| Function | Gives |
|---|---|
| `sum(data)`, `mean(data)`, `average(data)` | The sum and the arithmetic mean |
| `median(data)`, `mode(data)`, `modes(data)` | The middle value, the most common value, and every most-common value |
| `minimum(data)`, `maximum(data)`, `range(data)` | The extremes and their difference |
| `variance(data)`, `sample_variance(data)`, `population_variance(data)` | The sample variance (divides by n - 1), the same again, and the population variance (divides by n) |
| `standard_deviation(data)`, `sample_standard_deviation(data)`, `population_standard_deviation(data)` | The square roots of those |
| `standard_error(data)` | The standard error of the mean |
| `quantile(data, p)`, `percentile(data, percent)` | The value below which the fraction `p` (0 to 1), or `percent` (0 to 100), of the data lies |
| `quartiles(data)` | A `Quartiles<T>` with `q1`, `median` and `q3`, and `interquartile_range()` |
| `five_number_summary(data)` | A `FiveNumberSummary<T>` with `minimum`, `q1`, `median`, `q3` and `maximum`, and `range()` and `interquartile_range()` |
| `interquartile_range(data)` | `q3 - q1` |
| `outliers(data)`, `outliers(data, fence)` | The values more than `fence` (1.5 if left out) interquartile ranges outside the quartiles |
| `skewness(data)`, `kurtosis(data)`, `excess_kurtosis(data)`, `pearson_kurtosis(data)` | Shape. `kurtosis` and `pearson_kurtosis` are the Pearson kurtosis (3 for a normal distribution); `excess_kurtosis` is that minus 3, so 0 for a normal distribution. Each takes a second argument, `false`, for the population form |
| `geometric_mean(data)`, `harmonic_mean(data)`, `root_mean_square(data)` | The other means |
| `weighted_mean(data, weights)` | The mean with each value counted by its weight |
| `mean_absolute_deviation(data)`, `median_absolute_deviation(data)` | The average distance from the mean, and the median distance from the median |
| `coefficient_of_variation(data)` | The standard deviation divided by the mean |
| `sum_of_squares(data)` | The sum of the squares of the values |
| `covariance(x, y)`, `correlation(x, y)` | The sample covariance (add `false` for the population form) and Pearson's correlation |
| `z_score(x, mean, sd)`, `z_scores(data)` | One standardized value, or every value of the data standardized |
| `linear_regression(x, y)` | A `LinearRegression<T>`: `slope`, `intercept`, `r`, `r_squared`, `residual_standard_error`, `slope_standard_error`, `intercept_standard_error` and `size`, with `predict(x)` and `residual(x, y)` |

A function that needs more data than it was given, such as `variance` of one value, throws a `std::errors::statistics_error` that says how many values it needs.

## Distributions

Each distribution is a class template over the float type: `Normal<float>`, `Binomial<short float>` and so on.

| Continuous | Made with |
|---|---|
| `Normal<T>` | `()` for the standard normal, or `(mean, standard_deviation)` |
| `StudentT<T>` | `(degrees_of_freedom)` |
| `ChiSquared<T>` | `(degrees_of_freedom)` |
| `FisherF<T>` | `(numerator_df, denominator_df)` |
| `Exponential<T>` | `(rate)` |
| `Uniform<T>` | `()` for 0 to 1, or `(low, high)` |
| `Gamma<T>` | `(shape, scale)` |
| `Beta<T>` | `(alpha, beta)` |
| `NoncentralT<T>` | `(degrees_of_freedom, noncentrality)` |

| Discrete | Made with |
|---|---|
| `Binomial<T>` | `(trials, probability)` |
| `Geometric<T>` | `(probability)` |
| `Poisson<T>` | `(rate)` |
| `Hypergeometric<T>` | `(population, successes, draws)` |
| `NegativeBinomial<T>` | `(successes, probability)` |

Every distribution has `mean()`, `variance()`, `standard_deviation()`, `median()`, `quantile(p)` (the value with probability `p` below it), `upper_quantile(p)` (the value with probability `p` above it) and `is_valid()`.

Continuous distributions also have `pdf(x)`, `log_pdf(x)`, `cdf(x)` (the probability of a value at most `x`), `survival(x)` (the probability of a value above `x`), `below(x)` and `above(x)` (the same two), `between(low, high)`, `outside(low, high)` and `inverse_cdf(p)`. `Normal` adds `z_score(x)` and `value_at(z)`.

Discrete distributions take whole numbers instead: `pmf(k)`, `log_pmf(k)`, `cdf(k)`, `survival(k)`, `between(low, high)` and `outside(low, high)`, and the readable forms `exactly(k)`, `at_most(k)`, `less_than(k)`, `at_least(k)` and `greater_than(k)`.

```walnut
import module.io
import module.statistics

function main() -> int {
    std::statistics::Binomial<float>: coin(10, 0.5);
    std::io::println(coin.exactly(5), " ", coin.at_most(5), " ", coin.mean());
    return 0;
} end function main
```

This prints `0.24609375 0.623046875 5`.

## Samples and proportions

A `Sample<T>` holds what the tests need to know about one sample. Made from data, it keeps exact sums, so it is as precise as passing the data itself.

| Written | Gives |
|---|---|
| `sample_of(data)`, `Sample<T>::of(data)` | A sample from a list of values |
| `sample_summary(mean, sd, size)`, `Sample<T>::from_summary(mean, sd, size)` | A sample known only by its summary numbers |
| `differences_of(before, after)`, `Sample<T>::differences(before, after)` | The paired differences, for paired procedures |
| `s.with_population(n)` | The same sample, drawn from a population of `n`; the tests then check the 10% condition |

A `Sample` has `size()`, `mean()`, `standard_deviation()`, `standard_error()`, `degrees_of_freedom()`, `population()` and `has_population()`.

A `Proportion` is a count of successes: `Proportion(successes, size)`, or `Proportion(successes, size, population)`. It has `failures()`, `value()` and `with_population(n)`.

## Tests

Each test gives a `TestResult<T>`:

| Field or method | Holds |
|---|---|
| `name` | The test's name, such as `"Two-sample t-test for means"` |
| `statistic`, `p_value`, `degrees_of_freedom` | The results (`degrees_of_freedom` is NaN for z tests) |
| `estimate`, `null_value`, `standard_error` | The observed value, the hypothesized one, and the standard error |
| `tail` | The `Tail` that was tested |
| `conditions` | A `Vector<Condition>`; each `Condition` has `name`, `met` and `detail` |
| `reject(alpha)`, `fail_to_reject(alpha)` | Whether `p_value < alpha` |
| `conditions_met()` | Whether every condition holds |
| `to_string()` | A one-line summary |

| Test | Arguments |
|---|---|
| `one_mean_t_test` | `(data or sample, null_mean, tail)` |
| `one_mean_z_test` | `(data or sample, sigma, null_mean, tail)` |
| `two_mean_t_test` | `(a, b, null_difference = 0, tail, variances)` |
| `two_mean_z_test` | `(a, sigma_a, b, sigma_b, null_difference = 0, tail)` |
| `paired_t_test` | `(before, after, null_difference = 0, tail)`, or `(differences, null_difference, tail)` with a `Sample` |
| `one_proportion_z_test` | `(proportion, null_proportion, tail)` |
| `two_proportion_z_test` | `(a, b, tail)`, with two `Proportion`s |
| `chi_squared_goodness_of_fit_test` | `(observed, expected, kind)`. `kind` is `Expected::infer` (the default: proportions when the expected values add up to 1), `Expected::counts` or `Expected::proportions` |
| `chi_squared_independence_test`, `chi_squared_homogeneity_test` | `(table)`, a `Vector<Vector<T>>` of counts |
| `slope_t_test` | `(x, y, null_slope = 0, tail)` |

`tail` can always be left out, for a two-sided test. The proportion tests use `float` unless you name the type: `one_proportion_z_test<short float>(p, 0.5)`.

```walnut
import module.io
import module.statistics

using std::collections::Vector;
using std::statistics::Variances;
using std::statistics::Tail;

function main() -> int {
    Vector<float>: before(1.0, 3.0, 4.0, 4.0, 6.0, 5.0, 8.0, 7.0);
    Vector<float>: after(2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0);
    auto: welch = std::statistics::two_mean_t_test(before, after);
    auto: pooled = std::statistics::two_mean_t_test(before, after, 0.0, Tail::two, Variances::equal);
    auto: paired = std::statistics::paired_t_test(before, after);
    std::io::println(welch.name, " | ", pooled.name, " | ", paired.name);
    auto: p = std::statistics::one_proportion_z_test(std::statistics::Proportion(45, 100), 0.5);
    std::io::println(p.p_value < 0.32, " ", p.conditions_met());
    return 0;
} end function main
```

This prints `Two-sample t-test for means (Welch) | Two-sample pooled t-test for means | Paired t-test`, then `1 1`.

## Confidence intervals

Each interval gives a `ConfidenceInterval<T>` with `name`, `estimate`, `critical_value`, `standard_error`, `margin_of_error`, `lower`, `upper`, `confidence`, `degrees_of_freedom` and `conditions`, and the methods `contains(value)`, `width()`, `conditions_met()` and `to_string()`.

| Interval | Arguments |
|---|---|
| `one_mean_t_interval` | `(data or sample, confidence)` |
| `one_mean_z_interval` | `(data or sample, sigma, confidence)` |
| `two_mean_t_interval` | `(a, b, confidence, variances)` |
| `two_mean_z_interval` | `(a, sigma_a, b, sigma_b, confidence)` |
| `paired_t_interval` | `(before, after, confidence)`, or `(differences, confidence)` |
| `one_proportion_z_interval` | `(proportion, confidence)` |
| `two_proportion_z_interval` | `(a, b, confidence)` |
| `variance_interval` | `(data or sample, confidence)` |
| `slope_t_interval` | `(x, y, confidence)` |

`confidence` is a fraction, such as `0.95`.

```walnut
import module.io
import module.statistics

function main() -> int {
    auto: ci = std::statistics::one_mean_t_interval(std::statistics::sample_summary(5.0, 2.0, 30), 0.95);
    std::io::println(ci.lower > 4.25, " ", ci.upper < 5.75, " ", ci.contains(5.0));
    return 0;
} end function main
```

This prints `1 1 1`.

## Critical values, power and sample size

| Function | Gives |
|---|---|
| `critical_z(alpha, tail)`, `critical_t(alpha, df, tail)` | The z or t value that cuts off `alpha` |
| `critical_chi_squared(alpha, df)`, `critical_f(alpha, d1, d2)` | The upper-tail critical values |
| `z_star(confidence)`, `t_star(confidence, df)` | The critical values for an interval |
| `z_statistic(estimate, null_value, standard_error)`, `t_statistic(data or sample, null_mean)` | The test statistics on their own |
| `welch_degrees_of_freedom(a, b)` | The Welch–Satterthwaite degrees of freedom |
| `one_mean_z_test_power(null_mean, true_mean, sigma, size, alpha, tail)` | The power of the test |
| `one_mean_t_test_power(null_mean, true_mean, sd, size, alpha, tail)`, `paired_t_test_power(...)` | The same for t tests |
| `two_mean_z_test_power(null_difference, true_difference, sigma_a, size_a, sigma_b, size_b, alpha, tail)` | |
| `two_mean_t_test_power(null_difference, true_difference, sd_a, size_a, sd_b, size_b, alpha, tail, variances)` | |
| `one_proportion_z_test_power(null_p, true_p, size, alpha, tail)`, `two_proportion_z_test_power(p_a, p_b, size_a, size_b, alpha, tail)` | |
| `sample_size_for_mean(margin, sigma, confidence)` | The smallest sample with that margin of error |
| `sample_size_for_proportion(margin, confidence, p_guess = 0.5)` | The same for a proportion |

## Probability helpers

`probability_of_all(probabilities)` and `probability_of_any(probabilities)` combine independent events. `probability_of_union(a, b, both)`, `conditional_probability(both, given)`, `bayes(likelihood, prior, evidence)` and `complement(p)` are the textbook rules.
