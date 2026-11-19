#pragma once

#include <vector>
#include <string>
#include <stdexcept>
#include <cmath>
#include <algorithm>

#include "Core/Dates.h"
#include "../../Market/Curves/YieldCurve/YieldCurve.h"
#include "../../Market/Curves/CDS/CreditCurve.h"
#include "../../Market/Instruments/CDS/instruments.h"
#include "../../Market/Instruments/Tranches/tranches_instruments.h"
#include "pricers_traits.h"
#include "risk_result.h"
#include "bumper.h"

namespace Risk {

template <typename MDInput>
struct CurveBuilder {

    static std::vector<Market::CreditCurve>
    build_parallel(const MDInput& mds, const Market::YieldCurve& yc,
                   const Core::Date& ref, int, const double h_decimal)
    {
        std::vector<Market::CreditCurve> curves;
        curves.reserve(mds.size());
        for (const auto& md : mds) {
            Market::CDSMarketData md_b = md;
            for (auto& q : md_b.quotes) q.value = std::max(q.value + h_decimal, 1e-4);
            curves.push_back(Market::CreditBoot(md_b, yc, ref).curve());
        }
        return curves;
    }

    static std::vector<Market::CreditCurve>
    build_bucketed(const MDInput& mds, const Market::YieldCurve& yc,
                   const Core::Date& ref, int, const std::size_t pillar_idx, const double h_decimal)
    {
        std::vector<Market::CreditCurve> curves;
        curves.reserve(mds.size());
        for (const auto& md : mds) {
            Market::CDSMarketData md_b = md;
            md_b.quotes[pillar_idx].value =
                std::max(md_b.quotes[pillar_idx].value + h_decimal, 1e-4);
            curves.push_back(Market::CreditBoot(md_b, yc, ref).curve());
        }
        return curves;
    }

    static Market::TranchesGrid
    bump_grid_rates(const Market::TranchesGrid& grid, const double h_decimal, const double maturity)
    {
        Market::TranchesGrid g = grid;
        const double f = std::exp(-h_decimal * maturity);
        for (auto& df : g.premium_dFactors) df *= f;
        for (auto& df : g.default_dFactors)  df *= f;
        return g;
    }

    static const std::vector<Core::Point>& quotes(const MDInput& mds) {
        return mds.front().quotes;
    }
};

template <>
struct CurveBuilder<Market::CDSMarketData> {

    static Market::CreditCurve
    build_parallel(const Market::CDSMarketData& md, const Market::YieldCurve& yc,
                   const Core::Date& ref, int, const double h_decimal)
    {
        Market::CDSMarketData md_b = md;
        for (auto& q : md_b.quotes) q.value = std::max(q.value + h_decimal, 1e-4);
        return Market::CreditBoot(md_b, yc, ref).curve();
    }

    static Market::CreditCurve
    build_bucketed(const Market::CDSMarketData& md, const Market::YieldCurve& yc,
                   const Core::Date& ref, int, const std::size_t pillar_idx, const double h_decimal)
    {
        Market::CDSMarketData md_b = md;
        md_b.quotes[pillar_idx].value =
            std::max(md_b.quotes[pillar_idx].value + h_decimal, 1e-4);
        return Market::CreditBoot(md_b, yc, ref).curve();
    }

    static Market::TranchesGrid
    bump_grid_rates(const Market::TranchesGrid& grid, const double h_decimal, const double maturity)
    {
        Market::TranchesGrid g = grid;
        const double f = std::exp(-h_decimal * maturity);
        for (auto& df : g.premium_dFactors) df *= f;
        for (auto& df : g.default_dFactors)  df *= f;
        return g;
    }

    static const std::vector<Core::Point>& quotes(const Market::CDSMarketData& md) {
        return md.quotes;
    }
};

template <typename Pricer>
    requires HasTraits<Pricer>
class TranchesRiskEngine {

    using Traits     = PricerTraits<Pricer>;
    using CurveInput = Traits::CurveInput;
    using MDInput    = Traits::MDInput;
    using RRInput    = Traits::RRInput;
    using Builder    = CurveBuilder<MDInput>;

public:

