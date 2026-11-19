//
// Created by ricar on 18/05/2026.
//


#pragma once

#include <string>
#include <vector>
#include <optional>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <chrono>
#include <memory>
#include <ctime>

#include "Core/Dates.h"
#include "Core/types.h"
#include "Market/Instruments/CDS/instruments.h"
#include "Market/Instruments/Tranches/tranches_instruments.h"
#include "Market/Curves/YieldCurve/YieldCurve.h"
#include "Market/Curves/CDS/CreditCurve.h"
#include "Market/Curves/Tranches/BaseCorrelationCurve.h"
#include "Market/Calibration/base_correlation_curve.h"
#include "Pricers/CDS/cds_pricers.h"
#include "Pricers/Tranches/Base/base_tranche_pricer.h"
#include "Pricers/Tranches/Methods/lhp_pricer.h"
#include "Pricers/Tranches/Methods/gaussian_pricer.h"
#include "Pricers/Tranches/Methods/full_recursion_pricing.h"
#include "Pricers/Tranches/Methods/binomial_pricer.h"
#include "Pricers/tranche_book_pricer.h"
#include "Risk/CDS/risk_engine.h"
#include "Risk/CDS/bumper.h"
#include "Risk/Tranches/risk_engine.h"
#include "Risk/Tranches/risk_result.h"
#include "Risk/Tranches/bumper.h"
#include "Risk/Tranches/pricers_traits.h"

namespace RiskReport {

enum class Method { LHP, Gaussian, Recursion, Binomial };

inline std::string pricer_name(const Method p) {
    switch (p) {
        case Method::LHP:       return "LHP";
        case Method::Gaussian:  return "Gaussian";
        case Method::Recursion: return "Recursion";
        case Method::Binomial:  return "AdjustedBinomial";
    }
    return "Unknown";
}


struct Input {
    Core::Date ref_date;
    std::vector<Market::Deposit> yield_deposits;
    std::vector<Market::Future>  yield_futures;
    std::vector<Market::Swap>    yield_swaps;

    std::optional<Market::CDSMarketData>              cds_market;
    std::optional<Market::CDS>                        cds_instrument;
    Risk::CDSBumpConfig                               cds_bump = {};

    std::optional<Market::Tranches_MarketData>        tranche_market;
    std::optional<Market::CDSMarketData>              IndexMarketData;
    std::optional<std::vector<Market::CDSMarketData>> single_name_curves;

    Method                                            pricer_type = Method::LHP;
    Risk::TranchesBumpConfig                          tranche_bump = {};
    std::vector<Market::Index_tranche>                extra_tranches;
};


struct YieldCurveSection {
    struct Point {
        double t, df, zero_rate, fwd_rate;
    };
    std::vector<Point> pillars;
    std::vector<Point> grid;
};

struct CreditCurveSection {
    struct Pillar {
        double t, hazard_rate, survival_prob, default_prob;
        double market_spread_bp, repriced_spread_bp, repricing_error_bp;
    };
    struct GridPoint {
        double t, survival_prob, hazard_rate, cumdef;
    };
    std::string name;
    std::vector<Pillar> pillars;
    std::vector<GridPoint> grid;
};

struct CDSSection {
    std::string name;
    double maturity{}, nominal{}, recovery_rate{}, contractual_spread_bp{};
    double par_spread_bp{}, rpv01_eur_per_bp{}, rpv01_pct{};
    double default_leg_pv{}, premium_leg_pv{}, npv_eur{}, upfront_eur{};
    double CreditDV01{}, InterestRate_dv01{}, gamma{}, theta{};
    struct BucketedCS01 { double t; double creditDV01; };
    std::vector<BucketedCS01> bucketed_cs01;
};

struct BaseCorrelationSection {
    struct Point { double K2, rho, par_spread_bp, residual; };
    std::string pricer;
    std::vector<Point> points;
};

struct TranchesSection {
    struct Row {
        double K1{}, K2{}, nominal{}, maturity{}, rho1{}, rho2{};
        double par_spread_bp{}, contractual_spread_bp{};
        double protection_leg_pv{}, premium_leg_pv{}, npv_eur{};
        double CreditDV01{}, InterestRate_DV01{}, rho01{}, gamma{}, theta{};
        std::vector<double> bucketed_creditDV01;
        std::string pricer;
        bool quoted{};
    };
    std::vector<Row> rows;
    double book_npv = 0.0, book_cs01 = 0.0, book_dv01 = 0.0;
    double book_rho01 = 0.0, book_gamma = 0.0, book_theta = 0.0;
};


struct Report {
    std::string run_id, ref_date_str;
    YieldCurveSection yield_curve;
    std::optional<CreditCurveSection> credit_curve;
    std::optional<CDSSection> cds;
    std::optional<BaseCorrelationSection> base_correlation;
    std::optional<TranchesSection> tranches;

    void print() const;
    void to_json(const std::string& filename) const;

private:
    static std::ofstream open(const std::string& p);
};


struct ReportConfig {
    int grid_n = 200;
    bool verbose = true;
};

class Engine {
public:
    explicit Engine(const ReportConfig cfg = ReportConfig{}) : m_cfg(cfg) {}

    [[nodiscard]] Report run(const Input& in) const;


private:
    ReportConfig m_cfg;

    static Market::YieldCurve bootstrap_yc(const Input& in);
    [[nodiscard]] YieldCurveSection sample_yc(const Market::YieldCurve& yc, double t_max) const;

    // Credit curves
    [[nodiscard]] CreditCurveSection bootstrap_cc(
        const Market::CDSMarketData& mkt,
        const Market::YieldCurve& yc,
        double t_max) const;

    // CDS pricing
    [[nodiscard]] static CDSSection price_cds(
        const Market::CDS& cds,
        const Market::CDSMarketData& mkt,
        const Market::YieldCurve& yc,
        const Market::CreditCurve& cc,
        const Risk::CDSBumpConfig& bump);

    // Tranches
    [[nodiscard]] std::pair<BaseCorrelationSection, TranchesSection>
    price_tranches_lhp(
        const Input& in,
        const Market::YieldCurve& yc,
        const Market::CreditCurve& idx_cc) const;

    template <typename P, typename MDInput, typename RRInput>
    [[nodiscard]] std::pair<BaseCorrelationSection, TranchesSection>
    price_tranches(
        const Input& in,
        const Market::YieldCurve& yc,
        MDInput md_input,
        RRInput rr_input,
        int N,
        const std::string& pricerName) const;

    template<typename P>
    std::pair<BaseCorrelationSection, TranchesSection> build_sections_from_bc(
        P& pricer_obj,
        const Market::BaseCorrBootstrapResult& bc,
        const Input& in,
        const Market::YieldCurve& yc,
        const std::string& PricerName) const;

    template<typename P>
    void fill_tranche_greeks(
        TranchesSection::Row& row,
        P& pricer_obj,
        const Market::Index_tranche& tr,
        const Market::TranchesGrid& grid,
        double rho1,
        double rho2,
        const Input& in,
        const Market::YieldCurve& yc) const;

    template<typename P>
    Risk::PricerTraits<P>::MDInput get_md_input(const Input& in) const;

    template<typename P>
    Risk::PricerTraits<P>::RRInput get_rr_input(const Input& in) const;
};

} // namespace RiskReport

