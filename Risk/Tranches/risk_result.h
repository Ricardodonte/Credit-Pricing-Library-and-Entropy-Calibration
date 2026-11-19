//
// Created by ricar on 08/05/2026.
//

#pragma once
#include <vector>
#include <string>

namespace Risk {

struct TranchesGreeksResult {

    double cs01{};            // parallel credit spread DV01 (all names +1bp)
    double dv01{};            // parallel interest rate DV01 (flat yield +1bp)
    double rho01{};           // correlation sensitivity (rho +0.01)

    std::vector<double> bucketed_cs01;

    double gamma{};
    double theta{};

    double par_spread{};

    double K1{};
    double K2{};
    std::string pricer_name;
};


struct TranchesBookGreeksResult {
    std::vector<TranchesGreeksResult> tranches;
    [[nodiscard]] double total_cs01()  const;
    [[nodiscard]] double total_dv01()  const;
    [[nodiscard]] double total_rho01() const;
    [[nodiscard]] double total_gamma() const;
    [[nodiscard]] double total_theta() const;
};

inline double TranchesBookGreeksResult::total_cs01() const {
    double s = 0; for (const auto& g : tranches) s += g.cs01;  return s;
}
inline double TranchesBookGreeksResult::total_dv01() const {
    double s = 0; for (const auto& g : tranches) s += g.dv01;  return s;
}
inline double TranchesBookGreeksResult::total_rho01() const {
    double s = 0; for (const auto& g : tranches) s += g.rho01; return s;
}
inline double TranchesBookGreeksResult::total_gamma() const {
    double s = 0; for (const auto& g : tranches) s += g.gamma; return s;
}
inline double TranchesBookGreeksResult::total_theta() const {
    double s = 0; for (const auto& g : tranches) s += g.theta; return s;
}


struct StressPoint {
    double spread_bump_bp{};   // credit spread shift applied (bp)
    double rho_bump{};         // correlation shift applied
    double npv{};              // NPV after shock
    double pnl{};              // P&L vs base (npv - base_npv)
    double par_spread{};       // par spread after shock
};

struct StressResult {
    double base_npv{};
    double base_par_spread{};
    double K1{}, K2{};
    std::string pricer_name;
    std::vector<StressPoint> points;   // one per scenario
};


struct ModelComparePoint {
    double K1{}, K2{};
    double spread_A{};         // par spread from pricer A (bp)
    double spread_B{};         // par spread from pricer B (bp)
    double diff_bp{};          // spread_A - spread_B
    double npv_A{};
    double npv_B{};
    double npv_diff{};
};

struct ModelCompareResult {
    std::string pricer_A_name;
    std::string pricer_B_name;
    std::vector<ModelComparePoint> tranches;

    [[nodiscard]] double total_spread_distance_bp() const;
    // Max absolute difference
    [[nodiscard]] double max_spread_diff_bp() const;
};

inline double ModelCompareResult::total_spread_distance_bp() const {
    double s = 0;
    for (const auto& p : tranches) s += std::abs(p.diff_bp);
    return s;
}
inline double ModelCompareResult::max_spread_diff_bp() const {
    double m = 0;
    for (const auto& p : tranches) m = std::max(m, std::abs(p.diff_bp));
    return m;
}

}
