// ═════════════════════════════════════════════════════════════════════════════
//  IMPLÉMENTATION - RiskReport Interface
// ═════════════════════════════════════════════════════════════════════════════


#include "interface.h"

#include <chrono>
#include <iostream>
#include <string>
#include <filesystem>
#include <system_error>

#include "Market/Curves/CDS/CreditCurve.h"
#include "Market/Curves/YieldCurve/YieldCurve.h"



namespace RiskReport {



namespace detail {

inline std::string timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%dT%H%M%S", &tm);
    return buf;
}

inline double zero_rate(double df, double t) {
    return (t > 1e-10 && df > 1e-15) ? -std::log(df) / t : 0.0;
}

inline double fwd_rate(const Market::YieldCurve& yc, double t, double eps = 1e-3) {
    const double lo = yc.discount(std::max(t - eps, 1e-6));
    const double hi = yc.discount(t + eps);
    return (lo > 1e-15 && hi > 1e-15) ? -std::log(hi / lo) / (2.0 * eps) : 0.0;
}

inline double hazard_inst(const Market::CreditCurve& cc, double t, double eps = 1e-4) {
    const double qlo = cc.survival_probability(std::max(t - eps, 1e-6));
    const double qhi = cc.survival_probability(t + eps);
    return (qlo > 1e-15 && qhi > 1e-15) ? -std::log(qhi / qlo) / (2.0 * eps) : 0.0;
}

inline std::string tranche_label(double K1, double K2) {
    std::ostringstream s;
    s << std::fixed << std::setprecision(0) << "[" << K1 * 100 << "%-" << K2 * 100 << "%]";
    return s.str();
}

}



 Report Engine::run(const Input& in) const {
    Report rep;
    rep.run_id = detail::timestamp();
    rep.ref_date_str = in.ref_date.to_string();

    if (m_cfg.verbose) {
        std::cout << "═══ RiskReport ── " << rep.run_id << " ───────────────────\n";
        std::cout << "  Working directory: " << std::filesystem::current_path().string() << "\n";
    }

    const Market::YieldCurve yc = bootstrap_yc(in);

    double t_max = 5.0;

    if (in.cds_instrument) t_max = std::max(t_max, in.cds_instrument->get_maturity());
    if (in.tranche_market)
        for (const auto& tr : in.tranche_market->quoted_tranches)
            t_max = std::max(t_max, Core::year_fraction(tr.valuation_date , tr.MaturityDate , tr.day_count) );

    for (const auto& tr : in.extra_tranches)
        t_max = std::max(t_max, Core::year_fraction(tr.valuation_date , tr.MaturityDate , tr.day_count));

    rep.yield_curve = sample_yc(yc, t_max);
    if (m_cfg.verbose)
        std::cout << "  [YC]  " << yc.num_pillars() << " pillars bootstrapped\n";

    if (in.cds_market && in.cds_instrument) {
        if (m_cfg.verbose)
            std::cout << "  [CDS] " << in.cds_market->name << "\n";

        const Market::CreditBoot boot(*in.cds_market, yc, in.cds_market->valuationDate);
        const Market::CreditCurve& cc = boot.curve();

        rep.credit_curve = bootstrap_cc(*in.cds_market, yc, in.cds_instrument->get_maturity());
        rep.cds = price_cds(*in.cds_instrument, *in.cds_market, yc, cc, in.cds_bump);
    }

    if (in.tranche_market) {
        if (m_cfg.verbose)
            std::cout << "  [TRN] " << in.tranche_market->index_name
                      << " — " << pricer_name(in.pricer_type) << "\n";

        const int N = in.tranche_market->n_credits;

        switch (in.pricer_type) {
            case Method::LHP: {
                if (!in.IndexMarketData)
                    throw std::invalid_argument("IndexMarketData required for LHP pricer");
                const Market::CDSMarketData& idx_mkt = *in.IndexMarketData;
                const Market::CreditBoot idx_boot(idx_mkt, yc, idx_mkt.valuationDate);
                auto [bc, tr] = price_tranches_lhp(in, yc, idx_boot.curve());
                rep.base_correlation = std::move(bc);
                rep.tranches = std::move(tr);
                break;
            }

            case Method::Gaussian: {
                if (!in.single_name_curves)
                    throw std::invalid_argument("Single name curves required for Gaussian pricer");
                const auto& mds = *in.single_name_curves;
                std::vector<double> rrs;
                rrs.reserve(mds.size());
                for (const auto& cds : mds) rrs.push_back(cds.recoveryRate);
                auto [bc, tr] = price_tranches<Pricer::GaussianPricer,
                    std::vector<Market::CDSMarketData>, std::vector<double>>(
                    in, yc, mds, rrs, N, "Gaussian");
                rep.base_correlation = std::move(bc);
                rep.tranches = std::move(tr);
                break;
            }

            case Method::Recursion: {
                if (!in.single_name_curves)
                    throw std::invalid_argument("Single name curves required for Recursion pricer");
                const auto& mds = *in.single_name_curves;
                const double rr = in.tranche_market->recovery_rate;
                auto [bc, tr] = price_tranches<Pricer::RecursionPricer,
                    std::vector<Market::CDSMarketData>, double>(in, yc, mds, rr, N, "Recursion");
                rep.base_correlation = std::move(bc);
                rep.tranches = std::move(tr);
                break;
            }

            case Method::Binomial: {
                if (!in.single_name_curves)
                    throw std::invalid_argument("Single name curves required for Binomial pricer");
                const auto& mds = *in.single_name_curves;
                // Use Risk::AdjustedBinomialPricer<Multiple_Recovery> wrapper so that
                // PricerTraits<P> is defined and Greeks (HasTraits) are computed.
                std::vector<double> rrs;
                rrs.reserve(mds.size());
                for (const auto& cds : mds) rrs.push_back(cds.recoveryRate);
                auto [bc, tr] = price_tranches<
                    Risk::AdjustedBinomialPricer<Risk::Multiple_Recovery>,
                    std::vector<Market::CDSMarketData>,
                    Risk::Multiple_Recovery>(
                    in, yc, mds, Risk::Multiple_Recovery{rrs}, N, "AdjustedBinomial");
                rep.base_correlation = std::move(bc);
                rep.tranches = std::move(tr);
                break;
            }
        }
    }

    if (m_cfg.verbose) rep.print();
    return rep;
}