    TranchesRiskEngine(const Pricer& pricer, MDInput md, const Market::YieldCurve& yc,
               const Core::Date& ref, const int n_credits, RRInput rr,
               std::string name = "Pricer")
        : m_pricer(pricer), m_md(std::move(md)), m_yc(yc), m_ref(ref)
        , m_n(n_credits), m_rr(std::move(rr)), m_name(std::move(name)) {}

    [[nodiscard]] TranchesGreeksResult compute_greeks(
        const Market::Index_tranche& tranche, const Market::TranchesGrid& grid,
        double rho1, double rho2, const TranchesBumpConfig& cfg = {}) const
    {
        TranchesGreeksResult res;
        res.K1 = tranche.K1; res.K2 = tranche.K2;
        res.pricer_name = m_name;
        res.par_spread  = m_pricer.par_spread(tranche, grid, rho1, rho2);

        const double h_cs    = cfg.h_spread;
        const double h_rho   = cfg.h_rho;
        const double scale   = cfg.use_central_diff ? 2.0 : 1.0;
        const double base_npv = m_pricer.npv(tranche, grid, rho1, rho2);

        if (cfg.compute_cs01 || cfg.compute_gamma) {
            const double n_up = npv_cs_bump(tranche, grid, rho1, rho2, +h_cs);
            const double n_dn = cfg.use_central_diff
                ? npv_cs_bump(tranche, grid, rho1, rho2, -h_cs) : base_npv;
            if (cfg.compute_cs01)
                res.cs01 = (n_up - n_dn) / (scale * h_cs);
            if (cfg.compute_gamma) {
                const double n_dn_g = cfg.use_central_diff
                    ? n_dn : npv_cs_bump(tranche, grid, rho1, rho2, -h_cs);
                res.gamma = (n_up - 2.0*base_npv + n_dn_g) / (h_cs * h_cs);
            }
        }

        if (cfg.compute_dv01) {
            double mat = Core::year_fraction(tranche.valuation_date , tranche.MaturityDate , tranche.day_count);
            const auto g_up = Builder::bump_grid_rates(grid, +cfg.h_rate, mat);
            const double n_up = m_pricer.npv(tranche, g_up, rho1, rho2);
            double n_dn = base_npv;
            if (cfg.use_central_diff) {
                const auto g_dn = Builder::bump_grid_rates(grid, -cfg.h_rate, mat);
                n_dn = m_pricer.npv(tranche, g_dn, rho1, rho2);
            }
            res.dv01 = (n_up - n_dn) / (scale * cfg.h_rate);
        }

        if (cfg.compute_rho01) {
            const double r1u = std::clamp(rho1+h_rho, 0.0, 0.999);
            const double r2u = std::clamp(rho2+h_rho, 0.0, 0.999);
            const double n_up = m_pricer.npv(tranche, grid, r1u, r2u);
            double n_dn = base_npv;
            if (cfg.use_central_diff) {
                const double r1d = std::clamp(rho1-h_rho, 0.0, 0.999);
                const double r2d = std::clamp(rho2-h_rho, 0.0, 0.999);
                n_dn = m_pricer.npv(tranche, grid, r1d, r2d);
            }
            res.rho01 = (n_up - n_dn) / (scale * h_rho);
        }

        if (cfg.compute_bucketed_cs01) {
            const auto indices = resolve_pillars(cfg.cs01_pillars);
            const std::size_t nb = cfg.cs01_pillars.size();
            res.bucketed_cs01.resize(nb, 0.0);
            for (std::size_t b = 0; b < nb; ++b) {
                const std::size_t idx = indices[b];
                if (idx == static_cast<std::size_t>(-1)) continue;
                const auto c_up = Builder::build_bucketed(m_md, m_yc, m_ref, m_n, idx, +h_cs);
                const auto c_dn = Builder::build_bucketed(m_md, m_yc, m_ref, m_n, idx, -h_cs);
                res.bucketed_cs01[b] =
                    (Traits::make(c_up, m_n, m_rr).npv(tranche, grid, rho1, rho2)
                   - Traits::make(c_dn, m_n, m_rr).npv(tranche, grid, rho1, rho2))
                    / (2.0 * h_cs);
            }
        }

        if (cfg.compute_theta) {
            Market::Index_tranche tr1d = tranche;
            tr1d.MaturityDate = tr1d.MaturityDate.remove_days(1);
            const auto g1d = Market::build_time_grid(tr1d, m_yc);
            res.theta = m_pricer.npv(tr1d, g1d, rho1, rho2) - base_npv;
        }

        return res;
    }

