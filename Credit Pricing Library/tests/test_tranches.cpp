
#include "tests/test_framework.h"

#include "Core/Period.h"
#include "Curves/BaseCorrelationCurve.h"
#include "Curves/Bootstrap/BaseCorrelationBootstrapper.h"
#include "Curves/CreditCurve.h"
#include "Curves/YieldCurve.h"
#include "Instruments/Credit/Cds.h"
#include "Instruments/Credit/CreditIndex.h"
#include "Instruments/Credit/Tranche.h"
#include "Instruments/Credit/TrancheQuote.h"
#include "Models/Credit/TrancheLossModels.h"
#include "Pricing/Credit/IsdaCdsEngine.h"
#include "Pricing/Credit/PreparedTranche.h"
#include "Pricing/Credit/ReferencePool.h"

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

using Core::Date;
using Core::Period;
using Core::TimeUnit;
using Instruments::Credit::CreditIndex;
using Instruments::Credit::TrancheQuote;
using namespace Models::Credit;

namespace {
    const Date trade(2026, 9, 29);
    const Period five_years(5, TimeUnit::Years);

    Curves::YieldCurve<> flat_yield(double rate) {
        Curves::YieldCurve<> yc(trade);
        for (int y = 1; y <= 12; ++y) {
            const Date d = trade.add_years(y);
            yc.add_pillar(d, std::exp(-rate * yc.time_from_reference(d)));
        }
        return yc;
    }

    Curves::CreditCurve<> flat_credit(double hazard) {
        Curves::CreditCurve<> cc("NAME", trade);
        cc.add_pillar(Date(2040, 1, 1), hazard);
        return cc;
    }

    std::vector<Curves::CreditCurve<>> pool_curves(std::size_t n, double hazard, double dispersion) {
        std::vector<Curves::CreditCurve<>> curves;
        for (std::size_t i = 0; i < n; ++i) {
            const double x = n > 1 ? static_cast<double>(i) / static_cast<double>(n - 1) - 0.5 : 0.0;
            Curves::CreditCurve<> cc("NAME", trade);
            cc.add_pillar(Date(2028, 12, 20), hazard * (1.0 + dispersion * x));
            cc.add_pillar(Date(2040, 1, 1), 1.3 * hazard * (1.0 + dispersion * x));
            curves.push_back(std::move(cc));
        }
        return curves;
    }

    double brute_force_expected_tranche_loss(const std::vector<double>& p, const std::vector<double>& l, double k) {
        const std::size_t n = p.size();
        double result = 0.0;
        for (unsigned mask = 0; mask < (1u << n); ++mask) {
            double prob = 1.0, loss = 0.0;
            for (std::size_t i = 0; i < n; ++i) {
                const bool defaulted = (mask >> i) & 1u;
                prob *= defaulted ? p[i] : 1.0 - p[i];
                if (defaulted) loss += l[i];
            }
            result += prob * std::min(loss, k);
        }
        return result;
    }
}


TEST_CASE(credit_index_and_tranche_contract) {
    const auto index = CreditIndex::homogeneous("ITRAXX", 125, 0.4);
    CHECK(index.size() == 125);
    CHECK_CLOSE(index[0].weight, 0.008, 1e-15);
    CHECK_THROWS(CreditIndex("BAD", {{"A", 0.5, 0.4}, {"B", 0.4, 0.4}}), std::invalid_argument);
    CHECK_THROWS(CreditIndex("BAD", {{"A", 1.0, 1.0}}), std::invalid_argument);

    const auto tranche = Instruments::Credit::make_standard_tranche(trade, five_years, 0.03, 0.06, 0.01);
    const auto cds = Instruments::Credit::make_standard_cds(trade, five_years, 0.01, 0.4);
    CHECK(tranche.maturity == cds.maturity);
    CHECK(tranche.periods.size() == cds.periods.size());
    CHECK_CLOSE(tranche.width(), 0.03, 1e-15);
    CHECK(!tranche.is_equity());
    CHECK_THROWS(Instruments::Credit::make_standard_tranche(trade, five_years, 0.06, 0.03, 0.01), std::invalid_argument);
    CHECK_THROWS(Instruments::Credit::make_standard_tranche(trade, five_years, 0.0, 1.2, 0.01), std::invalid_argument);
}


