
#include "tests/test_framework.h"

#include "Core/Dates.h"
#include "Core/calendar.h"
#include "Core/day_counter.h"
#include "Core/interpolator.h"
#include "Core/numericals.h"
#include "Curves/YieldCurve.h"
#include "Curves/CreditCurve.h"

#include <cmath>
#include <stdexcept>
#include <utility>
#include <vector>

using Core::Date;


TEST_CASE(date_decrement_goes_backward) {
    Date d(2026, 9, 15);
    --d;
    CHECK(d == Date(2026, 9, 14));
    Date e(2026, 9, 15);
    const Date old = e--;
    CHECK(old == Date(2026, 9, 15));
    CHECK(e == Date(2026, 9, 14));
}

TEST_CASE(date_invalid_ymd_throws) {
    CHECK_THROWS(Date(2026, 2, 30), std::invalid_argument);
    CHECK_THROWS(Date(2026, 13, 1), std::invalid_argument);
    CHECK_THROWS(Date(2026, 0, 1), std::invalid_argument);
    CHECK_THROWS(Date(2025, 2, 29), std::invalid_argument);
    CHECK(Date(2024, 2, 29).day() == 29);
}

TEST_CASE(date_null_sentinel) {
    CHECK(Date().is_null());
    CHECK(Date::null_date().is_null());
    CHECK(!Date(1969, 12, 31).is_null());  // avant 1970 n'est plus « nulle »
    CHECK(Date().to_string() == "null");
}

TEST_CASE(date_day_of_week_before_1970) {
    CHECK(Date(1969, 12, 31).day_of_week() == 3);  // mercredi
    CHECK(Date(1900, 1, 1).day_of_week() == 1);    // lundi
    CHECK(Date(2026, 10, 3).day_of_week() == 6);   // samedi
}

TEST_CASE(date_add_months) {
    CHECK(Date(2026, 1, 31).add_months(1) == Date(2026, 2, 28));
    CHECK(Date(2024, 1, 31).add_months(1) == Date(2024, 2, 29));
    CHECK(Date(2026, 2, 28).add_months(1) == Date(2026, 3, 28));
    CHECK(Date(2026, 2, 28).add_months(1, true) == Date(2026, 3, 31));
    CHECK(Date(2026, 1, 15).add_months(-2) == Date(2025, 11, 15));
    CHECK(Date(2024, 2, 29).add_years(1) == Date(2025, 2, 28));
    CHECK(Date(2026, 3, 20).add_months(3) == Date(2026, 6, 20));
}

TEST_CASE(date_from_string) {
    CHECK(Date::from_string("2025-12-25") == Date(2025, 12, 25));
    CHECK_THROWS(Date::from_string("2025-02-30"), std::invalid_argument);
    CHECK_THROWS(Date::from_string("2025-12-25abc"), std::invalid_argument);
    CHECK_THROWS(Date::from_string("2025/12/25"), std::invalid_argument);
}

TEST_CASE(date_constexpr) {
    constexpr Date d(2026, 1, 1);
    static_assert(d.raw_days() > 0);
    static_assert(Date(2026, 1, 31).add_months(1) == Date(2026, 2, 28));
    static_assert(Date::is_leap_year(2024) && !Date::is_leap_year(2100));
    CHECK(true);
}


TEST_CASE(calendar_preceding_conventions) {
    const Core::WeekendsOnly cal;
    using BDC = Core::BusinessDayConvention;
    CHECK(cal.adjust(Date(2026, 10, 3), BDC::Preceding) == Date(2026, 10, 2));
    CHECK(cal.adjust(Date(2026, 10, 3), BDC::Following) == Date(2026, 10, 5));
    CHECK(cal.adjust(Date(2026, 10, 31), BDC::ModifiedFollowing) == Date(2026, 10, 30));
    CHECK(cal.adjust(Date(2026, 8, 1), BDC::ModifiedPreceding) == Date(2026, 8, 3));
    CHECK(cal.adjust(Date(2026, 10, 3), BDC::Unadjusted) == Date(2026, 10, 3));
}

