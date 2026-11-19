//
// RiskCalculator.cpp - Implementation of Greeks computation
//

#include "Risk/RiskCalculator.h"
#include <cmath>
#include <algorithm>
#include <numeric>

namespace Risk {

    // ─────────────────────────────────────────────────────────────────────────
    // Single Tranche Greeks Computation
    // ─────────────────────────────────────────────────────────────────────────

    TrancheGreeks RiskCalculator::compute_tranche_greeks(
        const Market::Index_tranche& tranche,
        const Market::TranchesGrid& grid,
        double rho1,
        double rho2,
        double base_npv
    ) const {

        TrancheGreeks greeks;

        // Compute implied par spread
        greeks.implied_spread = m_pricer.par_spread(tranche, grid, rho1, rho2) * 1e4;

        // ─── Delta (spread sensitivity) using central differences ───
        {
            const double bump = m_bump_bps * 1e-4;
            double npv_up = compute_spread_bumped_npv(tranche, grid, rho1, rho2, bump);
            double npv_down = compute_spread_bumped_npv(tranche, grid, rho1, rho2, -bump);

            // Delta is change per 1bp (0.01%)
            greeks.delta_spread = central_difference(npv_up, npv_down, bump) * 1e-4;
        }

        // ─── Gamma (spread convexity) using 3-point formula ───
        {
            const double bump = m_bump_bps * 1e-4;
            double npv_up = compute_spread_bumped_npv(tranche, grid, rho1, rho2, bump);
            double npv_down = compute_spread_bumped_npv(tranche, grid, rho1, rho2, -bump);

            greeks.gamma_spread = second_derivative(npv_up, base_npv, npv_down, bump)
                                * (1e-4 * 1e-4);
        }

        // ─── Vega (correlation sensitivity) using central differences ───
        {
            const double rho1_up = rho1 + m_corr_bump;
            const double rho1_down = rho1 - m_corr_bump;

            if (rho1_up >= 0.0 && rho1_up <= 0.9999 && rho1_down >= 0.0) {
                double npv_up = m_pricer.npv(tranche, grid, rho1_up, rho2);
                double npv_down = m_pricer.npv(tranche, grid, rho1_down, rho2);
                greeks.vega_correlation = central_difference(npv_up, npv_down, m_corr_bump) * 1e4;
            } else {
                greeks.vega_correlation = 0.0;
            }
        }

        // ─── Rho (interest rate sensitivity - tenor bump) ───
        {
            // Simplified: 100bp parallel shift
            const double rate_bump = 0.01; // +100 bps
            // In production: would bump the yield curve used in grid.default_dFactors
            // For now, simplified approximation
            greeks.rho_spread = -(base_npv * 0.05); // Typical DV01 order of magnitude
        }

        // ─── Theta (time decay) - daily decay ───
        {
            const double dy = 1.0 / 365.0; // One day in years
            greeks.theta = -(base_npv * 0.001); // Order of magnitude estimate
        }

        return greeks;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Portfolio Greeks Aggregation
    // ─────────────────────────────────────────────────────────────────────────

    PortfolioGreeks RiskCalculator::compute_portfolio_greeks(
        const std::vector<Market::Index_tranche>& tranches,
        const std::vector<Market::TranchesGrid>& grids,
        const std::vector<double>& rho1_vector,
        const std::vector<double>& rho2_vector,
        const std::vector<double>& nominal_values
    ) const {

        if (tranches.empty()) {
            return {};
        }

        PortfolioGreeks portfolio;
        portfolio.n_instruments = static_cast<int>(tranches.size());

        double total_npv = 0.0;
        double total_nominal = 0.0;

        for (size_t i = 0; i < tranches.size(); ++i) {
            double base_npv = m_pricer.npv(tranches[i], grids[i], rho1_vector[i], rho2_vector[i]);
            auto tranche_g = compute_tranche_greeks(
                tranches[i], grids[i],
                rho1_vector[i], rho2_vector[i],
                base_npv
            );

            portfolio.total_delta += tranche_g.delta_spread * nominal_values[i];
            portfolio.total_gamma += tranche_g.gamma_spread * nominal_values[i];
            portfolio.total_vega += tranche_g.vega_correlation * nominal_values[i];
            portfolio.total_rho += tranche_g.rho_spread * nominal_values[i];
            portfolio.total_theta += tranche_g.theta * nominal_values[i];

            total_npv += base_npv;
            total_nominal += nominal_values[i];
        }

        // Compute DV01 (market value of 1bp move)
        portfolio.dv01 = std::abs(portfolio.total_delta);

        // Dollar duration: duration in absolute terms
        if (total_npv > 1e-12) {
            portfolio.dollar_duration = portfolio.dv01 * 10000.0;
        }

        return portfolio;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Loss Metrics Estimation
    // ─────────────────────────────────────────────────────────────────────────

    LossMetrics RiskCalculator::estimate_loss_metrics(
        const std::vector<double>& loss_samples,
        double recovery_rate,
        double avg_correlation
    ) const {

        LossMetrics metrics;
        metrics.recovery_rate = recovery_rate;
        metrics.average_correlation = avg_correlation;

        if (loss_samples.empty()) {
            return metrics;
        }

        // Expected Loss
        metrics.expected_loss = std::accumulate(loss_samples.begin(), loss_samples.end(), 0.0)
                              / loss_samples.size();

        // VaR and CVaR computation (sorted)
        auto sorted_losses = loss_samples;
        std::sort(sorted_losses.begin(), sorted_losses.end());

        size_t idx_95 = static_cast<size_t>(sorted_losses.size() * 0.95);
        size_t idx_99 = static_cast<size_t>(sorted_losses.size() * 0.99);

        metrics.var_95 = sorted_losses[std::min(idx_95, sorted_losses.size() - 1)];
        metrics.var_99 = sorted_losses[std::min(idx_99, sorted_losses.size() - 1)];

        // CVaR (expected shortfall): average of top 5% losses
        double cvar_95_sum = 0.0;
        int cvar_95_count = 0;
        for (size_t i = idx_95; i < sorted_losses.size(); ++i) {
            cvar_95_sum += sorted_losses[i];
            ++cvar_95_count;
        }
        metrics.cvar_95 = cvar_95_count > 0 ? cvar_95_sum / cvar_95_count : metrics.var_95;

        // CVaR 99%
        double cvar_99_sum = 0.0;
        int cvar_99_count = 0;
        for (size_t i = idx_99; i < sorted_losses.size(); ++i) {
            cvar_99_sum += sorted_losses[i];
            ++cvar_99_count;
        }
        metrics.cvar_99 = cvar_99_count > 0 ? cvar_99_sum / cvar_99_count : metrics.var_99;

        metrics.potential_loss = sorted_losses.back(); // Worst case

        return metrics;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Sensitivity Profiling
    // ─────────────────────────────────────────────────────────────────────────

    RiskCalculator::SensitivityProfile RiskCalculator::compute_spread_sensitivity(
        const Market::Index_tranche& tranche,
        const Market::TranchesGrid& grid,
        double rho1,
        double rho2,
        int n_bumps
    ) const {

        SensitivityProfile profile;
        double base_npv = compute_base_npv(tranche, grid, rho1, rho2);

        // Generate bumps from -n_bumps to +n_bumps
        for (int i = -n_bumps; i <= n_bumps; ++i) {
            double bump_bps = i * m_bump_bps;
            double bump_amount = bump_bps * 1e-4;

            profile.spread_bumps.push_back(bump_bps);

            double npv_bumped = compute_spread_bumped_npv(tranche, grid, rho1, rho2, bump_amount);
            double pnl_change = npv_bumped - base_npv;
            profile.pnl_changes.push_back(pnl_change);

            // Compute local derivative
            if (i > -n_bumps && i < n_bumps) {
                double derivative = (profile.pnl_changes[i + n_bumps] - profile.pnl_changes[i + n_bumps - 1])
                                  / (2.0 * m_bump_bps * 1e-4);
                profile.derivatives.push_back(derivative);
            }
        }

        // Assess convexity by checking if gamma is significant
        if (profile.derivatives.size() >= 2) {
            double gamma_estimate = (profile.derivatives.back() - profile.derivatives.front())
                                  / (4.0 * m_bump_bps * 1e-4);
            profile.is_convex = std::abs(gamma_estimate) > 1e-8;
        }

        return profile;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Utility Functions
    // ─────────────────────────────────────────────────────────────────────────

    double RiskCalculator::compute_base_npv(
        const Market::Index_tranche& tranche,
        const Market::TranchesGrid& grid,
        double rho1,
        double rho2
    ) const {
        return m_pricer.npv(tranche, grid, rho1, rho2);
    }

    double RiskCalculator::compute_spread_bumped_npv(
        const Market::Index_tranche& tranche,
        const Market::TranchesGrid& grid,
        double rho1,
        double rho2,
        double bump_bps
    ) const {
        // Create bumped copy
        auto bumped_tranche = tranche;
        bumped_tranche.contractual_spread += bump_bps;
        bumped_tranche.fair_spread += bump_bps;

        return m_pricer.npv(bumped_tranche, grid, rho1, rho2);
    }

    double RiskCalculator::compute_correlation_bumped_npv(
        const Market::Index_tranche& tranche,
        const Market::TranchesGrid& grid,
        double rho1,
        double rho2,
        double bump_rho
    ) const {
        double rho1_bumped = std::clamp(rho1 + bump_rho, 0.0, 0.9999);
        double rho2_bumped = std::clamp(rho2 + bump_rho, 0.0, 0.9999);

        return m_pricer.npv(tranche, grid, rho1_bumped, rho2_bumped);
    }

}  // namespace Risk

