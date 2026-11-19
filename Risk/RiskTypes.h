//
// RiskTypes.h - Core risk metrics and data structures
// Created for CDS portfolio risk management
//

#pragma once

#include <vector>
#include <string>
#include <cmath>
#include <stdexcept>

namespace Risk {

    // ─────────────────────────────────────────────────────────────────────────
    // Core Greek Structures
    // ─────────────────────────────────────────────────────────────────────────

    struct TrancheGreeks {
        double delta_spread {};          // ∂NPV / ∂spread [basis points]
        double gamma_spread {};          // ∂²NPV / ∂spread² [basis points]
        double vega_correlation {};      // ∂NPV / ∂correlation [bps/0.01 change]
        double rho_spread {};            // ∂NPV / ∂yield_curve [bps/1% rate move]
        double theta {};                 // ∂NPV / ∂time decay [daily P&L]
        double implied_spread {};        // Current implied par spread [bps]

        [[nodiscard]] double effective_duration() const noexcept {
            return std::abs(delta_spread) > 1e-12 ? delta_spread / 10000.0 : 0.0;
        }

        [[nodiscard]] double convexity() const noexcept {
            return gamma_spread / 10000.0 / 10000.0;
        }

        [[nodiscard]] bool is_valid() const noexcept {
            return std::isfinite(delta_spread) && std::isfinite(gamma_spread) &&
                   std::isfinite(vega_correlation) && std::isfinite(rho_spread) &&
                   std::isfinite(theta);
        }
    };

    // ─────────────────────────────────────────────────────────────────────────
    // Portfolio Greeks
    // ─────────────────────────────────────────────────────────────────────────

    struct PortfolioGreeks {
        double total_delta {};           // Portfolio-level spread delta
        double total_gamma {};           // Portfolio-level spread gamma
        double total_vega {};            // Portfolio correlation sensitivity
        double total_rho {};             // Portfolio interest rate sensitivity
        double total_theta {};           // Portfolio time decay
        double dv01 {};                  // Market value of 1bp move [€]
        double dollar_duration {};       // Duration in absolute € terms
        int n_instruments {};            // Number of instruments in portfolio

        [[nodiscard]] double weighted_correlation_vega() const noexcept {
            return total_vega / std::max(n_instruments, 1);
        }

        [[nodiscard]] bool is_valid() const noexcept {
            return std::isfinite(total_delta) && std::isfinite(total_gamma) &&
                   std::isfinite(total_vega) && std::isfinite(total_rho) &&
                   std::isfinite(dv01) && n_instruments > 0;
        }
    };

    // ─────────────────────────────────────────────────────────────────────────
    // Risk Metrics & Loss Distribution
    // ─────────────────────────────────────────────────────────────────────────

    struct LossMetrics {
        double expected_loss {};         // E[L] [€]
        double var_95 {};                // Value at Risk @ 95% confidence [€]
        double var_99 {};                // Value at Risk @ 99% confidence [€]
        double cvar_95 {};               // Conditional VaR @ 95% [€]
        double cvar_99 {};               // Conditional VaR @ 99% [€]
        double potential_loss {};        // Max loss under base correlation scenario [€]
        double recovery_rate {};         // Portfolio-weighted recovery rate
        double average_correlation {};   // Portfolio-weighted average correlation

        [[nodiscard]] bool is_valid() const noexcept {
            return std::isfinite(expected_loss) && std::isfinite(var_95) &&
                   std::isfinite(cvar_95) && recovery_rate >= 0.0 && recovery_rate <= 1.0;
        }
    };

    // ─────────────────────────────────────────────────────────────────────────
    // Stress Test Scenario
    // ─────────────────────────────────────────────────────────────────────────

    enum class StressType {
        Parallel,           // All spreads move by same amount
        CorridorWiden,      // Credit spreads widen non-uniformly
        CorrelationShock,   // Base correlation changes
        RecoveryShock,      // Recovery rate assumption change
        VolatilityShock,    // Volatility surface moves
        CurveParallel,      // Yield curve parallel shift
        CurveRotation,      // Yield curve steepening/flattening
    };

    struct StressScenario {
        std::string name;
        StressType type;
        double magnitude {};             // Primary shock magnitude (bps or %)
        double secondary_magnitude {};   // Secondary shock (for complex scenarios)
        double probability {};           // Historical/estimated probability [0,1]
        bool is_symmetric {false};       // Apply both +/- direction

        StressScenario() = default;
        StressScenario(const std::string& n, StressType t, double mag, double prob = 0.01)
            : name(n), type(t), magnitude(mag), probability(prob) {}

        [[nodiscard]] bool is_valid() const noexcept {
            return probability >= 0.0 && probability <= 1.0 && std::isfinite(magnitude);
        }
    };

    // ─────────────────────────────────────────────────────────────────────────
    // Stress Test Results
    // ─────────────────────────────────────────────────────────────────────────

    struct StressResult {
        std::string scenario_name;
        double pnl_change {};            // P&L change under this scenario [€]
        double spread_move {};           // Associated spread move [bps]
        double correlation_move {};      // Associated correlation move
        double portfolio_recovery {};    // Recovery rate after scenario
        double max_tranche_loss {};      // Largest single tranche loss in scenario
        double is_breach {false};        // Whether scenario breaks risk limits

        [[nodiscard]] bool is_loss() const noexcept { return pnl_change < 0.0; }
        [[nodiscard]] double loss_magnitude() const noexcept { return std::abs(pnl_change); }
    };

    // ─────────────────────────────────────────────────────────────────────────
    // Risk Limits Configuration
    // ─────────────────────────────────────────────────────────────────────────

    struct RiskLimits {
        double max_dv01 {};              // Max daily value at risk [€]
        double max_correlation_vega {}; // Max correlation sensitivity [bps]
        double max_duration {};          // Max portfolio duration [years]
        double max_loss_var_95 {};       // Max 95% VaR [€]
        double max_concentration {};     // Max single tranche weight [%]
        double min_rating {};            // Minimum average rating constraint

        [[nodiscard]] bool is_valid() const noexcept {
            return max_dv01 > 0.0 && max_correlation_vega > 0.0 &&
                   max_duration > 0.0 && max_loss_var_95 > 0.0;
        }
    };

    // ─────────────────────────────────────────────────────────────────────────
    // Risk Report Snapshot
    // ─────────────────────────────────────────────────────────────────────────

    struct RiskSnapshot {
        std::string portfolio_id;
        std::string timestamp;
        PortfolioGreeks greeks;
        LossMetrics loss_metrics;
        RiskLimits limits;
        std::vector<StressResult> stress_results;
        bool all_limits_respected {true};
        std::string warning_message;

        [[nodiscard]] int count_breached_limits() const noexcept {
            int count = 0;
            for (const auto& sr : stress_results) {
                if (sr.is_breach) ++count;
            }
            return count;
        }
    };

}  // namespace Risk

