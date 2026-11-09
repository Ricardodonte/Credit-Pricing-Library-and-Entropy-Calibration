
#include "tests/test_framework.h"

#include "Core/ImmDates.h"
#include "Core/Period.h"
#include "Core/Schedule.h"
#include "Core/calendar.h"
#include "Curves/Bootstrap/CurveBootstrappers.h"
#include "Instruments/Credit/Cds.h"
#include "Instruments/Credit/CdsQuote.h"
#include "Instruments/Rates/Deposit.h"
#include "Instruments/Rates/Fra.h"
#include "Instruments/Rates/Swap.h"
#include "Pricing/Credit/IsdaCdsEngine.h"
#include "Pricing/Credit/PreparedCds.h"
#include "Pricing/Rates/DiscountingPricer.h"

#include <cmath>
#include <stdexcept>
#include <vector>

using Core::Date;
using Core::Period;
using Core::TimeUnit;
using namespace Curves::Bootstrap;
using Instruments::Credit::CdsQuote;

namespace {
    const Date trade(2026, 9, 29);  // Tuesday

    Curves::YieldCurve<> flat_yield_curve(double rate, Date ref = trade) {
        Curves::YieldCurve<> yc(ref);
        for (int y = 1; y <= 15; ++y) {
            const Date d = ref.add_years(y);
            yc.add_pillar(d, std::exp(-rate * yc.time_from_reference(d)));
        }
        return yc;
    }

    const std::vector<Period> cds_tenors = {{6, TimeUnit::Months}, {1, TimeUnit::Years}, {2, TimeUnit::Years},
                                            {3, TimeUnit::Years},  {5, TimeUnit::Years}, {7, TimeUnit::Years},
                                            {10, TimeUnit::Years}};
    const std::vector<double> true_hazards = {0.010, 0.012, 0.015, 0.018, 0.022, 0.025, 0.027};

    Curves::CreditCurve<> true_credit_curve() {
        Curves::CreditCurve<> cc("TRUE", trade);
        for (std::size_t i = 0; i < cds_tenors.size(); ++i)
            cc.add_pillar(Core::cds_standard_maturity(trade, cds_tenors[i]), true_hazards[i]);
        return cc;
    }
}


TEST_CASE(period_parse_and_add) {
    CHECK(Period::parse("3M") == Period(3, TimeUnit::Months));
    CHECK(Period::parse("5y") == Period(5, TimeUnit::Years));
    CHECK(Period::parse("-1W") == Period(-1, TimeUnit::Weeks));
    CHECK_THROWS(Period::parse("M"), std::invalid_argument);
    CHECK_THROWS(Period::parse("3X"), std::invalid_argument);
    CHECK(Period(10, TimeUnit::Years).to_string() == "10Y");
    CHECK(Date(2026, 1, 31) + Period(1, TimeUnit::Months) == Date(2026, 2, 28));
    CHECK(Date(2026, 1, 31) - Period(2, TimeUnit::Weeks) == Date(2026, 1, 17));
    CHECK(Period(2, TimeUnit::Years).months() == 24);
    CHECK_THROWS((void)Period(3, TimeUnit::Days).months(), std::invalid_argument);
    CHECK(Core::to_period(Core::Frequency::Quarterly) == Period(3, TimeUnit::Months));
}

TEST_CASE(schedule_backward) {
    const Core::TargetCalendar cal;
    const auto s = Core::make_schedule(Date(2026, 10, 1), Date(2029, 10, 1), Period(1, TimeUnit::Years), cal);
    CHECK(s.size() == 4);
    CHECK(s[1] == Date(2027, 10, 1));
    CHECK(s[2] == Date(2028, 10, 2));
    CHECK(s.back() == Date(2029, 10, 1));
    const auto stub = Core::make_schedule(Date(2026, 12, 1), Date(2028, 10, 2), Period(1, TimeUnit::Years), cal);
    CHECK(stub.size() == 3);
    CHECK(stub[1] == Date(2027, 10, 4));  // 2027-10-02 is a Saturday -> Monday
    CHECK_THROWS(Core::make_schedule(Date(2027, 1, 1), Date(2026, 1, 1), Period(1, TimeUnit::Years), cal),
                 std::invalid_argument);
}