    [[nodiscard]] TranchesBookGreeksResult compute_book_greeks(
        const std::vector<Market::Index_tranche>& tranches,
        const std::vector<Market::TranchesGrid>&  grids,
        const std::vector<double>& rho1s,
        const std::vector<double>& rho2s,
        const TranchesBumpConfig& cfg = {}) const
    {
        check_sizes(tranches.size(), grids.size(), rho1s.size(), rho2s.size(),
                    "compute_book_greeks");
        TranchesBookGreeksResult book;
        book.tranches.reserve(tranches.size());
        for (std::size_t i = 0; i < tranches.size(); ++i)
            book.tranches.push_back(
                compute_greeks(tranches[i], grids[i], rho1s[i], rho2s[i], cfg));
        return book;
    }

    [[nodiscard]] const std::string& name()   const { return m_name;   }
    [[nodiscard]] const Pricer&      pricer()  const { return m_pricer; }

private:

    const Pricer&        m_pricer;
    MDInput              m_md;
    const Market::YieldCurve& m_yc;
    const Core::Date&    m_ref;
    int                  m_n;
    RRInput              m_rr;
    std::string          m_name;

    [[nodiscard]] double npv_cs_bump(
        const Market::Index_tranche& tranche, const Market::TranchesGrid& grid,
        double rho1, double rho2, double h) const
    {
        const auto c = Builder::build_parallel(m_md, m_yc, m_ref, m_n, h);
        return Traits::make(c, m_n, m_rr).npv(tranche, grid, rho1, rho2);
    }

    [[nodiscard]] std::vector<std::size_t>
    resolve_pillars(const std::vector<double>& pillars) const
    {
        const auto& qs = Builder::quotes(m_md);
        std::vector<std::size_t> idx(pillars.size(), static_cast<std::size_t>(-1));
        for (std::size_t p = 0; p < pillars.size(); ++p)
            for (std::size_t q = 0; q < qs.size(); ++q)
                if (std::abs(qs[q].time - pillars[p]) < 0.1) { idx[p] = q; break; }
        return idx;
    }

    static void check_sizes(std::size_t a, std::size_t b, std::size_t c, std::size_t d,
                             const char* ctx)
    {
        if (a != b || a != c || a != d)
            throw std::invalid_argument(std::string(ctx) + ": mismatched sizes");
    }
};

template <typename PA, typename PB>
    requires HasTraits<PA> && HasTraits<PB>
[[nodiscard]] ModelCompareResult compare_models(
    const TranchesRiskEngine<PA>& eng_A, const TranchesRiskEngine<PB>& eng_B,
    const std::vector<Market::Index_tranche>& tranches,
    const std::vector<Market::TranchesGrid>&  grids,
    const std::vector<double>& rho1s, const std::vector<double>& rho2s)
{
    if (tranches.size() != grids.size() ||
        tranches.size() != rho1s.size() ||
        tranches.size() != rho2s.size())
        throw std::invalid_argument("compare_models: mismatched sizes");

    ModelCompareResult res;
    res.pricer_A_name = eng_A.name();
    res.pricer_B_name = eng_B.name();
    res.tranches.reserve(tranches.size());

    for (std::size_t i = 0; i < tranches.size(); ++i) {
        ModelComparePoint pt;
        pt.K1 = tranches[i].K1; pt.K2 = tranches[i].K2;
        pt.spread_A = eng_A.pricer().par_spread(tranches[i],grids[i],rho1s[i],rho2s[i])*1e4;
        pt.spread_B = eng_B.pricer().par_spread(tranches[i],grids[i],rho1s[i],rho2s[i])*1e4;
        pt.diff_bp  = pt.spread_A - pt.spread_B;
        pt.npv_A    = eng_A.pricer().npv(tranches[i],grids[i],rho1s[i],rho2s[i]);
        pt.npv_B    = eng_B.pricer().npv(tranches[i],grids[i],rho1s[i],rho2s[i]);
        pt.npv_diff = pt.npv_A - pt.npv_B;
        res.tranches.push_back(pt);
    }
    return res;
}

} // namespace Risk