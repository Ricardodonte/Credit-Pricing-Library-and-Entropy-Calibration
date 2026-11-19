//
// StressTestEngine.h - Scenario analysis and stress testing framework
// Multi-dimensional shock analysis for CDS portfolios
//

#pragma once

#include <vector>
#include <string>
#include <map>
#include <functional>
#include <algorithm>
#include <numeric>

#include "RiskTypes.h"
#include "Pricers/base_tranche_pricer.h"
#include "Market/tranches_instruments.h"

namespace Risk {

    class StressTestEngine {
    public:
        using ScenarioApplier = std::function<void(
            Market::Index_tranche&,
            const StressScenario&
        )>;

        // ─────────────────────────────────────────────────────────────────────
        // Constructor & Setup
        // ─────────────────────────────────────────────────────────────────────

        explicit StressTestEngine(const Pricer::BaseTranchePricer& pricer)
            : m_pricer(pricer), m_baseline_pnl(0.0) {
            register_standard_scenarios();
        }

        // ─────────────────────────────────────────────────────────────────────
        // Scenario Registration & Management
        // ─────────────────────────────────────────────────────────────────────

        void add_scenario(const StressScenario& scenario) {
            if (!scenario.is_valid()) {
                throw std::invalid_argument("Invalid stress scenario: probability out of range");
            }
            m_scenarios.push_back(scenario);
        }

        void add_custom_scenario(
            const std::string& name,
            const ScenarioApplier& applier
        ) {
            m_custom_appliers[name] = applier;
        }

        [[nodiscard]] size_t num_scenarios() const noexcept { return m_scenarios.size(); }
        [[nodiscard]] const std::vector<StressScenario>& get_scenarios() const noexcept {
            return m_scenarios;
        }

        // ─────────────────────────────────────────────────────────────────────
        // Baseline P&L Setup
        // ─────────────────────────────────────────────────────────────────────

        void set_baseline_pnl(double pnl) noexcept {
            m_baseline_pnl = pnl;
        }

        [[nodiscard]] double get_baseline_pnl() const noexcept {
            return m_baseline_pnl;
        }

        // ─────────────────────────────────────────────────────────────────────
        // Run Stress Tests
        // ─────────────────────────────────────────────────────────────────────

        [[nodiscard]] StressResult run_scenario(
            const StressScenario& scenario,
            const std::vector<Market::Index_tranche>& tranches,
            const std::vector<Market::TranchesGrid>& grids,
            const std::vector<double>& rho1_vector,
            const std::vector<double>& rho2_vector,
            const std::vector<double>& base_pnl_vector
        ) const;

        [[nodiscard]] std::vector<StressResult> run_all_scenarios(
            const std::vector<Market::Index_tranche>& tranches,
            const std::vector<Market::TranchesGrid>& grids,
            const std::vector<double>& rho1_vector,
            const std::vector<double>& rho2_vector,
            const std::vector<double>& base_pnl_vector
        ) const;

        // ─────────────────────────────────────────────────────────────────────
        // Extreme Scenario Analysis
        // ─────────────────────────────────────────────────────────────────────

        struct ExtremeScenarioResult {
            std::vector<StressResult> worst_case_scenarios;
            double largest_loss {};
            double var_of_scenarios_95 {};
            int n_scenarios_profitable {};
            double expected_scenario_loss {};
        };

        [[nodiscard]] ExtremeScenarioResult analyze_extreme_scenarios(
            const std::vector<StressResult>& results
        ) const;

        // ─────────────────────────────────────────────────────────────────────
        // Aggregate Risk Metrics
        // ─────────────────────────────────────────────────────────────────────

        struct ScenarioStatistics {
            double mean_loss {};
            double std_dev_loss {};
            double worst_loss {};
            double best_gain {};
            double loss_probability {};
            int n_breaching_scenarios {};
        };

        [[nodiscard]] ScenarioStatistics compute_scenario_statistics(
            const std::vector<StressResult>& results,
            double probability_threshold = 0.05
        ) const;

        // ─────────────────────────────────────────────────────────────────────
        // Hedging Analysis
        // ─────────────────────────────────────────────────────────────────────

        struct CorrelationHedgeAnalysis {
            double optimal_hedge_ratio {};      // % of position to hedge
            double residual_correlation_risk {}; // Risk after optimal hedge
            std::string recommended_instrument;  // Which instrument to hedge with
            double hedge_cost {};                // Cost of the hedge
        };

        [[nodiscard]] CorrelationHedgeAnalysis compute_optimal_correlation_hedge(
            const std::vector<StressResult>& correlation_scenarios
        ) const;

    private:
        const Pricer::BaseTranchePricer& m_pricer;
        std::vector<StressScenario> m_scenarios;
        std::map<std::string, ScenarioApplier> m_custom_appliers;
        double m_baseline_pnl;

        // ─────────────────────────────────────────────────────────────────────
        // Standard Scenarios Registration
        // ─────────────────────────────────────────────────────────────────────

        void register_standard_scenarios();

        // ─────────────────────────────────────────────────────────────────────
        // Internal Scenario Application
        // ─────────────────────────────────────────────────────────────────────

        void apply_stress_to_tranches(
            std::vector<Market::Index_tranche>& tranches,
            const StressScenario& scenario
        ) const;

        [[nodiscard]] double compute_stressed_portfolio_pnl(
            const std::vector<Market::Index_tranche>& tranches,
            const std::vector<Market::TranchesGrid>& grids,
            const std::vector<double>& rho1_vector,
            const std::vector<double>& rho2_vector,
            const std::vector<double>& base_pnl_vector
        ) const;
    };

}  // namespace Risk

