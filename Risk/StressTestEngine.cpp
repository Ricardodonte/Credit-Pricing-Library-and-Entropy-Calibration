//
// StressTestEngine.cpp - Scenario analysis implementation
//

#include "Risk/StressTestEngine.h"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <stdexcept>

namespace Risk {

    // ─────────────────────────────────────────────────────────────────────────
    // Standard Scenarios Registration
    // ─────────────────────────────────────────────────────────────────────────

    void StressTestEngine::register_standard_scenarios() {
        // Historical crisis scenarios

        // 2008 Financial Crisis: +200bps parallel
        add_scenario(StressScenario("2008 Crisis Parallel", StressType::Parallel, 200.0, 0.001));

        // Steepener scenario: correlation shock
        add_scenario(StressScenario("Correlation Shock (ρ -0.15)", StressType::CorrelationShock, -0.15, 0.015));

        // Recovery shock: lower recovery assumptions
        add_scenario(StressScenario("Recovery Shock (R -10%)", StressType::RecoveryShock, -0.10, 0.025));

        // Corridor widen: senior tranches widen more
        add_scenario(StressScenario("Corridor Widen", StressType::CorridorWiden, 150.0, 0.02));

        // Yield curve parallel shift
        add_scenario(StressScenario("Curve +100bps", StressType::CurveParallel, 100.0, 0.05));

        // Curve steepening
        add_scenario(StressScenario("Curve Steepen", StressType::CurveRotation, 50.0, 0.10));

        // Low correlation environment
        add_scenario(StressScenario("Low Correlation (ρ=0.15)", StressType::CorrelationShock, -0.30, 0.08));

        // High correlation environment
        add_scenario(StressScenario("High Correlation (ρ=0.80)", StressType::CorrelationShock, 0.50, 0.05));
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Scenario Application
    // ─────────────────────────────────────────────────────────────────────────

    void StressTestEngine::apply_stress_to_tranches(
        std::vector<Market::Index_tranche>& tranches,
        const StressScenario& scenario
    ) const {

        for (auto& tranche : tranches) {
            switch (scenario.type) {
                case StressType::Parallel: {
                    // All spreads move by same amount (bps)
                    tranche.contractual_spread += scenario.magnitude * 1e-4;
                    tranche.fair_spread += scenario.magnitude * 1e-4;
                    break;
                }

                case StressType::CorridorWiden: {
                    // Senior tranches (higher K) widen more
                    double K_mid = (tranche.K1 + tranche.K2) / 2.0;
                    double widening_factor = 1.0 + K_mid;
                    double spread_move = scenario.magnitude * widening_factor * 1e-4;
                    tranche.contractual_spread += spread_move;
                    tranche.fair_spread += spread_move;
                    break;
                }

                case StressType::CorrelationShock:
                case StressType::RecoveryShock:
                case StressType::VolatilityShock:
                case StressType::CurveParallel: {
                    // These are handled at portfolio level via correlation/rates
                    // Slight spread adjustment for curve moves
                    tranche.contractual_spread += scenario.magnitude * 0.1 * 1e-4;
                    tranche.fair_spread += scenario.magnitude * 0.1 * 1e-4;
                    break;
                }

                case StressType::CurveRotation: {
                    // Shorter end steepens more
                    double duration_weight = tranche.K1 < 0.10 ? 1.5 : 1.0;
                    tranche.contractual_spread += scenario.magnitude * duration_weight * 1e-4;
                    tranche.fair_spread += scenario.magnitude * duration_weight * 1e-4;
                    break;
                }

                default:
                    break;
            }
        }

        // Ensure spreads stay reasonable
        for (auto& tranche : tranches) {
            tranche.contractual_spread = std::max(tranche.contractual_spread, 0.0001);
            tranche.fair_spread = std::max(tranche.fair_spread, 0.0001);
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Run Single Stress Scenario
    // ─────────────────────────────────────────────────────────────────────────

    StressResult StressTestEngine::run_scenario(
        const StressScenario& scenario,
        const std::vector<Market::Index_tranche>& tranches,
        const std::vector<Market::TranchesGrid>& grids,
        const std::vector<double>& rho1_vector,
        const std::vector<double>& rho2_vector,
        const std::vector<double>& base_pnl_vector
    ) const {

        StressResult result;
        result.scenario_name = scenario.name;

        // Create stressed copies
        auto stressed_tranches = tranches;
        apply_stress_to_tranches(stressed_tranches, scenario);

        // Adjust correlations for correlation shocks
        auto stressed_rho1 = rho1_vector;
        auto stressed_rho2 = rho2_vector;

        if (scenario.type == StressType::CorrelationShock) {
            for (auto& rho : stressed_rho1) {
                rho = std::clamp(rho + scenario.magnitude, 0.0, 0.9999);
            }
            for (auto& rho : stressed_rho2) {
                rho = std::clamp(rho + scenario.magnitude, 0.0, 0.9999);
            }
            result.correlation_move = scenario.magnitude;
        }

        // Compute stressed PnL
        result.pnl_change = compute_stressed_portfolio_pnl(
            stressed_tranches, grids,
            stressed_rho1, stressed_rho2,
            base_pnl_vector
        ) - m_baseline_pnl;

        result.spread_move = scenario.magnitude;
        result.max_tranche_loss = 0.0; // Could be refined

        return result;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Run All Scenarios
    // ─────────────────────────────────────────────────────────────────────────

    std::vector<StressResult> StressTestEngine::run_all_scenarios(
        const std::vector<Market::Index_tranche>& tranches,
        const std::vector<Market::TranchesGrid>& grids,
        const std::vector<double>& rho1_vector,
        const std::vector<double>& rho2_vector,
        const std::vector<double>& base_pnl_vector
    ) const {

        std::vector<StressResult> results;
        results.reserve(m_scenarios.size());

        for (const auto& scenario : m_scenarios) {
            auto result = run_scenario(scenario, tranches, grids, rho1_vector, rho2_vector, base_pnl_vector);
            results.push_back(result);
        }

        return results;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Extreme Scenario Analysis
    // ─────────────────────────────────────────────────────────────────────────

    StressTestEngine::ExtremeScenarioResult StressTestEngine::analyze_extreme_scenarios(
        const std::vector<StressResult>& results
    ) const {

        ExtremeScenarioResult analysis;

        if (results.empty()) {
            return analysis;
        }

        // Find worst cases
        auto sorted_results = results;
        std::sort(sorted_results.begin(), sorted_results.end(),
                  [](const StressResult& a, const StressResult& b) {
                      return a.pnl_change < b.pnl_change;
                  });

        // Top 3 worst scenarios
        int n_worst = std::min(3, static_cast<int>(sorted_results.size()));
        for (int i = 0; i < n_worst; ++i) {
            analysis.worst_case_scenarios.push_back(sorted_results[i]);
        }

        analysis.largest_loss = sorted_results[0].loss_magnitude();

        // Calculate statistics
        analysis.n_scenarios_profitable = std::count_if(
            results.begin(), results.end(),
            [](const StressResult& r) { return r.pnl_change > 0.0; }
        );

        double total_loss = 0.0;
        int loss_count = 0;
        for (const auto& r : results) {
            if (r.is_loss()) {
                total_loss += r.loss_magnitude();
                ++loss_count;
            }
        }
        analysis.expected_scenario_loss = loss_count > 0 ? total_loss / loss_count : 0.0;

        // VaR of scenario results
        auto sorted_losses = sorted_results;
        size_t idx_95 = static_cast<size_t>(sorted_losses.size() * 0.95);
        analysis.var_of_scenarios_95 = sorted_losses[std::min(idx_95, sorted_losses.size() - 1)].pnl_change;

        return analysis;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Scenario Statistics
    // ─────────────────────────────────────────────────────────────────────────

    StressTestEngine::ScenarioStatistics StressTestEngine::compute_scenario_statistics(
        const std::vector<StressResult>& results,
        double probability_threshold
    ) const {

        ScenarioStatistics stats;

        if (results.empty()) {
            return stats;
        }

        // Mean loss
        double sum_loss = 0.0;
        int n_losses = 0;
        for (const auto& r : results) {
            if (r.is_loss()) {
                sum_loss += r.loss_magnitude();
                ++n_losses;
            }
        }
        stats.mean_loss = n_losses > 0 ? sum_loss / n_losses : 0.0;

        // Standard deviation
        double sum_sq_dev = 0.0;
        for (const auto& r : results) {
            if (r.is_loss()) {
                double dev = r.loss_magnitude() - stats.mean_loss;
                sum_sq_dev += dev * dev;
            }
        }
        stats.std_dev_loss = n_losses > 1 ? std::sqrt(sum_sq_dev / (n_losses - 1)) : 0.0;

        // Extremes
        stats.worst_loss = 0.0;
        stats.best_gain = 0.0;
        for (const auto& r : results) {
            if (r.pnl_change < 0 && r.loss_magnitude() > stats.worst_loss) {
                stats.worst_loss = r.loss_magnitude();
            }
            if (r.pnl_change > 0 && r.pnl_change > stats.best_gain) {
                stats.best_gain = r.pnl_change;
            }
        }

        stats.loss_probability = static_cast<double>(n_losses) / results.size();
        stats.n_breaching_scenarios = std::count_if(results.begin(), results.end(),
                                                     [probability_threshold](const StressResult& r) {
                                                         return r.is_breach;
                                                     });

        return stats;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Correlation Hedge Analysis
    // ─────────────────────────────────────────────────────────────────────────

    StressTestEngine::CorrelationHedgeAnalysis StressTestEngine::compute_optimal_correlation_hedge(
        const std::vector<StressResult>& correlation_scenarios
    ) const {

        CorrelationHedgeAnalysis analysis;

        if (correlation_scenarios.empty()) {
            return analysis;
        }

        // Find scenarios with high negative correlation shocks
        double max_loss = 0.0;
        for (const auto& sr : correlation_scenarios) {
            if (sr.pnl_change < 0) {
                max_loss = std::max(max_loss, sr.loss_magnitude());
            }
        }

        if (max_loss > 1e-6) {
            analysis.hedge_needed = true;
            analysis.optimal_hedge_ratio = 0.50; // Hedge 50% of position
            analysis.residual_correlation_risk = max_loss * (1.0 - analysis.optimal_hedge_ratio);
            analysis.hedge_cost = max_loss * 0.05; // 5% cost to hedge
            analysis.recommended_instrument = "Short correlation via iTraxx or single-name CDS";
            analysis.residual_correlation_risk = (max_loss * (1.0 - analysis.optimal_hedge_ratio)) / max_loss * 100.0; // %
        }

        return analysis;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // compute_stressed_portfolio_pnl - Helper
    // ─────────────────────────────────────────────────────────────────────────

    double StressTestEngine::compute_stressed_portfolio_pnl(
        const std::vector<Market::Index_tranche>& tranches,
        const std::vector<Market::TranchesGrid>& grids,
        const std::vector<double>& rho1_vector,
        const std::vector<double>& rho2_vector,
        const std::vector<double>& base_pnl_vector
    ) const {

        double total_pnl = 0.0;

        if (tranches.size() != grids.size() || tranches.size() != base_pnl_vector.size()) {
            return 0.0;
        }

        for (size_t i = 0; i < tranches.size(); ++i) {
            // Stressed NPV would be computed here via pricer
            // For now, just accumulate stressed position values
            total_pnl += base_pnl_vector[i];
        }

        return total_pnl;
    }

}  // namespace Risk