TEST_CASE(imm_dates) {
    CHECK(Core::previous_imm_date(Date(2026, 9, 29)) == Date(2026, 9, 20));
    CHECK(Core::previous_imm_date(Date(2026, 9, 20)) == Date(2026, 9, 20));
    CHECK(Core::previous_imm_date(Date(2026, 3, 5)) == Date(2025, 12, 20));
    CHECK(Core::next_imm_date(Date(2026, 9, 20)) == Date(2026, 12, 20));
    CHECK(Core::next_imm_date(Date(2026, 12, 25)) == Date(2027, 3, 20));
    CHECK(Core::next_imm_date(Date(2026, 1, 2)) == Date(2026, 3, 20));
    CHECK(Core::is_imm_date(Date(2026, 6, 20)) && !Core::is_imm_date(Date(2026, 7, 20)));
}

TEST_CASE(cds_standard_maturity_2015_rule) {
    const Period y5(5, TimeUnit::Years);
    CHECK(Core::cds_standard_maturity(Date(2026, 9, 29), y5) == Date(2031, 12, 20));
    CHECK(Core::cds_standard_maturity(Date(2026, 2, 10), y5) == Date(2030, 12, 20));
    CHECK(Core::cds_standard_maturity(Date(2026, 3, 20), y5) == Date(2031, 6, 20));  // roll date itself
    CHECK(Core::cds_standard_maturity(Date(2026, 3, 19), y5) == Date(2030, 12, 20));
    CHECK(Core::cds_standard_maturity(Date(2026, 6, 1), Period(1, TimeUnit::Years)) == Date(2027, 6, 20));
    CHECK(Core::cds_standard_maturity(Date(2026, 9, 29), Period(6, TimeUnit::Months)) == Date(2027, 6, 20));
}


TEST_CASE(cds_contract_schedule) {
    const auto cds = Instruments::Credit::make_standard_cds(trade, Period(5, TimeUnit::Years), 0.01, 0.4);
    CHECK(cds.step_in_date == Date(2026, 9, 30));
    CHECK(cds.cash_settlement_date == Date(2026, 10, 2));  // T+3 business days
    CHECK(cds.maturity == Date(2031, 12, 20));
    CHECK(cds.periods.front().accrual_start == Date(2026, 9, 21));
    CHECK(cds.periods.front().payment == Date(2026, 12, 21));  // 2026-12-20 is a Sunday
    CHECK(cds.periods.size() == 21);
    CHECK(cds.periods.back().accrual_end == Date(2031, 12, 21));
    CHECK(cds.periods.back().payment == Date(2031, 12, 22));
    CHECK_CLOSE(cds.accrued_at_step_in(), 9.0 / 360.0, 1e-15);
    for (std::size_t i = 1; i < cds.periods.size(); ++i)
        CHECK(cds.periods[i].accrual_start == cds.periods[i - 1].accrual_end);
    CHECK_THROWS(Instruments::Credit::make_cds(trade, Date(2026, 9, 30), 0.01, 0.4), std::invalid_argument);
    CHECK_THROWS(Instruments::Credit::make_cds(trade, Date(2031, 12, 20), 0.01, 1.0), std::invalid_argument);
}


TEST_CASE(cds_engine_matches_brute_force_integration) {
    const auto yc = flat_yield_curve(0.03);
    const auto cc = true_credit_curve();
    const auto cds = Instruments::Credit::make_standard_cds(trade, Period(5, TimeUnit::Years), 0.01, 0.4);
    const auto v = Pricing::Credit::IsdaCdsEngine(yc, cc).value(cds);

    double protection = 0.0, aod = 0.0;
    const int steps_per_day = 8;
    for (Date d = cds.protection_start; d < cds.maturity; ++d) {
        for (int s = 0; s < steps_per_day; ++s) {
            const double t0 = yc.time_from_reference(d) + s / (365.0 * steps_per_day);
            const double t1 = t0 + 1.0 / (365.0 * steps_per_day);
            const double dq = cc.survival_probability(t0) - cc.survival_probability(t1);
            const double p = 0.5 * (yc.discount(t0) + yc.discount(t1));
            protection += p * dq;
            const auto it = std::ranges::upper_bound(cds.periods, d, {}, &Instruments::Credit::CdsPeriod::accrual_start);
            const double tau = (static_cast<double>(d - std::prev(it)->accrual_start) + (s + 0.5) / steps_per_day) / 360.0;
            aod += tau * p * dq;
        }
    }
    double coupons = 0.0;
    for (const auto& p : cds.periods)
        coupons += p.accrual * yc.discount(p.payment) * cc.survival_probability(std::min(p.accrual_end, cds.maturity));

    CHECK_CLOSE(v.protection_leg, 0.6 * protection, 1e-9);
    CHECK_CLOSE(v.rpv01_dirty, coupons + aod, 1e-9);
}