inline Market::YieldCurve Engine::bootstrap_yc(const Input& in) {
    Market::YieldCurveBoot boot(in.ref_date);
    boot.add_deposits(in.yield_deposits);
    boot.add_futures(in.yield_futures);
    boot.add_swaps(in.yield_swaps);
    return boot.curve();
}

inline YieldCurveSection Engine::sample_yc(const Market::YieldCurve& yc, const double t_max) const {
    YieldCurveSection sec;
    for (const auto& p : yc.pillars()) {
        YieldCurveSection::Point pt{p.time, p.value,
            detail::zero_rate(p.value, p.time), detail::fwd_rate(yc, p.time)};
        sec.pillars.push_back(pt);
    }
    for (int i = 1; i <= m_cfg.grid_n; ++i) {
        const double t = t_max * i / static_cast<double>(m_cfg.grid_n);
        const double df = yc.discount(t);
        sec.grid.push_back({t, df, detail::zero_rate(df, t), detail::fwd_rate(yc, t)});
    }
    return sec;
}

inline CreditCurveSection Engine::bootstrap_cc(
    const Market::CDSMarketData& mkt, const Market::YieldCurve& yc, const double t_max) const {
    const Market::CreditBoot boot(mkt, yc, mkt.valuationDate);
    const Market::CreditCurve& cc = boot.curve();
    const auto& errs = boot.repricing_errors();
    const auto intensity = cc.hazard_rates();

    CreditCurveSection sec;
    sec.name = mkt.name;

    for (std::size_t i = 0; i < mkt.quotes.size(); ++i) {
        const double t = mkt.quotes[i].time;
        const double q = cc.survival_probability(t);
        const double mkt_bp = mkt.quotes[i].value * 1e4;
        const double err_bp = (i < errs.size()) ? errs[i].value : 0.0;
        const double lam = intensity[i].value;

        sec.pillars.push_back({t, lam, q, 1.0 - q, mkt_bp, mkt_bp + err_bp, err_bp});
    }

    const double tg = std::max(t_max, mkt.quotes.empty() ? 5.0 : mkt.quotes.back().time);
    for (int i = 1; i <= m_cfg.grid_n; ++i) {
        const double t = tg * i / static_cast<double>(m_cfg.grid_n);
        const double q = cc.survival_probability(t);
        sec.grid.push_back({t, q, detail::hazard_inst(cc, t), 1.0 - q});
    }
    return sec;
}