TEST_CASE(calendar_easter) {
    CHECK(Core::easter_sunday(2024) == Date(2024, 3, 31));
    CHECK(Core::easter_sunday(2025) == Date(2025, 4, 20));
    CHECK(Core::easter_sunday(2026) == Date(2026, 4, 5));
    CHECK(Core::easter_sunday(2038) == Date(2038, 4, 25));
}

TEST_CASE(calendar_target_holidays) {
    const Core::TargetCalendar cal;
    CHECK(!cal.is_business_day(Date(2026, 4, 3)));    // Vendredi saint
    CHECK(!cal.is_business_day(Date(2026, 4, 6)));    // Lundi de Pâques
    CHECK(!cal.is_business_day(Date(2025, 12, 26)));  // 26 décembre (vendredi)
    CHECK(!cal.is_business_day(Date(2026, 1, 1)));
    CHECK(!cal.is_business_day(Date(2026, 5, 1)));
    CHECK(cal.is_business_day(Date(2026, 4, 2)));
    CHECK(cal.is_business_day(Date(2026, 4, 7)));
    CHECK(cal.advance(Date(2026, 4, 2), 1) == Date(2026, 4, 7));
    CHECK(cal.advance(Date(2026, 4, 7), -1) == Date(2026, 4, 2));
}


TEST_CASE(daycounter_actact_isda) {
    using Core::ActualActual_ISDA;
    const ActualActual_ISDA dc;
    CHECK_CLOSE(dc.year_fraction(Date(2024, 1, 1), Date(2025, 1, 1)), 1.0, 1e-15);
    CHECK_CLOSE(dc.year_fraction(Date(2023, 7, 1), Date(2024, 7, 1)), 184.0 / 365.0 + 182.0 / 366.0, 1e-15);
    CHECK_CLOSE(dc.year_fraction(Date(2023, 7, 1), Date(2026, 7, 1)),
                184.0 / 365.0 + 2.0 + 181.0 / 365.0, 1e-15);
    CHECK_CLOSE(dc.year_fraction(Date(2027, 6, 1), Date(2026, 6, 1)),
                -dc.year_fraction(Date(2026, 6, 1), Date(2027, 6, 1)), 1e-15);
}

TEST_CASE(daycounter_thirty360) {
    const Core::Thirty360 bond;
    const Core::Thirty360European euro;
    CHECK(bond.day_count(Date(2026, 1, 31), Date(2026, 3, 31)) == 60);
    CHECK(euro.day_count(Date(2026, 1, 31), Date(2026, 3, 31)) == 60);
    CHECK(bond.day_count(Date(2026, 1, 15), Date(2026, 3, 31)) == 76);  // D2 = 31 conservé
    CHECK(euro.day_count(Date(2026, 1, 15), Date(2026, 3, 31)) == 75);  // D2 = 31 -> 30
    CHECK_CLOSE(bond.year_fraction(Date(2026, 1, 1), Date(2027, 1, 1)), 1.0, 1e-15);
}

TEST_CASE(daycounter_concept) {
    static_assert(Core::DayCountConvention<Core::Actual360>);
    static_assert(Core::DayCountConvention<Core::Actual365Fixed>);
    static_assert(Core::DayCountConvention<Core::ActualActual_ISDA>);
    static_assert(Core::DayCountConvention<Core::Thirty360>);
    static_assert(Core::DayCountConvention<Core::Thirty360European>);
    CHECK(true);
}