TEST_CASE(loss_recursion_matches_enumeration) {
    const std::vector<double> p = {0.02, 0.10, 0.25, 0.05, 0.40, 0.15, 0.08, 0.30, 0.12, 0.01};
    const std::vector<double> l = {0.01, 0.02, 0.03, 0.01, 0.02, 0.03, 0.01, 0.02, 0.03, 0.01};
    const LossRecursion recursion;
    for (const double k : {0.005, 0.01, 0.025, 0.04, 0.07, 0.19, 0.5})
        CHECK_CLOSE(recursion.expected_tranche_loss(p, l, k), brute_force_expected_tranche_loss(p, l, k), 1e-15);
    CHECK(recursion.expected_tranche_loss(p, l, 0.0) == 0.0);
    CHECK_THROWS(LossRecursion(0), std::invalid_argument);
}

TEST_CASE(conditional_models_agree_on_homogeneous_pool) {
    const std::size_t n = 100;
    const std::vector<double> p(n, 0.07);
    const std::vector<double> l(n, 0.6 / static_cast<double>(n));
    const LossRecursion recursion;
    const AdjustedBinomial binomial;
    const NormalApproximation normal;
    for (const double k : {0.01, 0.03, 0.06, 0.12}) {
        const double exact = recursion.expected_tranche_loss(p, l, k);
        CHECK_CLOSE(binomial.expected_tranche_loss(p, l, k), exact, 1e-13);  // alpha = 1: plain binomial, exact
        CHECK_CLOSE(normal.expected_tranche_loss(p, l, k), exact, 5e-4);
    }
    for (const double el : {recursion.expected_tranche_loss(p, l, 1.0), binomial.expected_tranche_loss(p, l, 1.0),
                            normal.expected_tranche_loss(p, l, 1.0)})
        CHECK_CLOSE(el, 0.6 * 0.07, 1e-6);
}

TEST_CASE(adjusted_binomial_on_heterogeneous_pool) {
    std::vector<double> p, l;
    for (int i = 0; i < 60; ++i) {
        p.push_back(0.02 + 0.002 * i);
        l.push_back((i % 3 == 0 ? 0.02 : 0.01) * 0.6);
    }
    const LossRecursion exact(1);  // losses are 1 or 2 units of 0.006: the lattice is exact
    const AdjustedBinomial binomial;
    for (const double k : {0.02, 0.05, 0.1}) {
        const double reference = exact.expected_tranche_loss(p, l, k);
        CHECK(std::abs(binomial.expected_tranche_loss(p, l, k) - reference) < 0.01 * reference);
    }
}


TEST_CASE(gaussian_copula_properties) {
    const std::size_t n = 40;
    std::vector<double> pd, lgd;
    for (std::size_t i = 0; i < n; ++i) {
        pd.push_back(0.03 + 0.001 * static_cast<double>(i));
        lgd.push_back(0.6 / static_cast<double>(n));
    }
    const PoolSnapshot pool{pd, lgd};
    const GaussianCopulaRecursion copula;
    const auto state = copula.prepare(pool);

    CHECK_CLOSE(copula.expected_tranche_loss(state, 0.03, 0.0), LossRecursion().expected_tranche_loss(pd, lgd, 0.03), 1e-11);  // N^-1 (Acklam) at 1e-9
    double expected_loss = 0.0;
    for (std::size_t i = 0; i < n; ++i) expected_loss += pd[i] * lgd[i];
    for (const double rho : {0.0, 0.3, 0.6, 0.9})
        CHECK_CLOSE(copula.expected_tranche_loss(state, 1.0, rho), expected_loss, 1e-6);
    double previous = 1.0;
    for (const double rho : {0.0, 0.2, 0.4, 0.6, 0.8}) {
        const double el = copula.expected_tranche_loss(state, 0.03, rho);
        CHECK(el < previous && el <= 0.03);
        previous = el;
    }
    const std::vector<double> zero(n, 0.0);
    CHECK(copula.expected_tranche_loss(copula.prepare({zero, lgd}), 0.03, 0.5) == 0.0);
}