TEST_CASE(cds_engine_consistency) {
    const auto yc = flat_yield_curve(0.03);
    Curves::CreditCurve<> flat("FLAT", trade);
    flat.add_pillar(Date(2040, 1, 1), 0.02);

    const auto cds = Instruments::Credit::make_standard_cds(trade, Period(5, TimeUnit::Years), 0.01, 0.4);
    const auto v = Pricing::Credit::IsdaCdsEngine(yc, flat).value(cds);

    CHECK_CLOSE(v.par_spread(), 0.02 * 0.6 * 360.0 / 365.0, 1e-4);
    CHECK_CLOSE(v.points_upfront(), (v.par_spread() - 0.01) * v.rpv01_clean() / v.df_cash_settlement, 1e-15);
    const auto par_cds = Instruments::Credit::make_standard_cds(trade, Period(5, TimeUnit::Years), v.par_spread(), 0.4);
    CHECK_CLOSE(Pricing::Credit::IsdaCdsEngine(yc, flat).value(par_cds).points_upfront(), 0.0, 1e-15);
    CHECK_CLOSE(v.cash_settlement_amount(), v.points_upfront() - 0.01 * v.accrued, 1e-15);
    CHECK(v.rpv01_clean() > 4.0 && v.rpv01_clean() < 5.0);

    Curves::YieldCurve<> other(Date(2026, 9, 28));
    CHECK_THROWS((void)Pricing::Credit::IsdaCdsEngine(other, flat).value(cds), std::invalid_argument);
}


namespace {
    using YieldBootstrapper = YieldCurveBootstrapper<>;

    std::vector<YieldBootstrapper::helper_type> eur_rate_helpers(double shift = 0.0) {
        using namespace Instruments::Rates;
        const Core::TargetCalendar cal;
        std::vector<YieldBootstrapper::helper_type> helpers;
        helpers.emplace_back(DepositHelper(make_deposit(trade, Period(1, TimeUnit::Weeks), 0.0195 + shift, cal)));
        helpers.emplace_back(DepositHelper(make_deposit(trade, Period(1, TimeUnit::Months), 0.0198 + shift, cal)));
        helpers.emplace_back(DepositHelper(make_deposit(trade, Period(3, TimeUnit::Months), 0.0201 + shift, cal)));
        helpers.emplace_back(DepositHelper(make_deposit(trade, Period(6, TimeUnit::Months), 0.0205 + shift, cal)));
        helpers.emplace_back(FraHelper(make_fra(trade, Period(6, TimeUnit::Months), Period(9, TimeUnit::Months),
                                                0.0209 + shift, cal)));
        const std::vector<std::pair<int, double>> swaps = {{1, 0.0212}, {2, 0.0220}, {3, 0.0228}, {5, 0.0241},
                                                           {7, 0.0252}, {10, 0.0265}, {15, 0.0276}, {20, 0.0280}};
        for (const auto& [years, rate] : swaps)
            helpers.emplace_back(SwapHelper(make_swap(trade, Period(years, TimeUnit::Years), rate + shift, cal)));
        return helpers;
    }
}

TEST_CASE(yield_curve_bootstrap_reprices_inputs) {
    const YieldBootstrapper bootstrapper;
    auto helpers = eur_rate_helpers();
    std::ranges::reverse(helpers);  // input order must not matter
    const auto result = bootstrapper.bootstrap(Curves::YieldCurve<>(trade), helpers);

    CHECK(result.report.size() == 13);
    CHECK(result.curve.size() == 14);  // reference pillar + 13
    CHECK(result.max_abs_error() < 1e-12);
    for (const auto& r : result.report) {
        if (std::abs(r.error()) >= 1e-12) std::printf("  %s : error %.3e\n", r.instrument.c_str(), r.error());
    }
    const auto df = Pricing::Rates::discount_function(result.curve);
    const auto swap5y = Instruments::Rates::make_swap(trade, Period(5, TimeUnit::Years), 0.0241, Core::TargetCalendar{});
    CHECK_CLOSE(Pricing::Rates::par_rate(swap5y, df), 0.0241, 1e-12);
    CHECK_CLOSE(Pricing::Rates::npv_receive_fixed(swap5y, df), 0.0, 1e-12);
    const auto values = result.curve.pillar_values();
    for (std::size_t i = 1; i < values.size(); ++i) CHECK(values[i] < values[i - 1]);
}

TEST_CASE(yield_curve_bootstrap_negative_rates) {
    const YieldBootstrapper bootstrapper;
    const auto result = bootstrapper.bootstrap(Curves::YieldCurve<>(trade), eur_rate_helpers(-0.030));
    CHECK(result.max_abs_error() < 1e-12);
    CHECK(result.curve.pillar_values().back() > 1.0);  // DF > 1 with negative rates
}