inline CDSSection Engine::price_cds(
    const Market::CDS& cds, const Market::CDSMarketData& mkt,
    const Market::YieldCurve& yc, const Market::CreditCurve& cc,
    const Risk::CDSBumpConfig& bump)
{
    CDSSection s;
    s.name = cds.Name.empty() ? "None" : cds.Name;
    s.maturity = cds.get_maturity();
    s.nominal = cds.Nominal;
    s.recovery_rate = cds.RecoveryRate;
    s.contractual_spread_bp = cds.ContractualSpread.has_value() ? cds.ContractualSpread.value() * 1e4 : cds.FixedSpread.value()*1e4;

    const Pricer::CDSPricer pricer(cds, yc, cc);
    s.par_spread_bp = pricer.par_spread() * 1e4;
    const double rpv01 = pricer.rpv01(cds.ValuationDate);
    s.rpv01_eur_per_bp = rpv01 * cds.Nominal * 1e-4;
    s.rpv01_pct = rpv01 * 1e-4;
    s.npv_eur = pricer.npv();
    s.upfront_eur = pricer.upfront();
    s.default_leg_pv = pricer.default_leg(cds.ValuationDate);
    s.premium_leg_pv = (cds.ContractualSpread.has_value() ? cds.ContractualSpread.value() : cds.FixedSpread.value()) * rpv01;

    const Risk::CDSRiskEngine engine(mkt, cc, yc, mkt.valuationDate);
    const Risk::CDSRiskResults raw = engine.CDSGreeks(cds, bump);
    s.CreditDV01 = raw.Credit_DV01;
    s.InterestRate_dv01 = raw.InterestRate_DV01;
    s.gamma = raw.SpreadGamma;
    s.theta = raw.Theta;

    return s;
}


inline std::pair<BaseCorrelationSection, TranchesSection>
Engine::price_tranches_lhp(
    const Input& in, const Market::YieldCurve& yc, const Market::CreditCurve& idx_cc) const {
    const auto& tmkt = *in.tranche_market;
    Pricer::lhp_pricer lhp(idx_cc, tmkt.recovery_rate);
    Pricer::BaseCorrelationBoot bc_boot(lhp, yc);
    const Market::BaseCorrBootstrapResult bc = bc_boot.bootstrap(tmkt);
    return build_sections_from_bc<Pricer::lhp_pricer>(lhp, bc, in, yc, "LHP");
}


template <typename P, typename MDInput, typename RRInput>
inline std::pair<BaseCorrelationSection, TranchesSection>
Engine::price_tranches(
    const Input& in, const Market::YieldCurve& yc, MDInput md_input,
    RRInput rr_input, int N, const std::string& pricerName) const {

    std::vector<Market::CreditCurve> sn_curves;
    sn_curves.reserve(static_cast<std::size_t>(N));
    if constexpr (std::is_same_v<MDInput, std::vector<Market::CDSMarketData>>) {
        for (const auto& md : md_input)
            sn_curves.push_back(Market::CreditBoot(md, yc, md.valuationDate).curve());
    }

    // For Risk::AdjustedBinomialPricer<T> (wrapper with traits), use the wrapper's
    // ctor which takes vector<CreditCurve>, int, vector<double>.
    // For plain pricers (Gaussian, Recursion), call directly.
    auto pricer_obj = [&]() -> P {
        if constexpr (std::is_same_v<P, Risk::AdjustedBinomialPricer<Risk::Multiple_Recovery>>) {
            return P(sn_curves, N, rr_input.values);
        } else if constexpr (std::is_same_v<P, Risk::AdjustedBinomialPricer<Risk::Uniform_Recovery>>) {
            return P(sn_curves, N, rr_input.value);
        } else {
            return P(sn_curves, N, rr_input);
        }
    }();
    Pricer::BaseCorrelationBoot<P> bc_boot(pricer_obj, yc);
    const Market::BaseCorrBootstrapResult bc = bc_boot.bootstrap(*in.tranche_market);
    return build_sections_from_bc<P>(pricer_obj, bc, in, yc, pricerName);
}