TEST_CASE(lhp_is_the_large_pool_limit) {
    const std::size_t n = 2000;
    const std::vector<double> pd(n, 0.05);
    const std::vector<double> lgd(n, 0.6 / static_cast<double>(n));
    const PoolSnapshot pool{pd, lgd};
    const LhpModel lhp;
    const GaussianCopulaNormal copula;
    const auto lhp_state = lhp.prepare(pool);
    const auto copula_state = copula.prepare(pool);
    for (const double rho : {0.1, 0.3, 0.6})
        for (const double k : {0.03, 0.07, 0.15})
            CHECK_CLOSE(lhp.expected_tranche_loss(lhp_state, k, rho),
                        copula.expected_tranche_loss(copula_state, k, rho), 2e-4);
    CHECK_CLOSE(lhp.expected_tranche_loss(lhp_state, 0.03, 0.0), 0.03, 1e-15);  // E[L] = 3% >= K
    CHECK_CLOSE(lhp.expected_tranche_loss(lhp_state, 1.0, 0.4), 0.6 * 0.05, 1e-15);
}


TEST_CASE(tranche_without_default_risk) {
    const auto yc = flat_yield(0.03);
    const std::vector<Curves::CreditCurve<>> curves(10, flat_credit(0.0));
    const Pricing::Credit::ReferencePool pool(CreditIndex::homogeneous("IDX", 10, 0.4), std::span(curves));
    const auto tranche = Instruments::Credit::make_standard_tranche(trade, five_years, 0.03, 0.07, 0.01);
    const Pricing::Credit::PreparedTranche<GaussianCopulaRecursion> prepared(tranche, yc, pool);

    const auto v = prepared.value(0.3, 0.4);
    double annuity = 0.0;
    for (const auto& p : tranche.periods) annuity += p.accrual * yc.discount(p.payment);
    CHECK_CLOSE(v.protection_leg, 0.0, 1e-15);
    CHECK_CLOSE(v.rpv01_dirty, annuity, 1e-13);
    CHECK_CLOSE(v.points_upfront(), -0.01 * v.rpv01_clean() / v.df_cash_settlement, 1e-15);
}

TEST_CASE(full_index_tranche_matches_cds_protection) {
    const auto yc = flat_yield(0.03);
    const auto name = flat_credit(0.02);
    const std::vector<Curves::CreditCurve<>> curves(20, name);
    const Pricing::Credit::ReferencePool pool(CreditIndex::homogeneous("IDX", 20, 0.4), std::span(curves));
    const auto tranche = Instruments::Credit::make_standard_tranche(trade, five_years, 0.0, 1.0, 0.01);
    const Pricing::Credit::PreparedTranche<LhpModel> prepared(tranche, yc, pool);
    const auto cds = Instruments::Credit::make_standard_cds(trade, five_years, 0.01, 0.4);
    const auto cds_value = Pricing::Credit::IsdaCdsEngine(yc, name).value(cds);
    for (const double rho : {0.0, 0.5, 0.9})
        CHECK_CLOSE(prepared.value(rho).protection_leg, cds_value.protection_leg, 2e-6);  // trapezoid, monthly grid
}