TEST_CASE(interpolators) {
    static_assert(Core::Interpolator<Core::LinearInterpolator>);
    static_assert(Core::Interpolator<Core::LogLinearInterpolator>);
    static_assert(Core::Interpolator<Core::PiecewiseConstant>);

    const std::vector<double> xs = {0.0, 1.0, 2.0};
    const std::vector<double> ys = {1.0, 0.81, 0.6561};
    using LL = Core::LogLinearInterpolator;
    std::vector<double> log_ys;
    for (const double y : ys) log_ys.push_back(LL::to_node(y));
    CHECK_CLOSE(LL::interpolate(xs, log_ys, 1, 0.5), 0.9, 1e-14);   // moyenne géométrique
    CHECK_CLOSE(LL::interpolate(xs, log_ys, 1, 1.0), 0.81, 1e-14);  // exactement sur un pilier
    CHECK_CLOSE(LL::extrapolate_right(xs, log_ys, 3.0), 0.531441, 1e-14);  // forward constant
    CHECK_CLOSE(LL::extrapolate_left(xs, log_ys, -1.0), 1.0, 1e-15);

    using PC = Core::PiecewiseConstant;
    CHECK(PC::interpolate(xs, ys, 1, 0.5) == 0.81);
    CHECK(PC::interpolate(xs, ys, 2, 1.5) == 0.6561);

    using LI = Core::LinearInterpolator;
    CHECK_CLOSE(LI::interpolate(xs, ys, 1, 0.5), 0.905, 1e-14);
    CHECK(LI::extrapolate_right(xs, ys, 5.0) == 0.6561);
}


TEST_CASE(yield_curve_basic) {
    const Date ref(2026, 1, 1);
    Curves::YieldCurve yc(ref);  // Act/365F par défaut
    yc.add_pillar(Date(2027, 1, 1), 0.97);  // t = 1.0
    yc.add_pillar(Date(2028, 1, 1), 0.94);  // t = 2.0

    CHECK_CLOSE(yc.discount(0.0), 1.0, 1e-15);
    CHECK_CLOSE(yc.discount(Date(2027, 1, 1)), 0.97, 1e-15);
    CHECK_CLOSE(yc.discount(1.0), 0.97, 1e-15);
    CHECK_CLOSE(yc.discount(1.5), std::sqrt(0.97 * 0.94), 1e-14);
    CHECK_CLOSE(yc.forward_rate(1.0, 2.0), std::log(0.97 / 0.94), 1e-14);
    CHECK_CLOSE(yc.forward_rate(Date(2027, 1, 1), Date(2028, 1, 1)), std::log(0.97 / 0.94), 1e-14);
    CHECK_CLOSE(yc.discount(3.0), 0.94 * (0.94 / 0.97), 1e-14);
}

TEST_CASE(yield_curve_validation) {
    Curves::YieldCurve yc(Date(2026, 1, 1));
    CHECK_THROWS(yc.add_pillar(Date(2027, 1, 1), 0.0), std::invalid_argument);   // DF = 0 interdit
    CHECK_THROWS(yc.add_pillar(Date(2027, 1, 1), -0.5), std::invalid_argument);
    CHECK_THROWS(yc.add_pillar(Date(2027, 1, 1), std::nan("")), std::invalid_argument);
    CHECK_THROWS(yc.add_pillar(Date(2026, 1, 1), 0.99), std::invalid_argument);  // = date de référence
    yc.add_pillar(Date(2027, 1, 1), 0.97);
    CHECK_THROWS(yc.add_pillar(Date(2026, 6, 1), 0.98), std::invalid_argument);  // non croissant
    CHECK_THROWS((void)yc.forward_rate(2.0, 1.0), std::invalid_argument);
    CHECK(yc.size() == 2);
}

TEST_CASE(yield_curve_candidate_matches_commit) {
    Curves::YieldCurve yc(Date(2026, 1, 1));
    yc.add_pillar(Date(2027, 1, 1), 0.97);
    const Date new_pillar(2029, 1, 1);
    const double t_new = yc.time_from_reference(new_pillar);
    const double df_new = 0.91;

    auto committed = yc;
    committed.add_pillar(new_pillar, df_new);
    for (double t : {0.5, 1.0, 1.7, t_new, 4.2}) {
        CHECK_CLOSE(yc.discount_with_candidate(t, t_new, df_new), committed.discount(t), 1e-15);
    }
    CHECK(yc.size() == 2);  // la courbe d'origine n'a pas bougé
}