template <typename P>
inline std::pair<BaseCorrelationSection, TranchesSection>
Engine::build_sections_from_bc(
    P& pricer_obj, const Market::BaseCorrBootstrapResult& bc,
    const Input& in, const Market::YieldCurve& yc, const std::string& PricerName) const {

    const auto& tmkt = *in.tranche_market;

    BaseCorrelationSection bcs;
    bcs.pricer = PricerName;
    for (std::size_t i = 0; i < tmkt.quoted_tranches.size(); ++i) {
        const double K2 = tmkt.quoted_tranches[i].K2;
        const double rho = bc.curve.rho(K2);
        const double par = (i < bc.par_spreads.size()) ? bc.par_spreads[i] * 1e4 : 0.0;
        const double res = (i < bc.residuals.size()) ? bc.residuals[i] : 0.0;
        bcs.points.push_back({K2, rho, par, res});
    }

    TranchesSection ts;
    auto all_tranches = tmkt.quoted_tranches;

    for (std::size_t i = 0; i < all_tranches.size(); ++i) {
        const auto& tr = all_tranches[i];
        const auto grid = Market::build_time_grid(tr, yc);
        const double rho1 = (tr.K1 < 1e-12) ? 0.0 : bc.curve.rho(tr.K1);
        const double rho2 = bc.curve.rho(tr.K2);
        const bool quoted = (i < tmkt.quoted_tranches.size());

        TranchesSection::Row row;
        row.K1 = tr.K1; row.K2 = tr.K2;
        row.quoted = quoted;
        row.nominal = tr.nominal;
        row.maturity = Core::year_fraction(tr.valuation_date , tr.MaturityDate , tr.day_count);
        row.rho1 = rho1; row.rho2 = rho2;
        row.pricer = PricerName;
        row.par_spread_bp = pricer_obj.par_spread(tr, grid, rho1, rho2) * 1e4;
        row.protection_leg_pv = pricer_obj.protection_leg(tr, grid, rho1, rho2);
        row.premium_leg_pv = pricer_obj.premium_leg(tr, grid, rho1, rho2);
        row.npv_eur = pricer_obj.npv(tr, grid, rho1, rho2);
        row.contractual_spread_bp = tr.quoted_upfront
            ? tr.contractual_spread * 1e4 : tr.fair_spread * 1e4;

        if constexpr (Risk::HasTraits<P>) {
            fill_tranche_greeks(row, pricer_obj, tr, grid, rho1, rho2, in, yc);
        }

        ts.rows.push_back(row);
    }

    for (const auto& r : ts.rows) {
        ts.book_npv += r.npv_eur;
        ts.book_cs01 += r.CreditDV01;
        ts.book_dv01 += r.InterestRate_DV01;
        ts.book_rho01 += r.rho01;
        ts.book_gamma += r.gamma;
        ts.book_theta += r.theta;
    }

    return {bcs, ts};
}


template <typename P>
 void Engine::fill_tranche_greeks(
    TranchesSection::Row& row, P& pricer_obj, const Market::Index_tranche& tr,
    const Market::TranchesGrid& grid, double rho1, double rho2,
    const Input& in, const Market::YieldCurve& yc) const {

    if constexpr (!Risk::HasTraits<P>) return;

    using MDInput = Risk::PricerTraits<P>::MDInput;
    using RRInput = Risk::PricerTraits<P>::RRInput;

    const MDInput md = get_md_input<P>(in);
    const RRInput rr = get_rr_input<P>(in);

    Risk::TranchesRiskEngine<P> engine(
        pricer_obj, md, yc, in.ref_date,
        in.tranche_market->n_credits, rr, pricer_name(in.pricer_type));

    const auto g = engine.compute_greeks(tr, grid, rho1, rho2, in.tranche_bump);

    row.CreditDV01 = g.cs01;
    row.InterestRate_DV01 = g.dv01;
    row.rho01 = g.rho01;
    row.gamma = g.gamma;
    row.theta = g.theta;
    row.bucketed_creditDV01 = g.bucketed_cs01;
}