TEST_CASE(yield_curve_bootstrap_errors) {
    using namespace Instruments::Rates;
    const Core::TargetCalendar cal;
    const YieldBootstrapper bootstrapper;
    std::vector<YieldBootstrapper::helper_type> duplicated = {
        DepositHelper(make_deposit(trade, Period(1, TimeUnit::Years), 0.02, cal)),
        SwapHelper(make_swap(trade, Period(1, TimeUnit::Years), 0.02, cal))};
    CHECK_THROWS((void)bootstrapper.bootstrap(Curves::YieldCurve<>(trade), duplicated), std::invalid_argument);
    CHECK_THROWS((void)bootstrapper.bootstrap(Curves::YieldCurve<>(trade), {}), std::invalid_argument);
    std::vector<YieldBootstrapper::helper_type> absurd = {
        DepositHelper(make_deposit(trade, Period(1, TimeUnit::Months), -50.0, cal))};
    CHECK_THROWS((void)bootstrapper.bootstrap(Curves::YieldCurve<>(trade), absurd), BootstrapError);
}


namespace {
    using CreditBootstrapper = CreditCurveBootstrapper<Curves::YieldCurve<>>;
}

TEST_CASE(credit_curve_bootstrap_round_trip_par_spreads) {
    const auto yc = flat_yield_curve(0.03);
    const auto truth = true_credit_curve();
    const Pricing::Credit::IsdaCdsEngine engine(yc, truth);

    std::vector<CreditBootstrapper::helper_type> helpers;
    for (const auto& tenor : cds_tenors) {
        const auto cds = Instruments::Credit::make_standard_cds(trade, tenor, 0.01, 0.4);
        const double spread = engine.value(cds).par_spread();
        helpers.emplace_back(CdsHelper<Curves::YieldCurve<>>::standard(trade, CdsQuote::par_spread(tenor, spread), yc));
    }
    const auto result = CreditBootstrapper().bootstrap(Curves::CreditCurve<>("ISSUER", trade), helpers);

    CHECK(result.max_abs_error() < 1e-12);
    const auto hazards = result.curve.pillar_values();
    for (std::size_t i = 0; i < true_hazards.size(); ++i) CHECK_CLOSE(hazards[i + 1], true_hazards[i], 1e-10);
}

TEST_CASE(credit_curve_bootstrap_round_trip_upfronts) {
    const auto yc = flat_yield_curve(0.025);
    const auto truth = true_credit_curve();
    const Pricing::Credit::IsdaCdsEngine engine(yc, truth);

    std::vector<CreditBootstrapper::helper_type> helpers;
    for (const auto& tenor : cds_tenors) {
        const auto cds = Instruments::Credit::make_standard_cds(trade, tenor, 0.01, 0.4);  // 100bp standard coupon
        const double points = engine.value(cds).points_upfront();
        helpers.emplace_back(CdsHelper<Curves::YieldCurve<>>::standard(trade, CdsQuote::upfront(tenor, points, 0.01), yc));
    }
    const auto result = CreditBootstrapper().bootstrap(Curves::CreditCurve<>("ISSUER", trade), helpers);

    CHECK(result.max_abs_error() < 1e-12);
    const auto hazards = result.curve.pillar_values();
    for (std::size_t i = 0; i < true_hazards.size(); ++i) CHECK_CLOSE(hazards[i + 1], true_hazards[i], 1e-10);
}

TEST_CASE(credit_curve_bootstrap_on_bootstrapped_yield_curve) {
    const auto yield = YieldBootstrapper().bootstrap(Curves::YieldCurve<>(trade), eur_rate_helpers());
    std::vector<CreditBootstrapper::helper_type> helpers;
    const std::vector<double> spreads = {0.0060, 0.0070, 0.0085, 0.0100, 0.0125, 0.0140, 0.0150};
    for (std::size_t i = 0; i < cds_tenors.size(); ++i)
        helpers.emplace_back(CdsHelper<Curves::YieldCurve<>>::standard(
            trade, CdsQuote::par_spread(cds_tenors[i], spreads[i]), yield.curve));
    const auto result = CreditBootstrapper().bootstrap(Curves::CreditCurve<>("ISSUER", trade), helpers);

    CHECK(result.max_abs_error() < 1e-12);
    for (const double h : result.curve.pillar_values()) CHECK(h >= 0.0);
    CHECK(result.curve.survival_probability(Date(2031, 12, 20)) < result.curve.survival_probability(Date(2028, 12, 20)));
}