TEST_CASE(yield_curve_other_day_counter) {
    Curves::YieldCurve<Core::Actual360> yc(Date(2026, 1, 1));
    CHECK_CLOSE(yc.time_from_reference(Date(2027, 1, 1)), 365.0 / 360.0, 1e-15);
}


TEST_CASE(credit_curve_survival) {
    Curves::CreditCurve cc("ISSUER", Date(2026, 1, 1));
    cc.add_pillar(Date(2027, 1, 1), 0.02);  // t1 = 1.0
    cc.add_pillar(Date(2031, 1, 1), 0.03);  // t2 = 1826/365
    const double t2 = cc.time_from_reference(Date(2031, 1, 1));

    CHECK_CLOSE(cc.survival_probability(0.5), std::exp(-0.01), 1e-15);
    CHECK_CLOSE(cc.survival_probability(1.0), std::exp(-0.02), 1e-15);
    CHECK_CLOSE(cc.survival_probability(3.0), std::exp(-(0.02 + 0.03 * 2.0)), 1e-15);
    CHECK_CLOSE(cc.survival_probability(t2), std::exp(-(0.02 + 0.03 * (t2 - 1.0))), 1e-15);
    CHECK_CLOSE(cc.survival_probability(6.0), std::exp(-(0.02 + 0.03 * 5.0)), 1e-15);
    CHECK_CLOSE(cc.survival_probability(0.0), 1.0, 1e-15);
    CHECK_CLOSE(cc.survival_probability(-1.0), 1.0, 1e-15);
    CHECK_CLOSE(cc.default_probability(1.0), 1.0 - std::exp(-0.02), 1e-15);
    CHECK_CLOSE(cc.survival_probability(Date(2027, 1, 1)), std::exp(-0.02), 1e-15);

    CHECK(cc.hazard_rate(0.5) == 0.02);
    CHECK(cc.hazard_rate(1.0) == 0.02);
    CHECK(cc.hazard_rate(1.0001) == 0.03);
    CHECK(cc.hazard_rate(10.0) == 0.03);
    CHECK(cc.name() == "ISSUER");
}

TEST_CASE(credit_curve_validation) {
    Curves::CreditCurve cc("X", Date(2026, 1, 1));
    CHECK_THROWS(cc.add_pillar(Date(2027, 1, 1), -0.01), std::invalid_argument);
    CHECK_THROWS(cc.add_pillar(Date(2027, 1, 1), std::nan("")), std::invalid_argument);
    CHECK_THROWS(Curves::CreditCurve("Y", Date()), std::invalid_argument);  // date nulle
    cc.add_pillar(Date(2027, 1, 1), 0.0);  // hazard nul autorisé
    CHECK_CLOSE(cc.survival_probability(1.0), 1.0, 1e-15);
}

TEST_CASE(credit_curve_candidate_matches_commit) {
    Curves::CreditCurve cc("X", Date(2026, 1, 1));
    cc.add_pillar(Date(2027, 1, 1), 0.02);
    const Date new_pillar(2029, 1, 1);
    const double lambda_new = 0.035;

    auto committed = cc;
    committed.add_pillar(new_pillar, lambda_new);
    for (double t : {0.3, 1.0, 2.2, committed.time_from_reference(new_pillar), 7.0}) {
        CHECK_CLOSE(cc.survival_probability_with_candidate(t, lambda_new),
                    committed.survival_probability(t), 1e-15);
    }
}

TEST_CASE(credit_curve_bootstrap_pattern) {
    Curves::CreditCurve cc("X", Date(2026, 1, 1));
    cc.add_pillar(Date(2027, 1, 1), 0.02);
    const double t_new = cc.time_from_reference(Date(2029, 1, 1));
    const double target_survival = 0.93;

    const Core::Brent brent(1e-14, 200);
    const double lambda = brent.solve(
        [&](double l) { return cc.survival_probability_with_candidate(t_new, l) - target_survival; },
        0.0, 1.0);
    cc.add_pillar(Date(2029, 1, 1), lambda);
    CHECK_CLOSE(cc.survival_probability(t_new), target_survival, 1e-12);
}