template <typename P>
Risk::PricerTraits<P>::MDInput Engine::get_md_input(const Input& in) const {
    if constexpr (std::is_same_v<typename Risk::PricerTraits<P>::MDInput, Market::CDSMarketData>) {
        return *in.IndexMarketData;
    } else {
        return *in.single_name_curves;
    }
}


    template <typename P>
    Risk::PricerTraits<P>::RRInput Engine::get_rr_input(const Input& in) const {

    if constexpr (std::is_same_v<P, Pricer::lhp_pricer>) {
        return Risk::Uniform_Recovery{in.tranche_market->recovery_rate};
    }

    else if constexpr (std::is_same_v<P, Pricer::RecursionPricer>) {
        return Risk::Uniform_Recovery{in.tranche_market->recovery_rate};
    }

    else {
        std::vector<double> rr;
        const auto& snc = *in.single_name_curves;
        rr.reserve(snc.size());
        for (const auto& cds : snc) {
            rr.push_back(cds.recoveryRate);
        }
        return Risk::Multiple_Recovery{rr};
    }
}


inline std::ofstream Report::open(const std::string& p) {
    std::ofstream f(p);
    if (!f.is_open()) throw std::runtime_error("Cannot open " + p);
    f << std::fixed << std::setprecision(8);
    std::cout << "  📝 Opened file: " << p << "\n";
    return f;
}

