//
// PortfolioRiskAnalyzer.cpp - Portfolio risk aggregation and reporting
//

#include "Risk/PortfolioRiskAnalyzer.h"
#include <iomanip>
#include <sstream>
#include <cmath>
#include <numeric>

namespace Risk {

    // ─────────────────────────────────────────────────────────────────────────
    // Portfolio Setup
    // ─────────────────────────────────────────────────────────────────────────

    void PortfolioRiskAnalyzer::add_tranche(
        const Market::Index_tranche& tranche,
        const Market::TranchesGrid& grid,
        double rho1,
        double rho2,
        double nominal
    ) {
        m_tranches.push_back(tranche);
        m_grids.push_back(grid);
        m_rho1_vector.push_back(rho1);
        m_rho2_vector.push_back(rho2);
        m_nominals.push_back(nominal);

        double base_pnl = m_pricer.npv(tranche, grid, rho1, rho2);
        m_base_pnl.push_back(base_pnl);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Full Risk Report Computation
    // ─────────────────────────────────────────────────────────────────────────

    RiskSnapshot PortfolioRiskAnalyzer::compute_full_risk_report() {

        RiskSnapshot snapshot;
        snapshot.portfolio_id = m_portfolio_id;

        // Generate timestamp
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::ostringstream oss;
        oss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
        snapshot.timestamp = oss.str();

        // ─── Compute Portfolio Greeks ───
        m_portfolio_greeks = m_risk_calc.compute_portfolio_greeks(
            m_tranches, m_grids,
            m_rho1_vector, m_rho2_vector,
            m_nominals
        );
        snapshot.greeks = m_portfolio_greeks;

        // ─── Estimate Loss Metrics ───
        // Simplified: use single loss sample (in production: Monte Carlo)
        std::vector<double> loss_samples;
        for (const auto& pnl : m_base_pnl) {
            loss_samples.push_back(std::max(0.0, -pnl)); // Losses only
        }
        m_loss_metrics = m_risk_calc.estimate_loss_metrics(
            loss_samples,
            compute_portfolio_recovery_rate(),
            compute_portfolio_correlation()
        );
        snapshot.loss_metrics = m_loss_metrics;

        // ─── Compute Tranche-level Greeks ───
        m_tranche_greeks.clear();
        for (size_t i = 0; i < m_tranches.size(); ++i) {
            auto tg = m_risk_calc.compute_tranche_greeks(
                m_tranches[i], m_grids[i],
                m_rho1_vector[i], m_rho2_vector[i],
                m_base_pnl[i]
            );
            m_tranche_greeks.push_back(tg);
        }

        // ─── Run Stress Tests ───
        m_stress_engine.set_baseline_pnl(
            std::accumulate(m_base_pnl.begin(), m_base_pnl.end(), 0.0)
        );
        auto stress_results = m_stress_engine.run_all_scenarios(
            m_tranches, m_grids,
            m_rho1_vector, m_rho2_vector,
            m_base_pnl
        );
        snapshot.stress_results = stress_results;

        // ─── Check Risk Limits ───
        snapshot.all_limits_respected = !check_risk_limit_breaches();

        // ─── Generate Warning Messages ───
        if (m_portfolio_greeks.total_vega > m_risk_limits.max_correlation_vega) {
            snapshot.warning_message = "WARNING: Correlation vega exceeds limit";
        }
        if (m_portfolio_greeks.dv01 > m_risk_limits.max_dv01) {
            snapshot.warning_message = "WARNING: DV01 exceeds limit";
        }

        return snapshot;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Risk Decomposition
    // ─────────────────────────────────────────────────────────────────────────

    std::vector<PortfolioRiskAnalyzer::TrancheContribution> PortfolioRiskAnalyzer::decompose_portfolio_risk() const {

        std::vector<TrancheContribution> contributions;

        double total_nominal = std::accumulate(m_nominals.begin(), m_nominals.end(), 0.0);
        if (total_nominal < 1e-12) {
            return contributions;
        }

        for (size_t i = 0; i < m_tranches.size(); ++i) {
            TrancheContribution contrib;
            contrib.tranche_index = i;
            contrib.nominal_weight = m_nominals[i] / total_nominal * 100.0;

            if (i < m_tranche_greeks.size()) {
                const auto& tg = m_tranche_greeks[i];
                contrib.delta_contribution = tg.delta_spread * m_nominals[i];
                contrib.gamma_contribution = tg.gamma_spread * m_nominals[i];
                contrib.vega_contribution = tg.vega_correlation * m_nominals[i];
                contrib.rho_contribution = tg.rho_spread * m_nominals[i];

                std::ostringstream oss;
                oss << "[" << std::fixed << std::setprecision(1)
                    << m_tranches[i].K1 * 100.0 << "%-"
                    << m_tranches[i].K2 * 100.0 << "%]";
                contrib.description = oss.str();
            }

            contributions.push_back(contrib);
        }

        return contributions;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // VaR Backtesting
    // ─────────────────────────────────────────────────────────────────────────

    void PortfolioRiskAnalyzer::add_historical_pnl(double pnl) {
        m_historical_pnl.push_back(pnl);
    }

    PortfolioRiskAnalyzer::VaRBacktest PortfolioRiskAnalyzer::backtest_var_95() const {

        VaRBacktest backtest;

        if (m_historical_pnl.empty()) {
            return backtest;
        }

        backtest.n_observations = static_cast<int>(m_historical_pnl.size());

        // Sort to compute VaR 95%
        auto sorted = m_historical_pnl;
        std::sort(sorted.begin(), sorted.end());

        size_t idx_95 = static_cast<size_t>(sorted.size() * 0.95);
        double var_95 = sorted[std::min(idx_95, sorted.size() - 1)];

        // Count breaches (actual loss > VaR)
        backtest.n_var_breaches = std::count_if(
            m_historical_pnl.begin(), m_historical_pnl.end(),
            [var_95](double p) { return p < var_95; }
        );

        backtest.breach_rate = static_cast<double>(backtest.n_var_breaches) / backtest.n_observations;

        // Kupiec POF (Proportion of Failures) test
        double expected_failures = 0.05 * backtest.n_observations;
        double failures = backtest.n_var_breaches;
        backtest.kupiec_statistic = 2.0 * (
            failures * std::log(failures / expected_failures) +
            (backtest.n_observations - failures) * std::log((backtest.n_observations - failures) / (0.95 * backtest.n_observations))
        );

        return backtest;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Hedging Recommendations
    // ─────────────────────────────────────────────────────────────────────────

    PortfolioRiskAnalyzer::HedgeRecommendation PortfolioRiskAnalyzer::recommend_hedges() {

        HedgeRecommendation rec;

        // Check if correlation risk is too high
        if (m_portfolio_greeks.total_vega > m_risk_limits.max_correlation_vega * 0.8) {
            rec.hedge_needed = true;
            rec.hedge_type = "correlation";
            rec.recommended_hedge_ratio = 0.40; // Hedge 40% of vega exposure
            rec.expected_cost = m_portfolio_greeks.total_vega * 0.02; // 2% cost estimate
            rec.residual_risk_reduction = 40.0; // 40% reduction in correlation risk
            rec.rationale = "High correlation vega exposure. Consider short correlation via iTraxx swaps.";
        }

        // Check if duration is too high
        else if (m_portfolio_greeks.total_rho > m_risk_limits.max_duration * 100.0) {
            rec.hedge_needed = true;
            rec.hedge_type = "duration";
            rec.recommended_hedge_ratio = 0.30;
            rec.expected_cost = m_portfolio_greeks.total_rho * 0.001;
            rec.residual_risk_reduction = 30.0;
            rec.rationale = "High interest rate duration. Consider longer-dated hedges.";
        }

        // Check if DV01 too large
        else if (m_portfolio_greeks.dv01 > m_risk_limits.max_dv01 * 0.9) {
            rec.hedge_needed = true;
            rec.hedge_type = "spread";
            rec.recommended_hedge_ratio = 0.25;
            rec.expected_cost = m_portfolio_greeks.dv01 * 0.001;
            rec.residual_risk_reduction = 25.0;
            rec.rationale = "DV01 approaching limits. Consider partial hedging via forwards.";
        }

        return rec;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Text Reporting
    // ─────────────────────────────────────────────────────────────────────────

    std::string PortfolioRiskAnalyzer::generate_report_text() const {

        std::ostringstream report;

        report << "\n" << std::string(80, '=') << "\n";
        report << "  PORTFOLIO RISK REPORT\n";
        report << "  Portfolio ID: " << m_portfolio_id << "\n";
        report << std::string(80, '=') << "\n\n";

        // Greeks Summary
        report << "  GREEKS SUMMARY\n";
        report << "  " << std::string(76, '-') << "\n";
        report << "  Total Delta (spread)      : " << std::setw(15) << std::fixed << std::setprecision(2)
               << m_portfolio_greeks.total_delta << " € per bp\n";
        report << "  Primary Gamma (spread)    : " << std::setw(15) << std::setprecision(4)
               << m_portfolio_greeks.total_gamma << " € per bp²\n";
        report << "  Correlation Vega          : " << std::setw(15) << std::setprecision(2)
               << m_portfolio_greeks.total_vega << " € per 0.01 move\n";
        report << "  Interest Rate Rho         : " << std::setw(15) << std::setprecision(2)
               << m_portfolio_greeks.total_rho << " € per 1% rate move\n";
        report << "  DV01                      : " << std::setw(15) << std::setprecision(2)
               << m_portfolio_greeks.dv01 << " €\n";

        // Loss Metrics
        report << "\n  LOSS DISTRIBUTION METRICS\n";
        report << "  " << std::string(76, '-') << "\n";
        report << "  Expected Loss             : " << std::setw(15) << std::setprecision(2)
               << m_loss_metrics.expected_loss << " €\n";
        report << "  Value at Risk (95%)       : " << std::setw(15) << std::setprecision(2)
               << m_loss_metrics.var_95 << " €\n";
        report << "  Conditional VaR (95%)     : " << std::setw(15) << std::setprecision(2)
               << m_loss_metrics.cvar_95 << " €\n";
        report << "  Potential Loss (worst)    : " << std::setw(15) << std::setprecision(2)
               << m_loss_metrics.potential_loss << " €\n";

        report << "\n" << std::string(80, '=') << "\n";

        return report.str();
    }

    std::string PortfolioRiskAnalyzer::generate_detailed_report(const ReportOptions& opts) const {

        std::ostringstream report;
        report << generate_report_text();

        if (opts.include_decomposition) {
            report << "\n  RISK DECOMPOSITION BY TRANCHE\n";
            report << "  " << std::string(76, '-') << "\n";

            auto decomp = decompose_portfolio_risk();
            for (const auto& contrib : decomp) {
                report << "  " << contrib.description
                       << " Weight: " << std::setw(6) << std::fixed << std::setprecision(2)
                       << contrib.nominal_weight << "%"
                       << " Delta: " << std::setw(12) << std::setprecision(2)
                       << contrib.delta_contribution << " €\n";
            }
        }

        if (opts.include_recommendations) {
            auto hedge_rec = recommend_hedges();
            if (hedge_rec.hedge_needed) {
                report << "\n  HEDGING RECOMMENDATIONS\n";
                report << "  " << std::string(76, '-') << "\n";
                report << "  Type: " << hedge_rec.hedge_type << "\n";
                report << "  Recommended Ratio: " << std::setw(6) << std::fixed << std::setprecision(1)
                       << hedge_rec.recommended_hedge_ratio * 100.0 << "%\n";
                report << "  Expected Cost: " << std::setw(15) << std::setprecision(2)
                       << hedge_rec.expected_cost << " €\n";
                report << "  Rationale: " << hedge_rec.rationale << "\n";
            }
        }

        return report.str();
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Helper Methods
    // ─────────────────────────────────────────────────────────────────────────

    bool PortfolioRiskAnalyzer::check_risk_limit_breaches() const {

        if (m_portfolio_greeks.dv01 > m_risk_limits.max_dv01) return true;
        if (m_portfolio_greeks.total_vega > m_risk_limits.max_correlation_vega) return true;
        if (m_loss_metrics.var_95 > m_risk_limits.max_loss_var_95) return true;

        return false;
    }

}  // namespace Risk

