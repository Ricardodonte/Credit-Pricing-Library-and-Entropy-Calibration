//
// RiskCalculator.h - Greeks computation engine using finite differences
// Efficient bumping strategies for CDS tranche portfolios
//

#pragma once

#include <vector>
#include <memory>
#include <cmath>
#include <algorithm>
#include <numeric>

#include "RiskTypes.h"
#include "Pricers/base_tranche_pricer.h"
#include "Market/tranches_instruments.h"
#include "Market/YieldCurve.h"
#include "Market/CreditCurve.h"

namespace Risk {

    class RiskCalculator {
    public:
        // ─────────────────────────────────────────────────────────────────────
        // Constructor & Configuration
        // ─────────────────────────────────────────────────────────────────────

        explicit RiskCalculator(
            const Pricer::BaseTranchePricer& pricer,
            double bump_size_bps = 1.0,
            double correlation_bump = 0.01
        ) : m_pricer(pricer),
            m_bump_bps(bump_size_bps),
            m_corr_bump(correlation_bump),
            m_tenor_bump_days(1) {}

        // ─────────────────────────────────────────────────────────────────────
        // Single Tranche Greeks
        // ─────────────────────────────────────────────────────────────────────

        [[nodiscard]] TrancheGreeks compute_tranche_greeks(
            const Market::Index_tranche& tranche,
            const Market::TranchesGrid& grid,
            double rho1,
            double rho2,
            double base_npv
        ) const;

        // ─────────────────────────────────────────────────────────────────────
        // Portfolio Greeks (vectorized)
        // ─────────────────────────────────────────────────────────────────────

        [[nodiscard]] PortfolioGreeks compute_portfolio_greeks(
            const std::vector<Market::Index_tranche>& tranches,
            const std::vector<Market::TranchesGrid>& grids,
            const std::vector<double>& rho1_vector,
            const std::vector<double>& rho2_vector,
            const std::vector<double>& nominal_values
        ) const;

        // ─────────────────────────────────────────────────────────────────────
        // Loss Distribution Estimation
        // ─────────────────────────────────────────────────────────────────────

        [[nodiscard]] LossMetrics estimate_loss_metrics(
            const std::vector<double>& loss_samples,
            double recovery_rate,
            double avg_correlation
        ) const;

        // ─────────────────────────────────────────────────────────────────────
        // Sensitivity Analysis
        // ─────────────────────────────────────────────────────────────────────

        struct SensitivityProfile {
            std::vector<double> spread_bumps;      // Bumps applied [bps]
            std::vector<double> pnl_changes;       // P&L responses [€]
            std::vector<double> derivatives;       // Numerical derivatives
            double r_squared {};                   // Quality of fit
            bool is_convex {false};                // Detected convexity
        };

        [[nodiscard]] SensitivityProfile compute_spread_sensitivity(
            const Market::Index_tranche& tranche,
            const Market::TranchesGrid& grid,
            double rho1,
            double rho2,
            int n_bumps = 5
        ) const;

        // ─────────────────────────────────────────────────────────────────────
        // Bump & Reprice Utilities
        // ─────────────────────────────────────────────────────────────────────

        [[nodiscard]] double compute_base_npv(
            const Market::Index_tranche& tranche,
            const Market::TranchesGrid& grid,
            double rho1,
            double rho2
        ) const;

        [[nodiscard]] double compute_spread_bumped_npv(
            const Market::Index_tranche& tranche,
            const Market::TranchesGrid& grid,
            double rho1,
            double rho2,
            double bump_bps
        ) const;

        [[nodiscard]] double compute_correlation_bumped_npv(
            const Market::Index_tranche& tranche,
            const Market::TranchesGrid& grid,
            double rho1,
            double rho2,
            double bump_rho
        ) const;

        // ─────────────────────────────────────────────────────────────────────
        // Configuration
        // ─────────────────────────────────────────────────────────────────────

        void set_bump_size(double bump_bps) noexcept {
            m_bump_bps = std::max(bump_bps, 0.1);
        }

        void set_correlation_bump(double bump) noexcept {
            m_corr_bump = std::clamp(bump, 0.001, 0.05);
        }

        [[nodiscard]] double get_bump_size() const noexcept { return m_bump_bps; }
        [[nodiscard]] double get_correlation_bump() const noexcept { return m_corr_bump; }

    private:
        const Pricer::BaseTranchePricer& m_pricer;
        double m_bump_bps;
        double m_corr_bump;
        int m_tenor_bump_days;

        // Central difference finite differences
        [[nodiscard]] double central_difference(
            double f_up,
            double f_down,
            double bump
        ) const noexcept {
            return (f_up - f_down) / (2.0 * bump);
        }

        // Second derivative via finite differences
        [[nodiscard]] double second_derivative(
            double f_up,
            double f_mid,
            double f_down,
            double bump
        ) const noexcept {
            return (f_up - 2.0 * f_mid + f_down) / (bump * bump);
        }
    };

}  // namespace Risk