// Helper function to create directory if it doesn't exist
namespace detail {

inline void create_directory(const std::string& path) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec) {
        throw std::runtime_error("Cannot create directory '" + path + "': " + ec.message());
    }
    std::cout << "  📁 Directory ensured: " << std::filesystem::absolute(path).string() << "\n";
}

} // namespace detail

 void Report::print() const {
    std::cout << "\n  ──────────────────────────────────────────────────────\n"
              << "  RiskReport ── " << run_id << " (ref: " << ref_date_str << ")\n"
              << "  ──────────────────────────────────────────────────────\n\n";

    if (credit_curve) {
        std::cout << "  Credit Curve: " << credit_curve->name << "\n"
                  << "  " << credit_curve->pillars.size() << " pillars, "
                  << credit_curve->grid.size() << " grid points\n\n";
    }

    if (cds) {
        std::cout << "  CDS (" << cds->name << "):\n"
                  << "    Par Spread:  " << std::setprecision(2) << cds->par_spread_bp << " bp\n"
                  << "    NPV:         " << std::setprecision(0) << cds->npv_eur << " EUR\n"
                  << "    CS01:        " << std::setprecision(2) << cds->CreditDV01 << " EUR/bp\n\n";
    }

    if (tranches) {
        std::cout << "  Tranches (" << tranches->rows.size() << " tranches):\n";
        for (const auto& r : tranches->rows) {
            std::cout << "    [" << std::setprecision(0) << r.K1 * 100 << "%-"
                      << r.K2 * 100 << "%]  Par: " << std::setprecision(2) << r.par_spread_bp
                      << "bp  NPV: " << std::setprecision(0) << r.npv_eur << " EUR\n";
        }
        std::cout << "  Book Summary:\n"
                  << "    Total NPV:   " << std::setprecision(0) << tranches->book_npv << " EUR\n"
                  << "    Total CS01:  " << std::setprecision(2) << tranches->book_cs01 << " EUR/bp\n"
                  << "    Total DV01:  " << std::setprecision(2) << tranches->book_dv01 << " EUR/bp\n\n";
    }
}



 void Report::to_json(const std::string& filename) const {
    // Créer le dossier s'il n'existe pas
    detail::create_directory("reports");

    std::string filepath = (std::filesystem::path("reports") / filename).string();
    auto f = open(filepath);

    f << "{\n";
    f << "  \"metadata\": {\n";
    f << R"(    "run_id": ")" << run_id << "\",\n";
    f << R"(    "ref_date": ")" << ref_date_str << "\"\n";
    f << "  },\n";

    // ─────────────────────────────────────────────────────────────
    // Yield Curve
    // ─────────────────────────────────────────────────────────────
    f << "  \"yield_curve\": {\n";
    f << "    \"pillars\": [\n";
    for (std::size_t i = 0; i < yield_curve.pillars.size(); ++i) {
        const auto& p = yield_curve.pillars[i];
        f << "      {\n";
        f << "        \"time\": " << p.t << ",\n";
        f << "        \"discount_factor\": " << p.df << ",\n";
        f << "        \"zero_rate\": " << p.zero_rate << ",\n";
        f << "        \"forward_rate\": " << p.fwd_rate << "\n";
        f << "      }" << (i < yield_curve.pillars.size() - 1 ? ",\n" : "\n");
    }
    f << "    ],\n";
    f << "    \"grid_points\": " << yield_curve.grid.size() << "\n";
    f << "  }";


    if (credit_curve) {
        f << ",\n";
        f << "  \"credit_curve\": {\n";
        f << R"(    "name": ")" << credit_curve->name << "\",\n";
        f << "    \"pillars\": [\n";
        for (std::size_t i = 0; i < credit_curve->pillars.size(); ++i) {
            const auto& p = credit_curve->pillars[i];
            f << "      {\n";
            f << "        \"time\": " << p.t << ",\n";
            f << "        \"hazard_rate\": " << p.hazard_rate << ",\n";
            f << "        \"survival_probability\": " << p.survival_prob << ",\n";
            f << "        \"default_probability\": " << p.default_prob << ",\n";
            f << "        \"market_spread_bp\": " << p.market_spread_bp << ",\n";
            f << "        \"repriced_spread_bp\": " << p.repriced_spread_bp << ",\n";
            f << "        \"repricing_error_bp\": " << p.repricing_error_bp << "\n";
            f << "      }" << (i < credit_curve->pillars.size() - 1 ? ",\n" : "\n");
        }
        f << "    ],\n";
        f << "    \"grid_points\": " << credit_curve->grid.size() << "\n";
        f << "  }";
    }

    if (cds) {
        f << ",\n";
        f << "  \"cds\": {\n";
        f << R"(    "name": ")" << cds->name << "\",\n";
        f << "    \"instrument\": {\n";
        f << "      \"maturity_years\": " << cds->maturity << ",\n";
        f << "      \"nominal_eur\": " << cds->nominal << ",\n";
        f << "      \"recovery_rate_pct\": " << (cds->recovery_rate * 100.0) << ",\n";
        f << "      \"contractual_spread_bp\": " << cds->contractual_spread_bp << "\n";
        f << "    },\n";
        f << "    \"pricing\": {\n";
        f << "      \"par_spread_bp\": " << cds->par_spread_bp << ",\n";
        f << "      \"rpv01_eur_per_bp\": " << cds->rpv01_eur_per_bp << ",\n";
        f << "      \"rpv01_pct\": " << cds->rpv01_pct << ",\n";
        f << "      \"default_leg_pv_eur\": " << cds->default_leg_pv << ",\n";
        f << "      \"premium_leg_pv_eur\": " << cds->premium_leg_pv << ",\n";
        f << "      \"npv_eur\": " << cds->npv_eur << ",\n";
        f << "      \"upfront_eur\": " << cds->upfront_eur << "\n";
        f << "    },\n";
        f << "    \"greeks\": {\n";
        f << "      \"credit_dv01_eur_per_bp\": " << cds->CreditDV01 << ",\n";
        f << "      \"interest_rate_dv01_eur_per_bp\": " << cds->InterestRate_dv01 << ",\n";
        f << "      \"spread_gamma\": " << cds->gamma << ",\n";
        f << "      \"theta_eur_per_day\": " << cds->theta << "\n";
        f << "    },\n";
        f << "    \"bucketed_greeks\": [\n";
        for (std::size_t i = 0; i < cds->bucketed_cs01.size(); ++i) {
            const auto& b = cds->bucketed_cs01[i];
            f << "      {\"time\": " << b.t << ", \"creditDV01\": " << b.creditDV01 << "}"
              << (i < cds->bucketed_cs01.size() - 1 ? ",\n" : "\n");
        }
        f << "    ]\n";
        f << "  }";
    }

    if (tranches) {
        f << ",\n";
        f << "  \"tranches\": {\n";
        f << "    \"tranches\": [\n";
        for (std::size_t i = 0; i < tranches->rows.size(); ++i) {
            const auto& r = tranches->rows[i];
            f << "      {\n";
            f << "        \"attachment_point_pct\": " << (r.K1 * 100.0) << ",\n";
            f << "        \"detachment_point_pct\": " << (r.K2 * 100.0) << ",\n";
            f << "        \"nominal_eur\": " << r.nominal << ",\n";
            f << "        \"maturity_years\": " << r.maturity << ",\n";
            f << "        \"quoted\": " << (r.quoted ? "true" : "false") << ",\n";
            f <<  R"(        "pricer": ")" << r.pricer << "\",\n";
            f << "        \"base_correlations\": {\n";
            f << "          \"rho1\": " << r.rho1 << ",\n";
            f << "          \"rho2\": " << r.rho2 << "\n";
            f << "        },\n";
            f << "        \"pricing\": {\n";
            f << "          \"par_spread_bp\": " << r.par_spread_bp << ",\n";
            f << "          \"contractual_spread_bp\": " << r.contractual_spread_bp << ",\n";
            f << "          \"protection_leg_pv_eur\": " << r.protection_leg_pv << ",\n";
            f << "          \"premium_leg_pv_eur\": " << r.premium_leg_pv << ",\n";
            f << "          \"npv_eur\": " << r.npv_eur << "\n";
            f << "        },\n";
            f << "        \"greeks\": {\n";
            f << "          \"credit_dv01_eur_per_bp\": " << r.CreditDV01 << ",\n";
            f << "          \"interest_rate_dv01_eur_per_bp\": " << r.InterestRate_DV01 << ",\n";
            f << "          \"rho_01\": " << r.rho01 << ",\n";
            f << "          \"gamma\": " << r.gamma << ",\n";
            f << "          \"theta_eur_per_day\": " << r.theta << "\n";
            f << "        },\n";
            f << "        \"bucketed_credit_dv01\": [\n";
            for (std::size_t j = 0; j < r.bucketed_creditDV01.size(); ++j) {
                f << "          " << r.bucketed_creditDV01[j]
                  << (j < r.bucketed_creditDV01.size() - 1 ? ",\n" : "\n");
            }
            f << "        ]\n";
            f << "      }" << (i < tranches->rows.size() - 1 ? ",\n" : "\n");
        }
        f << "    ],\n";
        f << "    \"book_summary\": {\n";
        f << "      \"total_npv_eur\": " << tranches->book_npv << ",\n";
        f << "      \"total_credit_dv01_eur_per_bp\": " << tranches->book_cs01 << ",\n";
        f << "      \"total_interest_rate_dv01_eur_per_bp\": " << tranches->book_dv01 << ",\n";
        f << "      \"total_rho_01\": " << tranches->book_rho01 << ",\n";
        f << "      \"total_gamma\": " << tranches->book_gamma << ",\n";
        f << "      \"total_theta_eur_per_day\": " << tranches->book_theta << "\n";
        f << "    }\n";
        f << "  }";
    }

    if (base_correlation) {
        f << ",\n";
        f << "  \"base_correlation\": {\n";
        f << R"(    "pricer": ")" << base_correlation->pricer << "\",\n";
        f << "    \"points\": [\n";
        for (std::size_t i = 0; i < base_correlation->points.size(); ++i) {
            const auto& pt = base_correlation->points[i];
            f << "      {\n";
            f << "        \"detachment_point_pct\": " << (pt.K2 * 100.0) << ",\n";
            f << "        \"base_correlation\": " << pt.rho << ",\n";
            f << "        \"par_spread_bp\": " << pt.par_spread_bp << ",\n";
            f << "        \"residual\": " << pt.residual << "\n";
            f << "      }" << (i < base_correlation->points.size() - 1 ? ",\n" : "\n");
        }
        f << "    ]\n";
        f << "  }";
    }

    f << "\n}\n";
}

} // namespace RiskReport