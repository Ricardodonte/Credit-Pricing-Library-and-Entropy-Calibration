
#include <chrono>
#include <span>
#include <cstdio>
#include <utility>
#include <vector>

#include "Core/Period.h"
#include "Core/calendar.h"
#include "Curves/Bootstrap/CurveBootstrappers.h"
#include "Instruments/Credit/Cds.h"
#include "Instruments/Credit/CdsQuote.h"
#include "Instruments/Rates/Deposit.h"
#include "Instruments/Rates/Fra.h"
#include "Instruments/Rates/Swap.h"
#include "Pricing/Credit/IsdaCdsEngine.h"
#include "Pricing/Credit/PreparedCds.h"
#include "Curves/Bootstrap/BaseCorrelationBootstrapper.h"
#include "Models/Credit/TrancheLossModels.h"
#include "Pricing/Credit/PreparedTranche.h"

using namespace Curves::Bootstrap;
using Core::Date;
using Core::Period;
using Instruments::Credit::CdsQuote;
using YieldCurveT = Curves::YieldCurve<>;

namespace {

    volatile double sink = 0.0;  // prevents the compiler from optimising the measured work away

    template <class Work>
    void measure(const char* label, int repetitions, Work&& work) {
        work();  // warm-up
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < repetitions; ++i) sink = sink + work();
        const auto end = std::chrono::steady_clock::now();
        const double total_us = std::chrono::duration<double, std::micro>(end - start).count();
        std::printf("  %-52s %10.2f us / appel   (%d appels, %.1f ms au total)\n", label, total_us / repetitions,
                    repetitions, total_us / 1000.0);
    }

    const Date trade(2026, 9, 29);

    std::vector<YieldCurveBootstrapper<>::helper_type> rate_helpers() {
        using namespace Instruments::Rates;
        const Core::TargetCalendar cal;
        std::vector<YieldCurveBootstrapper<>::helper_type> helpers;
        for (const auto& [tenor, rate] : std::vector<std::pair<const char*, double>>{
                 {"1W", 0.0195}, {"1M", 0.0198}, {"3M", 0.0201}, {"6M", 0.0205}})
            helpers.emplace_back(DepositHelper(make_deposit(trade, Period::parse(tenor), rate, cal)));
        helpers.emplace_back(FraHelper(make_fra(trade, Period::parse("6M"), Period::parse("9M"), 0.0209, cal)));
        for (const auto& [tenor, rate] : std::vector<std::pair<const char*, double>>{
                 {"1Y", 0.0212}, {"2Y", 0.0220}, {"3Y", 0.0228}, {"5Y", 0.0241}, {"7Y", 0.0252},
                 {"10Y", 0.0265}, {"15Y", 0.0276}, {"20Y", 0.0280}, {"30Y", 0.0275}})
            helpers.emplace_back(SwapHelper(make_swap(trade, Period::parse(tenor), rate, cal)));
        return helpers;
    }

    std::vector<CreditCurveBootstrapper<YieldCurveT>::helper_type> cds_helpers(const YieldCurveT& yc) {
        std::vector<CreditCurveBootstrapper<YieldCurveT>::helper_type> helpers;
        for (const auto& [tenor, spread] : std::vector<std::pair<const char*, double>>{
                 {"6M", 0.0060}, {"1Y", 0.0070}, {"2Y", 0.0085}, {"3Y", 0.0100},
                 {"5Y", 0.0125}, {"7Y", 0.0140}, {"10Y", 0.0150}})
            helpers.emplace_back(CdsHelper<YieldCurveT>::standard(trade, CdsQuote::par_spread(Period::parse(tenor), spread), yc));
        return helpers;
    }

    std::vector<CreditCurveBootstrapper<YieldCurveT>::helper_type> quarterly_cds_helpers(const YieldCurveT& yc, int years) {
        std::vector<CreditCurveBootstrapper<YieldCurveT>::helper_type> helpers;
        const Date first = Core::cds_standard_maturity(trade, Period(3, Core::TimeUnit::Months));
        for (int q = 0; q < 4 * years; ++q) {
            const double spread = 0.0060 + 0.0001 * q;
            const auto cds = Instruments::Credit::make_cds(trade, first.add_months(3 * q), spread, 0.4);
            helpers.emplace_back(CdsHelper<YieldCurveT>(cds, CdsQuote::par_spread(Period(3 * (q + 1), Core::TimeUnit::Months), spread), yc));
        }
        return helpers;
    }
}