TEST_CASE(solvers) {
    const Core::Brent brent(1e-14, 100);
    CHECK_CLOSE(brent.solve([](double x) { return x * x - 2.0; }, 0.0, 2.0), std::sqrt(2.0), 1e-12);
    CHECK_THROWS((void)brent.solve([](double x) { return x * x + 1.0; }, 0.0, 2.0), std::runtime_error);

    const Core::NewtonRaphson newton(1e-14, 100);
    auto f = [](double x) { return std::pair{x * x - 2.0, 2.0 * x}; };
    CHECK_CLOSE(newton.solve(f, 1.0), std::sqrt(2.0), 1e-12);
    CHECK_CLOSE(newton.solve(f, -1.0), -std::sqrt(2.0), 1e-12);
    CHECK_CLOSE(newton.solve(f, -1.0, 0.1, 10.0), std::sqrt(2.0), 1e-12);
    CHECK_THROWS((void)newton.solve([](double x) { return std::pair{x * x + 1.0, 2.0 * x}; }, 0.0),
                 std::runtime_error);
    const Core::NewtonRaphson short_newton(1e-14, 20);
    CHECK_THROWS((void)short_newton.solve([](double x) { return std::pair{x * x + 1.0, 2.0 * x}; }, 0.5),
                 std::runtime_error);
}

TEST_CASE(normal_distribution) {
    CHECK_CLOSE(Core::norm_cdf(0.0), 0.5, 1e-15);
    for (double x : {-3.0, -1.3, 0.2, 2.5}) {
        CHECK_CLOSE(Core::norm_inv(Core::norm_cdf(x)), x, 1e-8);
    }
    CHECK_THROWS((void)Core::norm_inv(0.0), std::domain_error);
}

static double bivariate_reference(double a, double b, double rho) {
    const double lo = -12.0;
    const int n = 200000;  // pair
    const double h = (a - lo) / n;
    const double s = std::sqrt(1.0 - rho * rho);
    auto g = [&](double x) { return Core::norm_pdf(x) * Core::norm_cdf((b - rho * x) / s); };
    double sum = g(lo) + g(a);
    for (int i = 1; i < n; ++i) sum += g(lo + i * h) * ((i % 2) ? 4.0 : 2.0);
    return sum * h / 3.0;
}

TEST_CASE(bivariate_normal_all_correlations) {
    const std::pair<double, double> points[] = {{0.3, -0.2}, {-1.0, 0.5}, {1.5, 1.2}, {-0.7, -1.1}};
    for (const auto& [a, b] : points) {
        for (double rho : {-0.99, -0.95, -0.93, -0.5, 0.0, 0.5, 0.9, 0.93, 0.95, 0.99}) {
            const double got = Core::bivariate_norm_cdf(a, b, rho);
            const double ref = bivariate_reference(a, b, rho);
            if (std::abs(got - ref) > 1e-9) {
                std::printf("  BVN(%.2f, %.2f, %.2f) = %.12f, reference %.12f\n", a, b, rho, got, ref);
            }
            CHECK_CLOSE(got, ref, 1e-9);
        }
        CHECK_CLOSE(Core::bivariate_norm_cdf(a, b, 0.0), Core::norm_cdf(a) * Core::norm_cdf(b), 1e-14);
    }
}

TEST_CASE(gauss_hermite_normal) {
    CHECK_CLOSE(Core::gauss_hermite_normal_expectation([](double) { return 1.0; }), 1.0, 1e-10);
    CHECK_CLOSE(Core::gauss_hermite_normal_expectation([](double z) { return z * z; }), 1.0, 1e-10);
    CHECK_CLOSE(Core::gauss_hermite_normal_expectation([](double z) { return std::exp(z); }),
                std::exp(0.5), 1e-10);
}

int main() {
    return TestFramework::run_all();
}