TEST_CASE(tranche_decomposition_and_quotes) {
    const auto yc = flat_yield(0.03);
    const auto curves = pool_curves(30, 0.015, 0.8);
    const Pricing::Credit::ReferencePool pool(CreditIndex::homogeneous("IDX", 30, 0.4), std::span(curves));
    const auto make = [&](double k1, double k2) {
        return Pricing::Credit::PreparedTranche<GaussianCopulaAdjBinomial>(
            Instruments::Credit::make_standard_tranche(trade, five_years, k1, k2, 0.01), yc, pool);
    };
    const double rho = 0.35;
    const auto equity = make(0.0, 0.03).value(rho);
    const auto mezz = make(0.03, 0.07).value(rho);
    const auto base = make(0.0, 0.07).value(rho);
    CHECK_CLOSE(0.03 * equity.protection_leg + 0.04 * mezz.protection_leg, 0.07 * base.protection_leg, 1e-14);
    CHECK_CLOSE(0.03 * equity.rpv01_dirty + 0.04 * mezz.rpv01_dirty, 0.07 * base.rpv01_dirty, 1e-13);
    CHECK(equity.par_spread() > mezz.par_spread());
    CHECK_CLOSE(mezz.points_upfront(), (mezz.par_spread() - 0.01) * mezz.rpv01_clean() / mezz.df_cash_settlement, 1e-14);

    Curves::BaseCorrelationCurve<> bc("IDX 5Y");
    bc.add_pillar(0.03, 0.25);
    bc.add_pillar(0.07, 0.45);
    const auto prepared = make(0.03, 0.07);
    CHECK(prepared.value(bc).npv() == prepared.value(0.25, 0.45).npv());
    CHECK_CLOSE(bc.correlation(0.05), 0.35, 1e-15);
    CHECK_CLOSE(bc.correlation(0.01), 0.25, 1e-15);  // flat extrapolation
    CHECK_THROWS(bc.add_pillar(0.05, 0.3), std::invalid_argument);
    CHECK_THROWS(bc.add_pillar(0.10, 1.2), std::invalid_argument);

    const Curves::YieldCurve<> other(Date(2026, 9, 28));
    CHECK_THROWS(Pricing::Credit::PreparedTranche<LhpModel>(
                     Instruments::Credit::make_standard_tranche(trade, five_years, 0.0, 0.03, 0.05), other, pool),
                 std::invalid_argument);
}


namespace {
    template <class Model>
    void check_base_correlation_round_trip(bool par_quotes) {
        using namespace Curves::Bootstrap;
        const auto yc = flat_yield(0.025);
        const auto curves = pool_curves(25, 0.012, 0.6);
        const Pricing::Credit::ReferencePool pool(CreditIndex::homogeneous("IDX", 25, 0.4), std::span(curves));

        const std::vector<double> strikes = {0.0, 0.03, 0.06, 0.12, 0.22};
        Curves::BaseCorrelationCurve<> truth("TRUE");
        const std::vector<double> rhos = {0.22, 0.31, 0.43, 0.58};
        for (std::size_t i = 0; i < rhos.size(); ++i) truth.add_pillar(strikes[i + 1], rhos[i]);

        std::vector<TrancheHelper<Model>> helpers;
        for (std::size_t i = rhos.size(); i-- > 0;) {  // any order
            const double k1 = strikes[i], k2 = strikes[i + 1];
            const double coupon = i == 0 ? 0.05 : 0.01;
            const auto tranche = Instruments::Credit::make_standard_tranche(trade, five_years, k1, k2, coupon);
            const auto value = Pricing::Credit::PreparedTranche<Model>(tranche, yc, pool).value(truth);
            const auto quote = par_quotes ? TrancheQuote::par_spread(five_years, k1, k2, value.par_spread())
                                          : TrancheQuote::upfront(five_years, k1, k2, value.points_upfront(), coupon);
            helpers.push_back(TrancheHelper<Model>::standard(trade, quote, yc, pool));
        }

        const auto result = BaseCorrelationBootstrapper<Model>().bootstrap(Curves::BaseCorrelationCurve<>("IDX"), helpers);
        CHECK(result.curve.size() == rhos.size());
        for (std::size_t i = 0; i < rhos.size(); ++i) {
            CHECK_CLOSE(result.curve.detachments()[i], strikes[i + 1], 1e-15);
            CHECK_CLOSE(result.curve.correlations()[i], rhos[i], 1e-8);
        }
        CHECK(result.max_abs_error() < 1e-10);
    }
}