int main() {
    std::printf("=== Benchmarks (moyenne par appel) ===\n\n");

    const auto rates = rate_helpers();
    const YieldCurveBootstrapper<> yield_bootstrapper;
    const auto yield = yield_bootstrapper.bootstrap(YieldCurveT(trade), rates);

    const CreditCurveBootstrapper<YieldCurveT> credit_bootstrapper;
    const auto cds = cds_helpers(yield.curve);
    const auto credit = credit_bootstrapper.bootstrap(Curves::CreditCurve<>("ISSUER", trade), cds);

    std::printf("Courbes : %zu piliers de taux (erreur max %.1e), %zu piliers de credit (erreur max %.1e bp)\n\n",
                yield.report.size(), yield.max_abs_error(), credit.report.size(), 1e4 * credit.max_abs_error());

    std::printf("Bootstrap\n");
    measure("Courbe de taux : 4 depots + 1 FRA + 9 swaps (1Y-30Y)", 2000, [&] {
        return yield_bootstrapper.bootstrap(YieldCurveT(trade), rates).curve.pillar_values().back();
    });
    measure("Courbe de credit : 7 CDS (6M-10Y)", 2000, [&] {
        return credit_bootstrapper.bootstrap(Curves::CreditCurve<>("ISSUER", trade), cds).curve.pillar_values().back();
    });
    measure("Chaine complete : taux puis credit", 1000, [&] {
        const auto y = yield_bootstrapper.bootstrap(YieldCurveT(trade), rates);
        return credit_bootstrapper.bootstrap(Curves::CreditCurve<>("ISSUER", trade), cds_helpers(y.curve))
            .curve.pillar_values().back();
    });

    std::printf("\nMontee en charge du bootstrap credit (1 pilier par trimestre)\n");
    for (const int years : {5, 10, 20}) {
        const auto helpers = quarterly_cds_helpers(yield.curve, years);
        char label[64];
        std::snprintf(label, sizeof label, "%d CDS (%d ans)", 4 * years, years);
        measure(label, 200, [&] {
            return credit_bootstrapper.bootstrap(Curves::CreditCurve<>("ISSUER", trade), helpers).curve.pillar_values().back();
        });
    }

    std::printf("\nPricing\n");
    const Pricing::Credit::IsdaCdsEngine engine(yield.curve, credit.curve);
    const auto cds5y = Instruments::Credit::make_standard_cds(trade, Period::parse("5Y"), 0.01, 0.4);
    const auto cds10y = Instruments::Credit::make_standard_cds(trade, Period::parse("10Y"), 0.01, 0.4);
    measure("CDS 5Y : valorisation complete (par spread, upfront)", 100000, [&] {
        return engine.value(cds5y).points_upfront();
    });
    measure("CDS 10Y : valorisation complete", 100000, [&] {
        return engine.value(cds10y).points_upfront();
    });
    const Pricing::Credit::PreparedCds prepared5y(cds5y, yield.curve, credit.curve.pillar_dates());
    const Pricing::Credit::PreparedCds prepared10y(cds10y, yield.curve, credit.curve.pillar_dates());
    measure("CDS 5Y prepare : valorisation", 1000000, [&] {
        return prepared5y.value(credit.curve).points_upfront();
    });
    measure("CDS 10Y prepare : valorisation", 1000000, [&] {
        return prepared10y.value(credit.curve).points_upfront();
    });
    measure("Preparation d'un CDS 5Y (grille)", 100000, [&] {
        return Pricing::Credit::PreparedCds(cds5y, yield.curve, credit.curve.pillar_dates()).credit_pillars().size();
    });
    measure("Construction d'un CDS standard 5Y (echeancier)", 100000, [&] {
        return Instruments::Credit::make_standard_cds(trade, Period::parse("5Y"), 0.01, 0.4).periods.back().accrual;
    });
    measure("Survie S(t) sur la courbe de credit", 1000000, [&] {
        return credit.curve.survival_probability(4.2);
    });
    measure("Discount DF(t) sur la courbe de taux", 1000000, [&] {
        return yield.curve.discount(4.2);
    });

    std::printf("\nTranches (125 noms, 5Y, grille trimestrielle)\n");
    std::vector<Curves::CreditCurve<>> names;
    for (int i = 0; i < 125; ++i) {
        Curves::CreditCurve<> c("NAME", trade);
        const double scale = 0.5 + i / 124.0;  // hazards de 0.5x a 1.5x la courbe ISSUER
        for (std::size_t k = 1; k < credit.curve.pillar_dates().size(); ++k)
            c.add_pillar(credit.curve.pillar_dates()[k], scale * credit.curve.pillar_values()[k]);
        names.push_back(std::move(c));
    }
    const Pricing::Credit::ReferencePool pool(Instruments::Credit::CreditIndex::homogeneous("IDX", 125, 0.4),
                                              std::span(names));
    const auto mezz = Instruments::Credit::make_standard_tranche(trade, Period::parse("5Y"), 0.03, 0.06, 0.01);
    const auto bench_model = [&]<class Model>(const char* name) {
        char label[96];
        std::snprintf(label, sizeof label, "%s : preparation 3-6%%", name);
        measure(label, 20, [&] {
            return Pricing::Credit::PreparedTranche<Model>(mezz, yield.curve, pool).grid().size() * 1.0;
        });
        const Pricing::Credit::PreparedTranche<Model> prepared(mezz, yield.curve, pool);
        std::snprintf(label, sizeof label, "%s : valorisation 3-6%%", name);
        measure(label, 50, [&] { return prepared.value(0.3, 0.4).points_upfront(); });

        const std::vector<std::pair<double, double>> strikes = {{0.0, 0.03}, {0.03, 0.06}, {0.06, 0.12}, {0.12, 1.0}};
        const std::vector<double> rhos = {0.3, 0.4, 0.5, 0.7};
        Curves::BaseCorrelationCurve<> truth;
        for (std::size_t i = 0; i < strikes.size(); ++i) truth.add_pillar(strikes[i].second, rhos[i]);
        std::vector<TrancheHelper<Model>> helpers;
        for (const auto& [k1, k2] : strikes) {
            const double coupon = k1 == 0.0 ? 0.05 : 0.01;
            const auto t = Instruments::Credit::make_standard_tranche(trade, Period::parse("5Y"), k1, k2, coupon);
            const double upfront = Pricing::Credit::PreparedTranche<Model>(t, yield.curve, pool).value(truth).points_upfront();
            helpers.push_back(TrancheHelper<Model>::standard(
                trade, Instruments::Credit::TrancheQuote::upfront(Period::parse("5Y"), k1, k2, upfront, coupon), yield.curve, pool));
        }
        std::snprintf(label, sizeof label, "%s : bootstrap corr. de base (4)", name);
        measure(label, 3, [&] {
            return BaseCorrelationBootstrapper<Model>().bootstrap(Curves::BaseCorrelationCurve<>(), helpers).curve.correlations().back();
        });
    };
    bench_model.operator()<Models::Credit::LhpModel>("LHP");
    bench_model.operator()<Models::Credit::GaussianCopulaNormal>("Copule + normale");
    bench_model.operator()<Models::Credit::GaussianCopulaAdjBinomial>("Copule + binomiale ajustee");
    bench_model.operator()<Models::Credit::GaussianCopulaRecursion>("Copule + recursion exacte");
    return 0;
}