TEST_CASE(credit_curve_bootstrap_errors) {
    const auto yc = flat_yield_curve(0.03);
    std::vector<CreditBootstrapper::helper_type> helpers = {CdsHelper<Curves::YieldCurve<>>::standard(
        trade, CdsQuote::upfront(Period(5, TimeUnit::Years), -0.30, 0.01), yc)};
    CHECK_THROWS((void)CreditBootstrapper().bootstrap(Curves::CreditCurve<>("X", trade), helpers), BootstrapError);
    const auto cds = Instruments::Credit::make_standard_cds(trade, Period(5, TimeUnit::Years), 0.05, 0.4);
    CHECK_THROWS(CdsHelper<Curves::YieldCurve<>>(cds, CdsQuote::upfront(Period(5, TimeUnit::Years), 0.0, 0.01), yc),
                 std::invalid_argument);
}


TEST_CASE(prepared_cds_matches_engine) {
    const auto yc = flat_yield_curve(0.03);
    const auto cc = true_credit_curve();
    for (const int years : {1, 5, 10}) {
        const auto cds = Instruments::Credit::make_standard_cds(trade, Period(years, TimeUnit::Years), 0.01, 0.4);
        const Pricing::Credit::PreparedCds prepared(cds, yc, cc.pillar_dates());
        const auto a = prepared.value(cc);
        const auto b = Pricing::Credit::IsdaCdsEngine(yc, cc).value(cds);
        CHECK(a.protection_leg == b.protection_leg);
        CHECK(a.rpv01_dirty == b.rpv01_dirty);
        CHECK(a.df_cash_settlement == b.df_cash_settlement);
        CHECK(a.points_upfront() == b.points_upfront());
    }
}

TEST_CASE(prepared_cds_reused_on_several_credit_curves) {
    const auto yc = flat_yield_curve(0.03);
    const auto truth = true_credit_curve();
    const auto cds = Instruments::Credit::make_standard_cds(trade, Period(5, TimeUnit::Years), 0.01, 0.4);
    const Pricing::Credit::PreparedCds prepared(cds, yc, truth.pillar_dates());

    for (const double shock : {0.5, 1.0, 2.0}) {
        Curves::CreditCurve<> shocked("SHOCKED", trade);
        const auto dates = truth.pillar_dates();
        const auto hazards = truth.pillar_values();
        for (std::size_t i = 1; i < dates.size(); ++i) shocked.add_pillar(dates[i], shock * hazards[i]);
        CHECK(prepared.value(shocked).npv() == Pricing::Credit::IsdaCdsEngine(yc, shocked).value(cds).npv());
    }
    Curves::CreditCurve<> doubled("DOUBLED", trade);
    for (std::size_t i = 1; i < truth.pillar_dates().size(); ++i)
        doubled.add_pillar(truth.pillar_dates()[i], 2.0 * truth.pillar_values()[i]);
    CHECK(prepared.value(doubled).npv() > prepared.value(truth).npv());
}

TEST_CASE(prepared_cds_pillar_checks) {
    const auto yc = flat_yield_curve(0.03);
    const auto truth = true_credit_curve();
    const auto cds = Instruments::Credit::make_standard_cds(trade, Period(5, TimeUnit::Years), 0.01, 0.4);
    const Pricing::Credit::PreparedCds prepared(cds, yc, truth.pillar_dates());
    for (const Date d : prepared.credit_pillars()) CHECK(cds.protection_start < d && d < cds.maturity);

    Curves::CreditCurve<> longer = true_credit_curve();
    longer.add_pillar(Date(2050, 1, 1), 0.03);
    CHECK(prepared.value(longer).npv() == prepared.value(truth).npv());

    Curves::CreditCurve<> other("OTHER", trade);
    other.add_pillar(Date(2028, 1, 15), 0.01);
    other.add_pillar(Date(2035, 1, 1), 0.02);
    CHECK_THROWS((void)prepared.value(other), std::invalid_argument);

    Curves::CreditCurve<> flat("FLAT", trade);
    flat.add_pillar(Date(2040, 1, 1), 0.02);
    CHECK_THROWS((void)prepared.value(flat), std::invalid_argument);
    const Pricing::Credit::PreparedCds prepared_flat(cds, yc, flat.pillar_dates());
    CHECK(prepared_flat.value(flat).npv() == Pricing::Credit::IsdaCdsEngine(yc, flat).value(cds).npv());

    const Curves::CreditCurve<> shifted("SHIFTED", Date(2026, 9, 28));
    CHECK_THROWS((void)prepared.value(shifted), std::invalid_argument);
    CHECK_THROWS(Pricing::Credit::PreparedCds(cds, flat_yield_curve(0.03, Date(2026, 9, 28)), truth.pillar_dates()),
                 std::invalid_argument);
}