TEST_CASE(base_correlation_round_trip_lhp) { check_base_correlation_round_trip<LhpModel>(false); }
TEST_CASE(base_correlation_round_trip_recursion) { check_base_correlation_round_trip<GaussianCopulaRecursion>(false); }
TEST_CASE(base_correlation_round_trip_par_spreads) { check_base_correlation_round_trip<GaussianCopulaNormal>(true); }

TEST_CASE(base_correlation_bootstrap_errors) {
    using namespace Curves::Bootstrap;
    const auto yc = flat_yield(0.025);
    const auto curves = pool_curves(10, 0.012, 0.0);
    const Pricing::Credit::ReferencePool pool(CreditIndex::homogeneous("IDX", 10, 0.4), std::span(curves));
    const auto helper = [&](double k1, double k2, double upfront) {
        return TrancheHelper<LhpModel>::standard(trade, TrancheQuote::upfront(five_years, k1, k2, upfront, 0.05), yc, pool);
    };
    const BaseCorrelationBootstrapper<LhpModel> bootstrapper;
    const Curves::BaseCorrelationCurve<> empty("IDX");

    CHECK_THROWS((void)bootstrapper.bootstrap(empty, {helper(0.0, 0.03, 0.2), helper(0.06, 0.12, 0.0)}),
                 std::invalid_argument);
    CHECK_THROWS((void)bootstrapper.bootstrap(empty, {helper(0.0, 0.03, 1.5)}), BootstrapError);
    const auto tranche = Instruments::Credit::make_standard_tranche(trade, five_years, 0.0, 0.03, 0.05);
    CHECK_THROWS(TrancheHelper<LhpModel>(Pricing::Credit::PreparedTranche<LhpModel>(tranche, yc, pool),
                                         TrancheQuote::upfront(five_years, 0.0, 0.06, 0.2, 0.05)),
                 std::invalid_argument);
    CHECK_THROWS(BaseCorrelationBootstrapper<LhpModel>({.min_correlation = 0.5, .max_correlation = 0.4}),
                 std::invalid_argument);
}

TEST_CASE(super_senior_above_maximal_loss) {
    using namespace Curves::Bootstrap;
    const auto yc = flat_yield(0.025);
    const auto curves = pool_curves(10, 0.012, 0.3);
    const Pricing::Credit::ReferencePool pool(CreditIndex::homogeneous("IDX", 10, 0.4), curves);
    Curves::BaseCorrelationCurve<> truth;
    truth.add_pillar(0.12, 0.45);
    truth.add_pillar(1.0, 0.80);

    std::vector<TrancheHelper<LhpModel>> helpers;
    for (const auto& [k1, k2] : std::vector<std::pair<double, double>>{{0.0, 0.12}, {0.12, 1.0}}) {
        const auto t = Instruments::Credit::make_standard_tranche(trade, five_years, k1, k2, 0.01);
        const double upfront = Pricing::Credit::PreparedTranche<LhpModel>(t, yc, pool).value(truth).points_upfront();
        helpers.push_back(TrancheHelper<LhpModel>::standard(trade, TrancheQuote::upfront(five_years, k1, k2, upfront, 0.01), yc, pool));
    }
    const auto result = BaseCorrelationBootstrapper<LhpModel>().bootstrap(Curves::BaseCorrelationCurve<>(), helpers);
    CHECK_CLOSE(result.curve.correlation(0.12), 0.45, 1e-8);
    CHECK(result.curve.correlation(1.0) == result.curve.correlation(0.12));  // flat from the previous pillar
    CHECK(result.max_abs_error() < 1e-10);                      // and the quote is still repriced

    helpers.back() = TrancheHelper<LhpModel>::standard(trade, TrancheQuote::upfront(five_years, 0.12, 1.0, 0.05, 0.01), yc, pool);
    CHECK_THROWS((void)BaseCorrelationBootstrapper<LhpModel>().bootstrap(Curves::BaseCorrelationCurve<>(), helpers),
                 BootstrapError);
}
