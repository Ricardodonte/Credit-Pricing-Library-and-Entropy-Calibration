

#include <chrono>

#include "Core/calendar.h"
#include "Core/Dates.h"
#include "Core/day_counter.h"
#include "Curves/YieldCurve.h"
#include "Curves/CreditCurve.h"
#include "Core/Period.h"
#include "Curves/Bootstrap/CurveBootstrappers.h"
#include "Instruments/Credit/Cds.h"
#include "Instruments/Rates/Deposit.h"
#include "Instruments/Rates/Swap.h"
#include "Pricing/Credit/IsdaCdsEngine.h"
#include "Curves/BaseCorrelationCurve.h"
#include "Curves/Bootstrap/BaseCorrelationBootstrapper.h"
#include "Instruments/Credit/CreditIndex.h"
#include "Instruments/Credit/Tranche.h"
#include "Instruments/Credit/TrancheQuote.h"
#include "Models/Credit/TrancheLossModels.h"
#include "Pricing/Credit/PreparedTranche.h"
#include "Pricing/Credit/ReferencePool.h"
#include <iostream>
#include <iomanip>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

int main() {
    using namespace Core;

    std::cout << "=== Démonstration Date ===\n\n";

    Date d1(2026, 9, 11);
    Date d2(2031, 9, 20);

    std::cout << "Date 1: " << d1.to_string() << "\n";
    std::cout << "Date 2: " << d2.to_string() << "\n";
    std::cout << "Différence: " << (d2 - d1) << " jours\n";
    std::cout << "Jour de la semaine d1: " << d1.day_of_week() << " (0=dim, 6=sam)\n\n";

    Date d3 = d1 + 30;
    std::cout << "d1 + 30 jours = " << d3.to_string() << "\n";
    std::cout << "d1 + 6 mois = " << d1.add_months(6).to_string() << "\n";

    Date d4 = Date::from_string("2025-12-25");
    std::cout << "Parsed: " << d4.to_string() << "\n\n";

    std::cout << "=== Démonstration Calendar ===\n\n";

    TargetCalendar target_cal;
    Date christmas(2026, 12, 25);
    Date adjusted = target_cal.adjust(christmas);

    std::cout << "Noël 2026: " << christmas.to_string() << "\n";
    std::cout << "Ajusté (TARGET): " << adjusted.to_string() << "\n";
    std::cout << "Est jour ouvré? " << (target_cal.is_business_day(christmas) ? "Oui" : "Non") << "\n\n";

    Date start(2026, 9, 11);
    Date advanced = target_cal.advance(start, 10);
    std::cout << "Start: " << start.to_string() << "\n";
    std::cout << "+10 jours ouvrés: " << advanced.to_string() << "\n\n";

    int bdays = target_cal.business_days_between(d1, d2);
    std::cout << "Jours ouvrés entre " << d1.to_string()
              << " et " << d2.to_string() << ": " << bdays << "\n\n";

    std::cout << "=== Démonstration DayCounter ===\n\n";

    Actual360 act360;
    int days = act360.day_count(d1, d2);
    double yf = act360.year_fraction(d1, d2);

    std::cout << "Actual/360:\n";
    std::cout << "  Days: " << days << "\n";
    std::cout << "  Year fraction: " << std::fixed << std::setprecision(6) << yf << "\n\n";

    Actual365 act365;
    yf = act365.year_fraction(d1, d2);
    std::cout << "Actual/365 Fixed:\n";
    std::cout << "  Year fraction: " << yf << "\n\n";


    std::cout << "=== Tests constexpr ===\n\n";

    constexpr Date compile_time_date(2026, 1, 1);
    constexpr int compile_time_days = compile_time_date.raw_days();
    static_assert(compile_time_days > 0, "Date must be positive");

    std::cout << "Compile-time date: " << compile_time_date.to_string() << "\n";
    std::cout << "Raw days: " << compile_time_days << "\n\n";

    std::cout << "=== Démonstration Courbes ===\n\n";

    const Date ref(2026, 1, 1);
    Curves::YieldCurve yc(ref);  // axe des temps en Act/365F
    yc.add_pillar(Date(2027, 1, 1), 0.97);
    yc.add_pillar(Date(2031, 1, 1), 0.86);
    std::cout << "DF(" << Date(2029, 6, 30).to_string() << ") = " << yc.discount(Date(2029, 6, 30)) << "\n";

    Curves::CreditCurve cc("ISSUER", ref);
    cc.add_pillar(Date(2027, 1, 1), 0.02);
    cc.add_pillar(Date(2031, 1, 1), 0.03);
    std::cout << "S(" << Date(2029, 6, 30).to_string() << ") = "
              << cc.survival_probability(Date(2029, 6, 30)) << "\n\n";

    {
        using namespace Curves::Bootstrap;
        using Instruments::Credit::CdsQuote;
        const Date trade(2026, 9, 29);
        const TargetCalendar target;

        std::cout << "=== Bootstrap courbe de taux (" << trade.to_string() << ") ===\n\n";
        std::vector<YieldCurveBootstrapper<>::helper_type> rate_helpers;
        for (const auto& [tenor, rate] : std::vector<std::pair<const char*, double>>{
                 {"1W", 0.0195}, {"1M", 0.0198}, {"3M", 0.0201}, {"6M", 0.0205}})
            rate_helpers.emplace_back(DepositHelper(Instruments::Rates::make_deposit(trade, Period::parse(tenor), rate, target)));
        for (const auto& [tenor, rate] : std::vector<std::pair<const char*, double>>{
                 {"1Y", 0.0212}, {"2Y", 0.0220}, {"3Y", 0.0228}, {"5Y", 0.0241}, {"7Y", 0.0252}, {"10Y", 0.0265}})
            rate_helpers.emplace_back(SwapHelper(Instruments::Rates::make_swap(trade, Period::parse(tenor), rate, target)));

        const auto yield = YieldCurveBootstrapper<>().bootstrap(Curves::YieldCurve(trade), rate_helpers);
        for (const auto& r : yield.report)
            std::cout << "  " << std::left << std::setw(22) << r.instrument << " DF = " << std::setprecision(8)
                      << r.pillar_value << "   erreur = " << std::scientific << std::setprecision(1) << r.error()
                      << std::fixed << "\n";

        std::cout << "\n=== Bootstrap courbe de crédit (par spreads, R = 40%) ===\n\n";
        using YieldCurveT = Curves::YieldCurve<>;
        std::vector<CreditCurveBootstrapper<YieldCurveT>::helper_type> cds_helpers;
        for (const auto& [tenor, spread] : std::vector<std::pair<const char*, double>>{
                 {"6M", 0.0060}, {"1Y", 0.0070}, {"2Y", 0.0085}, {"3Y", 0.0100}, {"5Y", 0.0125}, {"7Y", 0.0140}, {"10Y", 0.0150}})
            cds_helpers.emplace_back(CdsHelper<YieldCurveT>::standard(trade, CdsQuote::par_spread(Period::parse(tenor), spread), yield.curve));

        const auto credit = CreditCurveBootstrapper<YieldCurveT>().bootstrap(Curves::CreditCurve("ISSUER", trade), cds_helpers);
        for (const auto& r : credit.report)
            std::cout << "  " << std::left << std::setw(20) << r.instrument << " " << r.pillar.to_string()
                      << "  lambda = " << std::setprecision(6) << r.pillar_value
                      << "  S = " << credit.curve.survival_probability(r.pillar)
                      << "  erreur (bp) = " << std::scientific << std::setprecision(1) << 1e4 * r.error()
                      << std::fixed << "\n";

        std::cout << "\n=== Pricing d'un CDS 5Y standard, coupon 100bp, notionnel 10M ===\n\n";
        const auto cds = Instruments::Credit::make_standard_cds(trade, Period::parse("5Y"), 0.01, 0.4,
                                                                WeekendsOnly{}, {}, 10'000'000.0);
        const auto v = Pricing::Credit::IsdaCdsEngine(yield.curve, credit.curve).value(cds);
        std::cout << std::setprecision(4)
                  << "  Maturité          : " << cds.maturity.to_string() << "\n"
                  << "  Par spread        : " << 1e4 * v.par_spread() << " bp\n"
                  << "  RPV01 (clean)     : " << v.rpv01_clean() << "\n"
                  << "  Points upfront    : " << 100.0 * v.points_upfront() << " %\n"
                  << "  Cash settlement   : " << std::setprecision(2) << v.cash_settlement_amount() << "\n"
                  << "  NPV (acheteur)    : " << v.npv() << "\n\n";

        std::cout << "=== Tranches : corrélation de base (125 noms, 5Y, copule gaussienne + récursion) ===\n\n";
        std::vector<Curves::CreditCurve<>> names;
        for (int i = 0; i < 125; ++i) {
            Curves::CreditCurve<> name("NAME " + std::to_string(i + 1), trade);
            for (std::size_t k = 1; k < credit.curve.pillar_dates().size(); ++k)
                name.add_pillar(credit.curve.pillar_dates()[k], (0.5 + i / 124.0) * credit.curve.pillar_values()[k]);
            names.push_back(std::move(name));
        }
        const Pricing::Credit::ReferencePool pool(Instruments::Credit::CreditIndex::homogeneous("IDX", 125, 0.4), names);

        using Model = Models::Credit::GaussianCopulaRecursion;
        using Instruments::Credit::TrancheQuote;
        const Period five_years = Period::parse("5Y");
        std::vector<TrancheHelper<Model>> tranche_helpers;
        for (const auto& [k1, k2, upfront] : std::vector<std::tuple<double, double, double>>{
                 {0.00, 0.03, 0.6925}, {0.03, 0.06, 0.3000}, {0.06, 0.12, 0.1275}})
            tranche_helpers.push_back(TrancheHelper<Model>::standard(
                trade, TrancheQuote::upfront(five_years, k1, k2, upfront, 0.01), yield.curve, pool));

        const auto base_correlation = BaseCorrelationBootstrapper<Model>().bootstrap(
            Curves::BaseCorrelationCurve<>("IDX 5Y"), tranche_helpers);
        for (const auto& r : base_correlation.report)
            std::cout << "  " << std::left << std::setw(28) << r.instrument << std::right << " upfront = "
                      << std::setprecision(2) << std::setw(6) << 100.0 * r.quote << " %   rho(K) = "
                      << std::setprecision(4) << r.base_correlation << "   erreur = " << std::scientific
                      << std::setprecision(1) << r.error() << std::fixed << "\n";

        const auto bespoke = Instruments::Credit::make_standard_tranche(trade, five_years, 0.05, 0.10, 0.01,
                                                                        WeekendsOnly{}, {}, 10'000'000.0);
        const auto bv = Pricing::Credit::PreparedTranche<Model>(bespoke, yield.curve, pool).value(base_correlation.curve);
        std::cout << std::setprecision(4)
                  << "\n  Tranche 5-10 %, notionnel 10M : rho(5%) = " << base_correlation.curve.correlation(0.05)
                  << ", rho(10%) = " << base_correlation.curve.correlation(0.10) << "\n"
                  << "  Par spread        : " << 1e4 * bv.par_spread() << " bp\n"
                  << "  Points upfront    : " << 100.0 * bv.points_upfront() << " %\n"
                  << "  NPV (acheteur)    : " << std::setprecision(2) << bv.npv() << "\n\n";
        const auto senior = Instruments::Credit::make_standard_tranche(trade, five_years, 0.12, 1.0, 0.01);
        const auto sv = Pricing::Credit::PreparedTranche<Model>(senior, yield.curve, pool).value(base_correlation.curve);
        std::cout << "  Super senior 12-100 % implicite : " << std::setprecision(4) << 100.0 * sv.points_upfront()
                  << " % upfront\n\n";
    }

    std::cout << "=== Performance Test ===\n\n";

    const int iterations = 10'000'000;
    Date test_date(2026, 1, 1);
    TargetCalendar cal;

    auto start_time = std::chrono::high_resolution_clock::now();

    int count = 0;
    for (int i = 0; i < iterations; ++i) {
        if (cal.is_business_day(test_date + i)) {
            ++count;
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    std::cout << "Processed " << iterations << " dates in "
              << duration.count() << " ms\n";
    std::cout << "Business days found: " << count << "\n";
    std::cout << "Speed: " << (iterations / (duration.count() / 1000.0))
              << " dates/second\n";

    return 0;
}
