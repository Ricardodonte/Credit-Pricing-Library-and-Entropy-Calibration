//
// PortfolioRiskAnalyzer.h - Aggregate portfolio risk management
// Consolidated reporting and risk aggregation across tranches
//

#pragma once

#include <vector>
#include <string>
#include <chrono>
#include <memory>
#include <algorithm>
#include <numeric>

#include "RiskTypes.h"
#include "RiskCalculator.h"
#include "StressTestEngine.h"
#include "Pricers/base_tranche_pricer.h"
#include "Market/tranches_instruments.h"

namespace Risk {

    class PortfolioRiskAnalyzer {
    public:
        // ─────────────────────────────────────────────────────────────────────
        // Construction
        // ─────────────────────────────────────────────────────────────────────

        PortfolioRiskAnalyzer(
            const std::string& portfolio_id,
            const Pricer::BaseTranchePricer& pricer
        ) : m_portfolio_id(portfolio_id),
            m_risk_calc(pricer),
            m_stress_engine(pricer),
            m_pricer(pricer) {}

        // ─────────────────────────────────────────────────────────────────────
        // Portfolio Setup
        // ─────────────────────────────────────────────────────────────────────

        void add_tranche(
            const Market::Index_tranche& tranche,
            const Market::TranchesGrid& grid,
            double rho1,
            double rho2,
            double nominal
        );

        void set_risk_limits(const RiskLimits& limits) noexcept {
            m_risk_limits = limits;
        }

        [[nodiscard]] size_t num_tranches() const noexcept {
            return m_tranches.size();
        }

        // ─────────────────────────────────────────────────────────────────────
        // Risk Analysis
        // ─────────────────────────────────────────────────────────────────────

        [[nodiscard]] RiskSnapshot compute_full_risk_report();

        [[nodiscard]] PortfolioGreeks get_portfolio_greeks() const noexcept {
            return m_portfolio_greeks;
        }

        [[nodiscard]] LossMetrics get_loss_metrics() const noexcept {
            return m_loss_metrics;
        }

        // ─────────────────────────────────────────────────────────────────────
        // Risk Decomposition
        // ─────────────────────────────────────────────────────────────────────

        struct TrancheContribution {
            size_t tranche_index;
            double delta_contribution {};
            double gamma_contribution {};
            double vega_contribution {};
            double rho_contribution {};
            double nominal_weight {};
            std::string description;
        };

        [[nodiscard]] std::vector<TrancheContribution> decompose_portfolio_risk() const;

        // ─────────────────────────────────────────────────────────────────────
        // Historical Backtesting
        // ─────────────────────────────────────────────────────────────────────

        struct VaRBacktest {
            int n_observations {};
            int n_var_breaches {};
            double breach_rate {};       // % of times actual loss > VaR
            double kupiec_statistic {};  // Traffic light approach
            bool is_adequate {true};     // Green/Yellow/Red

            enum class TrafficLight { Green, Yellow, Red };
            [[nodiscard]] TrafficLight get_traffic_light() const noexcept {
                if (kupiec_statistic < 0.01) return TrafficLight::Green;
                if (kupiec_statistic < 0.05) return TrafficLight::Yellow;
                return TrafficLight::Red;
            }
        };

        void add_historical_pnl(double pnl);

        [[nodiscard]] VaRBacktest backtest_var_95() const;

        // ─────────────────────────────────────────────────────────────────────
        // Hedging Recommendation
        // ─────────────────────────────────────────────────────────────────────

        struct HedgeRecommendation {
            bool hedge_needed {false};
            std::string hedge_type;       // "correlation", "duration", "rating"
            double recommended_hedge_ratio {};
            double expected_cost {};
            double residual_risk_reduction {}; // % reduction in VaR
            std::string rationale;
        };

        [[nodiscard]] HedgeRecommendation recommend_hedges();

        // ─────────────────────────────────────────────────────────────────────
        // Reporting
        // ─────────────────────────────────────────────────────────────────────

        [[nodiscard]] std::string generate_report_text() const;

        struct ReportOptions {
            bool include_Greeks {true};
            bool include_stress_results {true};
            bool include_decomposition {true};
            bool include_recommendations {true};
            bool detailed_mode {false};
        };

        [[nodiscard]] std::string generate_detailed_report(const ReportOptions& opts) const;

    private:
        // ─────────────────────────────────────────────────────────────────────
        // Data Members
        // ─────────────────────────────────────────────────────────────────────

        std::string m_portfolio_id;
        RiskCalculator m_risk_calc;
        StressTestEngine m_stress_engine;
        const Pricer::BaseTranchePricer& m_pricer;

        std::vector<Market::Index_tranche> m_tranches;
        std::vector<Market::TranchesGrid> m_grids;
        std::vector<double> m_rho1_vector;
        std::vector<double> m_rho2_vector;
        std::vector<double> m_nominals;
        std::vector<double> m_base_pnl;

        PortfolioGreeks m_portfolio_greeks;
        LossMetrics m_loss_metrics;
        RiskLimits m_risk_limits;
        std::vector<double> m_historical_pnl;
        std::vector<TrancheGreeks> m_tranche_greeks;

        // ─────────────────────────────────────────────────────────────────────
        // Helper Methods
        // ─────────────────────────────────────────────────────────────────────

        [[nodiscard]] bool check_risk_limit_breaches() const;

        [[nodiscard]] double compute_portfolio_recovery_rate() const noexcept {
            if (m_tranches.empty()) return 0.0;
            double total_nominal = std::accumulate(m_nominals.begin(), m_nominals.end(), 0.0);
            if (total_nominal < 1e-12) return 0.0;
            return 0.40; // Simplified: assumes uniform recovery
        }

        [[nodiscard]] double compute_portfolio_correlation() const noexcept {
            if (m_rho1_vector.empty()) return 0.0;
            return std::accumulate(m_rho1_vector.begin(), m_rho1_vector.end(), 0.0)
                   / m_rho1_vector.size();
        }
    };

}  // namespace Risk

