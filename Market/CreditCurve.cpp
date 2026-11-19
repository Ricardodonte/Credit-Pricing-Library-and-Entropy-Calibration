//
// Created by ricar on 10/04/2026.
//

#include "CreditCurve.h"
#include <cmath>

namespace Market {

    double CreditCurve::survival_probability(const double t) const {

        if (t <= 0.0) return 1.0;

        const auto it = std::lower_bound(intensity.begin(), intensity.end(), t,
            [](const Core::Point& p, const double val){ return p.time < val; });

        if (it == intensity.begin()) {
            const double cum = it->value * t;
            return std::exp(-cum);
        }

        double cum = 0.0;
        double t_prev = 0.0;
        for (auto kt = intensity.begin(); kt != it; ++kt) {
            cum   += kt->value * (kt->time - t_prev);
            t_prev = kt->time;
        }

        if (it != intensity.end())
            cum += it->value * (t - t_prev);

        return std::exp(-cum);
    }

    double CreditCurve::survival_probability(const double t, const double T) const {
        return survival_probability(T)/survival_probability(t);
    }

    std::pair<std::vector<double>, std::vector<double> > CreditBoot::discount_grid(const CDS &cds, const CDS::CDSGrids &grid) const {

        const size_t nd = grid.defaultTimes.size();
        const size_t np = grid.premiumTimes.size();

        std::vector<double> P_def(nd), P_prem(np);

        for (size_t k = 0; k < nd; ++k) {
            const Core::Date& d = grid.defaultTimes[k];
            const double t = year_fraction(cds.ValuationDate, d, cds.conventionalDayCount);
            P_def[k]  = m_yieldCurve.discount(t);
        }

        for (size_t n = 0; n < np; ++n) {
            const Core::Date& d = grid.premiumTimes[n];
            const double t = year_fraction(cds.ValuationDate, d, cds.conventionalDayCount);
            P_prem[n]  = m_yieldCurve.discount(t);
        }

        return {P_def, P_prem};
    }

    double CreditBoot::par_spread(const CDS &cds, const CDS::CDSGrids &grid,
        const std::pair<std::vector<double>, std::vector<double> > &df_grid) const {

        const auto P_def = df_grid.first;
        const auto P_prem = df_grid.second;
        const size_t nd = grid.defaultTimes.size();
        const size_t np = grid.premiumTimes.size();

        std::vector<double> Q_def(nd),  Q_prem(np);

        for (size_t k = 0; k < nd; ++k) {
            const Core::Date& d = grid.defaultTimes[k];
            const double t = year_fraction(cds.ValuationDate, d, cds.conventionalDayCount);
            Q_def[k]  = m_creditCurve.survival_probability(t);
        }

        for (size_t n = 0; n < np; ++n) {
            const Core::Date& d = grid.premiumTimes[n];
            const double t = year_fraction(cds.ValuationDate, d, cds.conventionalDayCount);
            Q_prem[n]  = m_creditCurve.survival_probability(t);
        }

        double defLeg = 0.0;
        for (size_t k = 1; k < nd; ++k) {
            defLeg  += (P_def[k-1] + P_def[k]) * (Q_def[k-1]  - Q_def[k]);
        }
        defLeg   *= (1.0 - cds.RecoveryRate);

        double rpv01 = 0.0;
        for (size_t n = 1; n < np; ++n) {
            const Core::Date& d0 = grid.premiumTimes[n-1];
            const Core::Date& d1 = grid.premiumTimes[n];
            const double tau = year_fraction(d0, d1, cds.conventionalDayCount);
            rpv01   += tau * P_prem[n] * (Q_prem[n-1]  + Q_prem[n]);
        }

        const double S  = defLeg / rpv01;
        return S;
    }


     void CreditBoot::bootstrap() {

        for (auto q : m_MarketData.quotes) {
            CDS cds;
            cds.maturity          = q.time;
            cds.RecoveryRate      = m_MarketData.recoveryRate;
            cds.EffectiveDate     = m_MarketData.effectiveDate;
            cds.ValuationDate     = m_MarketData.valuationDate;
            cds.frequency         = m_MarketData.frequency;
            cds.conventionalDayCount = Core::DayCount::ACT_360;
            cds.ContractualSpread = q.value;

            const CDS::CDSGrids grid = cds.buildCDSGrids(1);
            const auto df_grid = discount_grid(cds,grid);

            const double t_maturity = year_fraction(cds.ValuationDate,
                                         cds.get_MaturityDate(),
                                         cds.conventionalDayCount);

            m_creditCurve.intensity.push_back({ t_maturity, 0.0 });

            constexpr double lam_lo = 1e-8;
            constexpr double lam_hi = 5.0;

            auto objective = [&](const double lam) -> double {
                m_creditCurve.intensity.back().value = lam;
                return par_spread(cds, grid, df_grid) - cds.ContractualSpread;
            };

            const double fa = objective(lam_lo);
            const double fb = objective(lam_hi);
            if (fa * fb > 0.0) {
                throw std::runtime_error(
                    "CreditBoot: No root value in [lam_lo, lam_hi] for the pillar t="
                    + std::to_string(q.time)
                    + " spread=" + std::to_string(q.value)
                );
            }

            m_creditCurve.intensity.back().value = m_solver.solve(objective, lam_lo, lam_hi);

            const double repriced_spread = par_spread(cds, grid, df_grid);
            m_repricing_errors.push_back({ t_maturity, (repriced_spread - q.value) * 1e4 });
        }
    }

}
