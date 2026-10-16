#ifndef WALNUT_STATISTICS_HPP
#define WALNUT_STATISTICS_HPP

#include "walnut_runtime.hpp"
#include "fizmo/../statistics.hpp"

#include <cstdint>
#include <exception>
#include <string>
#include <vector>

namespace walnut_rt {
namespace stats {

namespace fs = fizmo::statistics;

template <typename... T>
struct tag {};

template <typename T>
using Sample = fs::Sample<T>;

template <typename T>
using TestResult = fs::TestResult<T>;

template <typename T>
using ConfidenceInterval = fs::ConfidenceInterval<T>;

template <typename T>
using Normal = fs::Normal<T>;
template <typename T>
using StudentT = fs::StudentT<T>;
template <typename T>
using ChiSquared = fs::ChiSquared<T>;
template <typename T>
using FisherF = fs::FisherF<T>;
template <typename T>
using Exponential = fs::Exponential<T>;
template <typename T>
using Uniform = fs::Uniform<T>;
template <typename T>
using Gamma = fs::Gamma<T>;
template <typename T>
using Beta = fs::Beta<T>;
template <typename T>
using NoncentralT = fs::NoncentralT<T>;
template <typename T>
using Binomial = fs::Binomial<T>;
template <typename T>
using Geometric = fs::Geometric<T>;
template <typename T>
using Poisson = fs::Poisson<T>;
template <typename T>
using Hypergeometric = fs::Hypergeometric<T>;
template <typename T>
using NegativeBinomial = fs::NegativeBinomial<T>;

template <typename Fn>
inline auto guarded(Fn&& fn) -> decltype(fn()) {
    try {
        return fn();
    } catch (const runtime_error&) {
        throw;
    } catch (const std::exception& e) {
        fail(e.what());
    }
}

inline std::uint64_t count(long long n) { return n < 0 ? 0 : static_cast<std::uint64_t>(n); }

inline fs::Tail tail_of(long long code) {
    if (code == 0) return fs::Tail::lower;
    if (code == 1) return fs::Tail::upper;
    return fs::Tail::two;
}

inline long long tail_code(fs::Tail t) {
    if (t == fs::Tail::lower) return 0;
    if (t == fs::Tail::upper) return 1;
    return 2;
}

inline fs::Variances variances_of(long long code) { return code == 1 ? fs::Variances::equal : fs::Variances::unequal; }

inline fs::Expected expected_of(long long code) {
    if (code == 1) return fs::Expected::counts;
    if (code == 2) return fs::Expected::proportions;
    return fs::Expected::automatic;
}

template <typename T> inline T sum(const std::vector<T>& d) { return guarded([&] { return fs::sum(d); }); }
template <typename T> inline T mean(const std::vector<T>& d) { return guarded([&] { return fs::mean(d); }); }
template <typename T> inline T median(const std::vector<T>& d) { return guarded([&] { return fs::median(d); }); }
template <typename T> inline T mode(const std::vector<T>& d) { return guarded([&] { return fs::mode(d); }); }
template <typename T> inline std::vector<T> modes(const std::vector<T>& d) { return guarded([&] { return fs::modes(d); }); }
template <typename T> inline T minimum(const std::vector<T>& d) { return guarded([&] { return fs::minimum(d); }); }
template <typename T> inline T maximum(const std::vector<T>& d) { return guarded([&] { return fs::maximum(d); }); }
template <typename T> inline T range(const std::vector<T>& d) { return guarded([&] { return fs::range(d); }); }
template <typename T> inline T variance(const std::vector<T>& d) { return guarded([&] { return fs::variance(d); }); }
template <typename T> inline T population_variance(const std::vector<T>& d) { return guarded([&] { return fs::population_variance(d); }); }
template <typename T> inline T standard_deviation(const std::vector<T>& d) { return guarded([&] { return fs::standard_deviation(d); }); }
template <typename T> inline T population_standard_deviation(const std::vector<T>& d) { return guarded([&] { return fs::population_standard_deviation(d); }); }
template <typename T> inline T standard_error(const std::vector<T>& d) { return guarded([&] { return fs::standard_error(d); }); }
template <typename T> inline T quantile(const std::vector<T>& d, const T& p) { return guarded([&] { return fs::quantile(d, p); }); }
template <typename T> inline T percentile(const std::vector<T>& d, const T& p) { return guarded([&] { return fs::percentile(d, p); }); }
template <typename T> inline T interquartile_range(const std::vector<T>& d) { return guarded([&] { return fs::interquartile_range(d); }); }
template <typename T> inline std::vector<T> outliers(const std::vector<T>& d, const T& fence) { return guarded([&] { return fs::outliers(d, fence); }); }
template <typename T> inline T skewness(const std::vector<T>& d, bool sample) { return guarded([&] { return fs::skewness(d, sample); }); }
template <typename T> inline T kurtosis(const std::vector<T>& d, bool sample) { return guarded([&] { return fs::kurtosis(d, sample); }); }
template <typename T> inline T excess_kurtosis(const std::vector<T>& d, bool sample) { return guarded([&] { return fs::excess_kurtosis(d, sample); }); }
template <typename T> inline T pearson_kurtosis(const std::vector<T>& d, bool sample) { return guarded([&] { return fs::pearson_kurtosis(d, sample); }); }
template <typename T> inline T covariance(const std::vector<T>& x, const std::vector<T>& y, bool sample) { return guarded([&] { return fs::covariance(x, y, sample); }); }
template <typename T> inline T correlation(const std::vector<T>& x, const std::vector<T>& y) { return guarded([&] { return fs::correlation(x, y); }); }
template <typename T> inline T z_score(const T& x, const T& m, const T& sd) { return guarded([&] { return fs::z_score(x, m, sd); }); }
template <typename T> inline std::vector<T> z_scores(const std::vector<T>& d, bool sample) { return guarded([&] { return fs::z_scores(d, sample); }); }
template <typename T> inline T geometric_mean(const std::vector<T>& d) { return guarded([&] { return fs::geometric_mean(d); }); }
template <typename T> inline T harmonic_mean(const std::vector<T>& d) { return guarded([&] { return fs::harmonic_mean(d); }); }
template <typename T> inline T weighted_mean(const std::vector<T>& d, const std::vector<T>& w) { return guarded([&] { return fs::weighted_mean(d, w); }); }
template <typename T> inline T mean_absolute_deviation(const std::vector<T>& d) { return guarded([&] { return fs::mean_absolute_deviation(d); }); }
template <typename T> inline T median_absolute_deviation(const std::vector<T>& d) { return guarded([&] { return fs::median_absolute_deviation(d); }); }
template <typename T> inline T coefficient_of_variation(const std::vector<T>& d) { return guarded([&] { return fs::coefficient_of_variation(d); }); }
template <typename T> inline T sum_of_squares(const std::vector<T>& d) { return guarded([&] { return fs::sum_of_squares(d); }); }
template <typename T> inline T root_mean_square(const std::vector<T>& d) { return guarded([&] { return fs::root_mean_square(d); }); }

template <typename T>
inline std::vector<T> quartiles(const std::vector<T>& d) {
    return guarded([&] {
        const fs::Quartiles<T> q = fs::quartiles(d);
        return std::vector<T>{q.q1, q.median, q.q3};
    });
}

template <typename T>
inline std::vector<T> five_numbers(const std::vector<T>& d) {
    return guarded([&] {
        const fs::FiveNumberSummary<T> s = fs::five_number_summary(d);
        return std::vector<T>{s.minimum, s.q1, s.median, s.q3, s.maximum};
    });
}

template <typename T>
inline std::vector<T> regression(const std::vector<T>& x, const std::vector<T>& y) {
    return guarded([&] {
        const fs::LinearRegression<T> r = fs::linear_regression(x, y);
        return std::vector<T>{r.slope, r.intercept, r.r, r.r_squared, r.residual_standard_error, r.slope_standard_error, r.intercept_standard_error, T(r.n)};
    });
}

template <typename T> inline fs::Sample<T> sample_from_data(const std::vector<T>& d) { return guarded([&] { return fs::Sample<T>::from_data(d); }); }
template <typename T> inline fs::Sample<T> sample_from_summary(const T& m, const T& sd, long long n) { return guarded([&] { return fs::Sample<T>::from_summary(m, sd, count(n)); }); }
template <typename T> inline fs::Sample<T> sample_from_differences(const std::vector<T>& a, const std::vector<T>& b) { return guarded([&] { return fs::Sample<T>::from_differences(a, b); }); }
template <typename T> inline fs::Sample<T> sample_with_population(const fs::Sample<T>& s, long long n) { return s.with_population(count(n)); }
template <typename T> inline long long sample_size(const fs::Sample<T>& s) { return static_cast<long long>(s.size()); }
template <typename T> inline long long sample_population(const fs::Sample<T>& s) { return static_cast<long long>(s.population()); }
template <typename T> inline long long sample_df(const fs::Sample<T>& s) { return static_cast<long long>(s.degrees_of_freedom()); }

template <typename T> inline std::string test_name(const fs::TestResult<T>& r) { return r.name; }
template <typename T>
inline T test_number(const fs::TestResult<T>& r, long long which) {
    switch (which) {
        case 0: return r.statistic;
        case 1: return r.p_value;
        case 2: return r.degrees_of_freedom;
        case 3: return r.estimate;
        case 4: return r.null_value;
        default: return r.standard_error;
    }
}
template <typename T> inline long long test_tail(const fs::TestResult<T>& r) { return tail_code(r.tail); }
template <typename T> inline long long test_condition_count(const fs::TestResult<T>& r) { return static_cast<long long>(r.conditions.size()); }
template <typename T> inline std::string test_condition_name(const fs::TestResult<T>& r, long long i) { return r.conditions[static_cast<std::size_t>(i)].name; }
template <typename T> inline bool test_condition_met(const fs::TestResult<T>& r, long long i) { return r.conditions[static_cast<std::size_t>(i)].met; }
template <typename T> inline std::string test_condition_detail(const fs::TestResult<T>& r, long long i) { return r.conditions[static_cast<std::size_t>(i)].detail; }

template <typename T> inline std::string interval_name(const fs::ConfidenceInterval<T>& r) { return r.name; }
template <typename T>
inline T interval_number(const fs::ConfidenceInterval<T>& r, long long which) {
    switch (which) {
        case 0: return r.estimate;
        case 1: return r.critical_value;
        case 2: return r.standard_error;
        case 3: return r.margin_of_error;
        case 4: return r.lower;
        case 5: return r.upper;
        case 6: return r.confidence;
        default: return r.degrees_of_freedom;
    }
}
template <typename T> inline long long interval_condition_count(const fs::ConfidenceInterval<T>& r) { return static_cast<long long>(r.conditions.size()); }
template <typename T> inline std::string interval_condition_name(const fs::ConfidenceInterval<T>& r, long long i) { return r.conditions[static_cast<std::size_t>(i)].name; }
template <typename T> inline bool interval_condition_met(const fs::ConfidenceInterval<T>& r, long long i) { return r.conditions[static_cast<std::size_t>(i)].met; }
template <typename T> inline std::string interval_condition_detail(const fs::ConfidenceInterval<T>& r, long long i) { return r.conditions[static_cast<std::size_t>(i)].detail; }

inline fs::Proportion proportion(long long s, long long n, long long pop) { return fs::Proportion(count(s), count(n), count(pop)); }

template <typename T> inline fs::TestResult<T> one_mean_t_test(const fs::Sample<T>& s, const T& mu0, long long tail) { return guarded([&] { return fs::one_mean_t_test(s, mu0, tail_of(tail)); }); }
template <typename T> inline fs::TestResult<T> one_mean_z_test(const fs::Sample<T>& s, const T& sigma, const T& mu0, long long tail) { return guarded([&] { return fs::one_mean_z_test(s, sigma, mu0, tail_of(tail)); }); }
template <typename T> inline fs::TestResult<T> two_mean_t_test(const fs::Sample<T>& a, const fs::Sample<T>& b, const T& d0, long long tail, long long v) { return guarded([&] { return fs::two_mean_t_test(a, b, d0, tail_of(tail), variances_of(v)); }); }
template <typename T> inline fs::TestResult<T> two_mean_z_test(const fs::Sample<T>& a, const T& s1, const fs::Sample<T>& b, const T& s2, const T& d0, long long tail) { return guarded([&] { return fs::two_mean_z_test(a, s1, b, s2, d0, tail_of(tail)); }); }
template <typename T> inline fs::TestResult<T> paired_t_test(const fs::Sample<T>& d, const T& d0, long long tail) { return guarded([&] { return fs::paired_t_test(d, d0, tail_of(tail)); }); }
template <typename T> inline fs::TestResult<T> one_proportion_z_test(long long s, long long n, long long pop, const T& p0, long long tail) { return guarded([&] { return fs::one_proportion_z_test<T>(proportion(s, n, pop), p0, tail_of(tail)); }); }
template <typename T> inline fs::TestResult<T> two_proportion_z_test(long long s1, long long n1, long long pop1, long long s2, long long n2, long long pop2, long long tail) { return guarded([&] { return fs::two_proportion_z_test<T>(proportion(s1, n1, pop1), proportion(s2, n2, pop2), tail_of(tail)); }); }
template <typename T> inline fs::TestResult<T> goodness_of_fit(const std::vector<T>& o, const std::vector<T>& e, long long kind) { return guarded([&] { return fs::chi_squared_goodness_of_fit_test(o, e, expected_of(kind)); }); }
template <typename T> inline fs::TestResult<T> independence(const std::vector<std::vector<T>>& table) { return guarded([&] { return fs::chi_squared_independence_test(table); }); }
template <typename T> inline fs::TestResult<T> homogeneity(const std::vector<std::vector<T>>& table) { return guarded([&] { return fs::chi_squared_homogeneity_test(table); }); }
template <typename T> inline fs::TestResult<T> slope_t_test(const std::vector<T>& x, const std::vector<T>& y, const T& b0, long long tail) { return guarded([&] { return fs::slope_t_test(x, y, b0, tail_of(tail)); }); }

template <typename T> inline fs::ConfidenceInterval<T> one_mean_t_interval(const fs::Sample<T>& s, const T& c) { return guarded([&] { return fs::one_mean_t_interval(s, c); }); }
template <typename T> inline fs::ConfidenceInterval<T> one_mean_z_interval(const fs::Sample<T>& s, const T& sigma, const T& c) { return guarded([&] { return fs::one_mean_z_interval(s, sigma, c); }); }
template <typename T> inline fs::ConfidenceInterval<T> two_mean_t_interval(const fs::Sample<T>& a, const fs::Sample<T>& b, const T& c, long long v) { return guarded([&] { return fs::two_mean_t_interval(a, b, c, variances_of(v)); }); }
template <typename T> inline fs::ConfidenceInterval<T> two_mean_z_interval(const fs::Sample<T>& a, const T& s1, const fs::Sample<T>& b, const T& s2, const T& c) { return guarded([&] { return fs::two_mean_z_interval(a, s1, b, s2, c); }); }
template <typename T> inline fs::ConfidenceInterval<T> paired_t_interval(const fs::Sample<T>& d, const T& c) { return guarded([&] { return fs::paired_t_interval(d, c); }); }
template <typename T> inline fs::ConfidenceInterval<T> variance_interval(const fs::Sample<T>& s, const T& c) { return guarded([&] { return fs::variance_interval(s, c); }); }
template <typename T> inline fs::ConfidenceInterval<T> slope_t_interval(const std::vector<T>& x, const std::vector<T>& y, const T& c) { return guarded([&] { return fs::slope_t_interval(x, y, c); }); }
template <typename T> inline fs::ConfidenceInterval<T> one_proportion_z_interval(long long s, long long n, long long pop, const T& c) { return guarded([&] { return fs::one_proportion_z_interval<T>(proportion(s, n, pop), c); }); }
template <typename T> inline fs::ConfidenceInterval<T> two_proportion_z_interval(long long s1, long long n1, long long pop1, long long s2, long long n2, long long pop2, const T& c) { return guarded([&] { return fs::two_proportion_z_interval<T>(proportion(s1, n1, pop1), proportion(s2, n2, pop2), c); }); }

template <typename T> inline T critical_z(const T& alpha, long long tail) { return guarded([&] { return fs::critical_z(alpha, tail_of(tail)); }); }
template <typename T> inline T critical_t(const T& alpha, const T& df, long long tail) { return guarded([&] { return fs::critical_t(alpha, df, tail_of(tail)); }); }
template <typename T> inline T critical_chi_squared(const T& alpha, const T& df) { return guarded([&] { return fs::critical_chi_squared(alpha, df); }); }
template <typename T> inline T critical_f(const T& alpha, const T& d1, const T& d2) { return guarded([&] { return fs::critical_f(alpha, d1, d2); }); }
template <typename T> inline T z_star(const T& c) { return guarded([&] { return fs::z_star(c); }); }
template <typename T> inline T t_star(const T& c, const T& df) { return guarded([&] { return fs::t_star(c, df); }); }
template <typename T> inline T z_statistic(const T& e, const T& n, const T& se) { return guarded([&] { return fs::z_statistic(e, n, se); }); }
template <typename T> inline T t_statistic(const fs::Sample<T>& s, const T& mu0) { return guarded([&] { return fs::t_statistic(s, mu0); }); }
template <typename T> inline T welch_df(const fs::Sample<T>& a, const fs::Sample<T>& b) { return guarded([&] { return fs::welch_degrees_of_freedom(a, b); }); }

template <typename T> inline T one_mean_z_power(const T& m0, const T& ma, const T& sigma, long long n, const T& alpha, long long tail) { return guarded([&] { return fs::one_mean_z_test_power(m0, ma, sigma, count(n), alpha, tail_of(tail)); }); }
template <typename T> inline T one_mean_t_power(const T& m0, const T& ma, const T& sd, long long n, const T& alpha, long long tail) { return guarded([&] { return fs::one_mean_t_test_power(m0, ma, sd, count(n), alpha, tail_of(tail)); }); }
template <typename T> inline T two_mean_z_power(const T& d0, const T& da, const T& s1, long long n1, const T& s2, long long n2, const T& alpha, long long tail) { return guarded([&] { return fs::two_mean_z_test_power(d0, da, s1, count(n1), s2, count(n2), alpha, tail_of(tail)); }); }
template <typename T> inline T two_mean_t_power(const T& d0, const T& da, const T& s1, long long n1, const T& s2, long long n2, const T& alpha, long long tail, long long v) { return guarded([&] { return fs::two_mean_t_test_power(d0, da, s1, count(n1), s2, count(n2), alpha, tail_of(tail), variances_of(v)); }); }
template <typename T> inline T one_proportion_z_power(const T& p0, const T& pa, long long n, const T& alpha, long long tail) { return guarded([&] { return fs::one_proportion_z_test_power(p0, pa, count(n), alpha, tail_of(tail)); }); }
template <typename T> inline T two_proportion_z_power(const T& p1, const T& p2, long long n1, long long n2, const T& alpha, long long tail) { return guarded([&] { return fs::two_proportion_z_test_power(p1, p2, count(n1), count(n2), alpha, tail_of(tail)); }); }
template <typename T> inline long long size_for_mean(const T& margin, const T& sigma, const T& c) { return guarded([&] { return static_cast<long long>(fs::sample_size_for_mean(margin, sigma, c)); }); }
template <typename T> inline long long size_for_proportion(const T& margin, const T& c, const T& guess) { return guarded([&] { return static_cast<long long>(fs::sample_size_for_proportion(margin, c, guess)); }); }

template <typename D, typename T = typename D::value_type> inline T dist_mean(const D& d) { return guarded([&] { return d.mean(); }); }
template <typename D, typename T = typename D::value_type> inline T dist_variance(const D& d) { return guarded([&] { return d.variance(); }); }
template <typename D, typename T = typename D::value_type> inline T dist_sd(const D& d) { return guarded([&] { return d.standard_deviation(); }); }
template <typename D, typename T = typename D::value_type> inline T dist_median(const D& d) { return guarded([&] { return d.median(); }); }
template <typename D, typename T = typename D::value_type> inline T dist_quantile(const D& d, const T& p) { return guarded([&] { return d.quantile(p); }); }
template <typename D, typename T = typename D::value_type> inline T dist_upper_quantile(const D& d, const T& p) { return guarded([&] { return d.upper_quantile(p); }); }
template <typename D, typename T = typename D::value_type> inline T dist_pdf(const D& d, const T& x) { return guarded([&] { return d.pdf(x); }); }
template <typename D, typename T = typename D::value_type> inline T dist_log_pdf(const D& d, const T& x) { return guarded([&] { return d.log_pdf(x); }); }
template <typename D, typename T = typename D::value_type> inline T dist_cdf(const D& d, const T& x) { return guarded([&] { return d.cdf(x); }); }
template <typename D, typename T = typename D::value_type> inline T dist_survival(const D& d, const T& x) { return guarded([&] { return d.survival(x); }); }
template <typename D, typename T = typename D::value_type> inline T dist_between(const D& d, const T& a, const T& b) { return guarded([&] { return d.between(a, b); }); }
template <typename D, typename T = typename D::value_type> inline T dist_outside(const D& d, const T& a, const T& b) { return guarded([&] { return d.outside(a, b); }); }
template <typename D, typename T = typename D::value_type> inline T dist_pmf(const D& d, long long k) { return guarded([&] { return d.pmf(static_cast<std::int64_t>(k)); }); }
template <typename D, typename T = typename D::value_type> inline T dist_log_pmf(const D& d, long long k) { return guarded([&] { return d.log_pmf(static_cast<std::int64_t>(k)); }); }
template <typename D, typename T = typename D::value_type> inline T dist_cdf_k(const D& d, long long k) { return guarded([&] { return d.cdf(static_cast<std::int64_t>(k)); }); }
template <typename D, typename T = typename D::value_type> inline T dist_survival_k(const D& d, long long k) { return guarded([&] { return d.survival(static_cast<std::int64_t>(k)); }); }
template <typename D, typename T = typename D::value_type> inline T dist_between_k(const D& d, long long a, long long b) { return guarded([&] { return d.between(static_cast<std::int64_t>(a), static_cast<std::int64_t>(b)); }); }
template <typename D, typename T = typename D::value_type> inline T dist_outside_k(const D& d, long long a, long long b) { return guarded([&] { return d.outside(static_cast<std::int64_t>(a), static_cast<std::int64_t>(b)); }); }
template <typename D> inline bool dist_valid(const D& d) { return d.valid(); }

template <typename T> inline fs::Normal<T> make_normal_standard() { return fs::Normal<T>(); }
template <typename T> inline fs::Normal<T> make_normal(const T& m, const T& sd) { return fs::Normal<T>(m, sd); }
template <typename T> inline fs::Uniform<T> make_uniform_unit() { return fs::Uniform<T>(); }
template <typename T> inline fs::StudentT<T> make_student_t(const T& df) { return fs::StudentT<T>(df); }
template <typename T> inline fs::ChiSquared<T> make_chi_squared(const T& df) { return fs::ChiSquared<T>(df); }
template <typename T> inline fs::FisherF<T> make_fisher_f(const T& a, const T& b) { return fs::FisherF<T>(a, b); }
template <typename T> inline fs::Exponential<T> make_exponential(const T& rate) { return fs::Exponential<T>(rate); }
template <typename T> inline fs::Uniform<T> make_uniform(const T& a, const T& b) { return fs::Uniform<T>(a, b); }
template <typename T> inline fs::Gamma<T> make_gamma(const T& shape, const T& scale) { return fs::Gamma<T>(shape, scale); }
template <typename T> inline fs::Beta<T> make_beta(const T& a, const T& b) { return fs::Beta<T>(a, b); }
template <typename T> inline fs::NoncentralT<T> make_noncentral_t(const T& df, const T& nc) { return fs::NoncentralT<T>(df, nc); }
template <typename T> inline fs::Binomial<T> make_binomial(long long n, const T& p) { return fs::Binomial<T>(count(n), p); }
template <typename T> inline fs::Geometric<T> make_geometric(const T& p) { return fs::Geometric<T>(p); }
template <typename T> inline fs::Poisson<T> make_poisson(const T& rate) { return fs::Poisson<T>(rate); }
template <typename T> inline fs::Hypergeometric<T> make_hypergeometric(long long pop, long long succ, long long draws) { return fs::Hypergeometric<T>(count(pop), count(succ), count(draws)); }
template <typename T> inline fs::NegativeBinomial<T> make_negative_binomial(const T& r, const T& p) { return fs::NegativeBinomial<T>(r, p); }

} // namespace stats
} // namespace walnut_rt

#endif // WALNUT_STATISTICS_HPP
