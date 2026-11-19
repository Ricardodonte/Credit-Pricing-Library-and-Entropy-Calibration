//
// KL_pricer.h
// KL-divergence tranche calibration — Sections 7.3–7.5 of the mémoire
//
// Changes vs. previous version
// ─────────────────────────────────────────────────────────────────────────────
//  • idx(i,k,n) = (i−1)·(n+1)+k  with 1-based i  (document convention §7.3)
//  • delta_P sign fix: year_fraction(prev_prem, date)  (was inverted)
//  • KLCalibrator: pure Newton-Raphson, no line-search
//      – Atmu precomputed once per iteration  (O(M·dim) not O(M) per (i,k))
//      – log-sum-exp stabilisation in q*(μ) computation
//      – gradient and Hessian accumulated in a single pass over i
//      – dual_value called exactly once, at convergence / end
//      – working buffers allocated once, outside the loop
//  • Guards: cerr + throw on Zi underflow, singular Hessian, max-iter breach
//  • PriorBuilder: cerr guard when normalisation sum deviates from 1
// ─────────────────────────────────────────────────────────────────────────────

#pragma once

#include <vector>
#include <set>
#include <stdexcept>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <cassert>
#include <functional>
#include <iostream>
#include <iomanip>
#include <limits>
#include <string>

#include "Core/numericals.h"
#include "Core/types.h"
#include "Market/Curves/CDS/CreditCurve.h"
#include "Market/Curves/YieldCurve/YieldCurve.h"
#include "Market/Instruments/Tranches/tranches_instruments.h"

namespace KL {

// ─────────────────────────────────────────────────────────────────────────────
// Indexing convention  (§7.3)
// idx(i, k, n) = (i − 1)·(n+1) + k,   i = 1..N  (1-based),  k = 0..n
// ─────────────────────────────────────────────────────────────────────────────
[[nodiscard]] inline int idx(const int i, const int k, const int n_credit) noexcept
{
    return (i - 1) * (n_credit + 1) + k;
}

// ─────────────────────────────────────────────────────────────────────────────
// TimeGrid — merged grid  T = T^P ∪ T^D
// ─────────────────────────────────────────────────────────────────────────────
struct GridNode {
    double t       = 0.0;
    double df      = 1.0;
    bool   is_prem = false;
    double delta_P = 0.0;   // ACT/360 accrual from previous premium date; 0 if !is_prem
};

struct TimeGrid {
    std::vector<GridNode> nodes;
    int    N  = 0;   // |T|
    int    NP = 0;   // |T^P|
    int    ND = 0;   // |T^D| = N  (T^P ⊆ T^D, monthly default grid)

    // Σ_{i: is_prem} P(0,t_i)·δ^P(t_i)  — scalar used for b_m
    double sum_prem_df_delta = 0.0;
};

// ─────────────────────────────────────────────────────────────────────────────
// ISDA IMM date helpers
// ─────────────────────────────────────────────────────────────────────────────

// First IMM date (Mar/Jun/Sep/Dec 20) on or after ref.
inline Core::Date imm_on_or_after(const Core::Date& ref)
{
    auto [y, m, d] = ref.toGregorian();
    int imm_month, imm_year = y;
    if      (m <  3 || (m ==  3 && d <= 20)) imm_month =  3;
    else if (m <  6 || (m ==  6 && d <= 20)) imm_month =  6;
    else if (m <  9 || (m ==  9 && d <= 20)) imm_month =  9;
    else if (m < 12 || (m == 12 && d <= 20)) imm_month = 12;
    else { imm_month = 3; ++imm_year; }
    return {imm_year, imm_month, 20};
}

// Advance an IMM date by n_quarters quarters.
inline Core::Date imm_advance(const Core::Date& d, const int n_quarters)
{
    auto [y, m, day] = d.toGregorian();
    int new_month = m + n_quarters * 3;
    int new_year  = y + (new_month - 1) / 12;
    new_month     = (new_month - 1) % 12 + 1;
    return {new_year, new_month, 20};
}

// Advance a date by one calendar month, keeping the 20th.
inline Core::Date month20_advance(const Core::Date& d)
{
    auto [y, m, day] = d.toGregorian();
    int nm = m + 1, ny = y;
    if (nm > 12) { nm = 1; ++ny; }
    return {ny, nm, 20};
}

// ─────────────────────────────────────────────────────────────────────────────
// build_kl_time_grid  — ISDA-compliant merged grid
// ─────────────────────────────────────────────────────────────────────────────
inline TimeGrid build_kl_time_grid(
    const Market::Index_tranche& tranche,
    const Market::YieldCurve&    yc)
{
    double maturity = Core::year_fraction(tranche.valuation_date , tranche.MaturityDate , tranche.day_count);
    const Core::Date& val_date  = tranche.valuation_date;
    const int         n_qtr     = static_cast<int>(std::round(maturity * 4.0));
    const Core::Date  first_imm = imm_on_or_after(val_date);
    const Core::Date  mat_date  = imm_advance(first_imm, n_qtr);

    // ── Premium schedule: quarterly IMM dates up to maturity ─────────────────
    std::vector<Core::Date> prem_dates;
    {
        Core::Date cur = first_imm;
        while (cur <= mat_date) { prem_dates.push_back(cur); cur = imm_advance(cur, 1); }
        if (prem_dates.empty() || prem_dates.back() != mat_date)
            prem_dates.push_back(mat_date);
    }
    if (prem_dates.empty())
        throw std::runtime_error("build_kl_time_grid: empty premium schedule");

    // ── Default schedule: every 20th of each month up to maturity ────────────
    std::vector<Core::Date> def_dates;
    {
        auto [vy, vm, vd] = val_date.toGregorian();
        Core::Date cur(vy, vm, 20);
        if (cur <= val_date) cur = month20_advance(cur);
        while (cur <= mat_date) { def_dates.push_back(cur); cur = month20_advance(cur); }
        if (def_dates.empty() || def_dates.back() != mat_date)
            def_dates.push_back(mat_date);
    }

    // ── Merge T^P ∪ T^D, sorted, no duplicates ───────────────────────────────
    std::vector<Core::Date> merged;
    merged.reserve(prem_dates.size() + def_dates.size());
    {
        std::size_t pi = 0, di = 0;
        while (pi < prem_dates.size() && di < def_dates.size()) {
            if      (prem_dates[pi] < def_dates[di])  merged.push_back(prem_dates[pi++]);
            else if (def_dates[di]  < prem_dates[pi]) merged.push_back(def_dates[di++]);
            else  { merged.push_back(def_dates[di++]); ++pi; }
        }
        while (pi < prem_dates.size()) merged.push_back(prem_dates[pi++]);
        while (di < def_dates.size())  merged.push_back(def_dates[di++]);
    }

    // ── Build GridNode vector ─────────────────────────────────────────────────
    std::set<long long> prem_jdn_set;
    for (const auto& d : prem_dates)
        prem_jdn_set.insert(d.getJulianDays());

    TimeGrid grid;
    grid.nodes.reserve(merged.size());
    grid.NP = static_cast<int>(prem_dates.size());

    Core::Date prev_prem = val_date;   // accrual origin

    for (const auto& date : merged) {
        GridNode nd;
        nd.t      = Core::year_fraction(val_date, date, Core::DayCount::ACT_360);
        nd.df     = yc.discount(nd.t);
        nd.is_prem = prem_jdn_set.contains(date.getJulianDays());

        if (nd.is_prem) {
            // FIX: was year_fraction(date, prev_prem, …) — arguments were swapped,
            //      producing negative accrual fractions.
            nd.delta_P = Core::year_fraction(prev_prem, date, Core::DayCount::ACT_360);
            prev_prem  = date;
        }
        grid.nodes.push_back(nd);
    }

    grid.N  = static_cast<int>(grid.nodes.size());
    grid.ND = grid.N;

    grid.sum_prem_df_delta = 0.0;
    for (const auto& nd : grid.nodes)
        if (nd.is_prem) grid.sum_prem_df_delta += nd.df * nd.delta_P;

    return grid;
}

// ─────────────────────────────────────────────────────────────────────────────
// PriorBuilder  (Hull-White full recursion + Gauss-Hermite quadrature)
// Returns prior[i][k] = P[L(t_i) = k·(1−R)/n],  i=0..N-1, k=0..n
// ─────────────────────────────────────────────────────────────────────────────
class PriorBuilder {
public:
    PriorBuilder(
        const std::vector<Market::CreditCurve>& credit_curves,
        const int                               n_credit,
        const double                            recovery_rate = 0.40)
        : m_curves(credit_curves), m_n(n_credit), m_R(recovery_rate)
    {
        if (n_credit <= 0)
            throw std::invalid_argument("PriorBuilder: n_credit must be > 0");
        if (m_curves.size() != static_cast<std::size_t>(n_credit))
            throw std::invalid_argument("PriorBuilder: credit_curves.size() != n_credit");
    }

    [[nodiscard]] std::vector<std::vector<double>>
    build(const TimeGrid& grid, const double rho) const
    {
        const int N = grid.N;
        const int n = m_n;

        std::vector<std::vector<double>> prior(
            static_cast<std::size_t>(N),
            std::vector<double>(static_cast<std::size_t>(n + 1), 0.0));

        // C[j][i] = Φ^{-1}(PD_j(t_i))
        std::vector<std::vector<double>> C(
            static_cast<std::size_t>(n),
            std::vector<double>(static_cast<std::size_t>(N)));

        for (int j = 0; j < n; ++j)
            for (int i = 0; i < N; ++i) {
                const double pd = 1.0 - m_curves[static_cast<std::size_t>(j)]
                                            .survival_probability(grid.nodes[static_cast<std::size_t>(i)].t);
                if      (pd <= 1e-12)        C[static_cast<std::size_t>(j)][static_cast<std::size_t>(i)] = -10.0;
                else if (pd >= 1.0 - 1e-12)  C[static_cast<std::size_t>(j)][static_cast<std::size_t>(i)] =  10.0;
                else                          C[static_cast<std::size_t>(j)][static_cast<std::size_t>(i)] = Core::norm_inv(pd);
            }

        const double rho_sqrt    = std::sqrt(rho);
        const double sqrt_1m_rho = std::sqrt(1.0 - rho);

        std::vector<double> f_buf(static_cast<std::size_t>(n + 1));

        for (std::size_t gi = 0; gi < Core::N_GH; ++gi) {
            const double Z = std::sqrt(2.0) * Core::GH_NODES[gi];
            const double w = Core::GH_WEIGHTS[gi] / std::sqrt(MathConstants::PI);

            for (int i = 0; i < N; ++i) {
                std::ranges::fill(f_buf, 0.0);
                f_buf[0] = 1.0;

                for (int j = 1; j <= n; ++j) {
                    const double c_j = C[static_cast<std::size_t>(j - 1)][static_cast<std::size_t>(i)];
                    const double pd  = Core::norm_cdf((c_j - rho_sqrt * Z) / sqrt_1m_rho);
                    const double q   = 1.0 - pd;
                    for (int k = std::min(j, n); k >= 1; --k)
                        f_buf[static_cast<std::size_t>(k)] =
                            f_buf[static_cast<std::size_t>(k)] * q +
                            f_buf[static_cast<std::size_t>(k - 1)] * pd;
                    f_buf[0] *= q;
                }

                for (int k = 0; k <= n; ++k)
                    prior[static_cast<std::size_t>(i)][static_cast<std::size_t>(k)] +=
                        w * f_buf[static_cast<std::size_t>(k)];
            }
        }

        // Normalisation — warn if quadrature error is non-negligible
        for (int i = 0; i < N; ++i) {
            double sum = 0.0;
            for (int k = 0; k <= n; ++k) {
                prior[static_cast<std::size_t>(i)][static_cast<std::size_t>(k)] =
                    std::max(prior[static_cast<std::size_t>(i)][static_cast<std::size_t>(k)], 1e-300);
                sum += prior[static_cast<std::size_t>(i)][static_cast<std::size_t>(k)];
            }
            if (std::abs(sum - 1.0) > 1e-6)
                std::cerr << "[PriorBuilder] Guard: sum of prior[" << i << "] = "
                          << sum << "  (deviation = " << (sum - 1.0) << ")\n";
            const double inv_sum = 1.0 / sum;
            for (int k = 0; k <= n; ++k)
                prior[static_cast<std::size_t>(i)][static_cast<std::size_t>(k)] *= inv_sum;
        }

        return prior;
    }

    // Golden-section search: minimise Σ_m (s_m^model(ρ) − s_m^mkt)²
    static double fit_rho_ols(
        const std::vector<Market::Index_tranche>&    tranches,
        const std::vector<double>&                   mkt_spreads,
        const std::function<double(double, int)>&    par_spread_fn,
        const double rho_lo  = 0.01,
        const double rho_hi  = 0.99,
        const int    max_iter = 100,
        const double tol      = 1e-8)
    {
        const int M = static_cast<int>(tranches.size());
        auto ols = [&](const double rho) {
            double res = 0.0;
            for (int m = 0; m < M; ++m) {
                const double d = par_spread_fn(rho, m)
                               - mkt_spreads[static_cast<std::size_t>(m)];
                res += d * d;
            }
            return res;
        };
        const double phi = (std::sqrt(5.0) - 1.0) * 0.5;
        double a = rho_lo, b = rho_hi;
        double c = b - phi * (b - a);
        double d = a + phi * (b - a);
        for (int it = 0; it < max_iter && (b - a) > tol; ++it) {
            if (ols(c) < ols(d)) b = d; else a = c;
            c = b - phi * (b - a);
            d = a + phi * (b - a);
        }
        return 0.5 * (a + b);
    }

private:
    const std::vector<Market::CreditCurve>& m_curves;
    int    m_n;
    double m_R;
};

// ─────────────────────────────────────────────────────────────────────────────
// ConstraintMatrix  (Proposition 7.4)
// A[m][idx(i,k,n)],  i = 1..N (1-based),  k = 0..n
// ─────────────────────────────────────────────────────────────────────────────
struct ConstraintMatrix {
    int M = 0;
    int N = 0;
    int n = 0;
    std::vector<std::vector<double>> A;   // A[m][j], j = idx(i,k,n)
    std::vector<double>              b;   // b[m]
};

inline ConstraintMatrix build_constraint_matrix(
    const std::vector<Market::Index_tranche>& tranches,
    const TimeGrid&                           grid,
    const int                                 n_credit,
    const double                              recovery_rate)
{
    const int    M   = static_cast<int>(tranches.size());
    const int    N   = grid.N;
    const int    n   = n_credit;
    const int    dim = N * (n + 1);
    const double R   = recovery_rate;

    if (M == 0) throw std::invalid_argument("build_constraint_matrix: no tranches supplied");
    if (N == 0) throw std::invalid_argument("build_constraint_matrix: grid is empty");

    ConstraintMatrix cm;
    cm.M = M; cm.N = N; cm.n = n;
    cm.A.assign(static_cast<std::size_t>(M),
                std::vector<double>(static_cast<std::size_t>(dim), 0.0));
    cm.b.resize(static_cast<std::size_t>(M));

    // x_k = k·(1−R)/n
    std::vector<double> x(static_cast<std::size_t>(n + 1));
    for (int k = 0; k <= n; ++k)
        x[static_cast<std::size_t>(k)] = k * (1.0 - R) / n;

    for (int m = 0; m < M; ++m) {
        const double K1 = tranches[static_cast<std::size_t>(m)].K1;
        const double K2 = tranches[static_cast<std::size_t>(m)].K2;
        const double sm = tranches[static_cast<std::size_t>(m)].fair_spread;
        const double dK = K2 - K1;

        if (dK <= 0.0)
            throw std::invalid_argument(
                "build_constraint_matrix: K2 <= K1 for tranche " + std::to_string(m));

        // l_k^m = ((x_k − K1)^+ − (x_k − K2)^+) / (K2 − K1)
        std::vector<double> lkm(static_cast<std::size_t>(n + 1));
        for (int k = 0; k <= n; ++k)
            lkm[static_cast<std::size_t>(k)] =
                (std::max(x[static_cast<std::size_t>(k)] - K1, 0.0)
               - std::max(x[static_cast<std::size_t>(k)] - K2, 0.0)) / dK;

        // b_m = s_m · Σ_{i: is_prem} P(0,t_i)·δ^P(t_i)
        cm.b[static_cast<std::size_t>(m)] = sm * grid.sum_prem_df_delta;

        // A_{m, idx(i,k)} = l_k^m · [ df_i·(1 + s_m·δ^P(t_i)) − df_{i+1} ]
        // with df_{N+1} ≡ 0  (last node, both cases in Prop. 7.4 collapse to this)
        for (int i = 1; i <= N; ++i) {
            const auto&  nd      = grid.nodes[static_cast<std::size_t>(i - 1)];
            const double df_i    = nd.df;
            const double dP      = nd.is_prem ? nd.delta_P : 0.0;
            const double df_next = (i < N) ? grid.nodes[static_cast<std::size_t>(i)].df : 0.0;

            for (int k = 0; k <= n; ++k)
                cm.A[static_cast<std::size_t>(m)][static_cast<std::size_t>(idx(i, k, n))] =
                    lkm[static_cast<std::size_t>(k)] * (df_i * (1.0 + sm * dP) - df_next);
        }
    }
    return cm;
}

// ─────────────────────────────────────────────────────────────────────────────
// KLResult
// ─────────────────────────────────────────────────────────────────────────────
struct KLResult {
    std::vector<double>              mu_star;
    std::vector<std::vector<double>> q_star;
    std::vector<double>              grad_final;
    double                           dual_value{};
    int                              iterations{};
    bool                             converged{};
};

// ─────────────────────────────────────────────────────────────────────────────
// KLCalibrator  — dual Newton-Raphson  (Propositions 7.7–7.10 + §7.5)
//
// Maximises  g(μ) = −μᵀb − Σ_i w_i log Z_i(μ)
// via  μ ← μ + H^{-1}∇g  where H = ∇²g = −Σ_i (1/w_i) Cov_{q*(t_i)}[A_m, A_l]
// ─────────────────────────────────────────────────────────────────────────────
class KLCalibrator {
public:
    explicit KLCalibrator(
        const double tol      = 1e-10,
        const int    max_iter = 200,
        const bool   verbose  = false)
        : m_tol(tol), m_max_iter(max_iter), m_verbose(verbose) {}

    [[nodiscard]] KLResult calibrate(
        const ConstraintMatrix&                  cm,
        const std::vector<std::vector<double>>&  prior,
        const std::vector<double>&               weights,
        const std::vector<double>&               mu0 = {}) const
    {
        const int M   = cm.M;
        const int N   = cm.N;
        const int n   = cm.n;
        const int dim = N * (n + 1);

        // Input guards
        if (static_cast<int>(prior.size()) != N)
            throw std::invalid_argument("KLCalibrator: prior.size() != N");
        if (static_cast<int>(weights.size()) != N)
            throw std::invalid_argument("KLCalibrator: weights.size() != N");
        for (int i = 0; i < N; ++i)
            if (static_cast<int>(prior[static_cast<std::size_t>(i)].size()) != n + 1)
                throw std::invalid_argument("KLCalibrator: prior[i].size() != n+1");

        // Initialise μ
        std::vector<double> mu(static_cast<std::size_t>(M), 0.0);
        if (!mu0.empty()) {
            if (static_cast<int>(mu0.size()) != M)
                throw std::invalid_argument("KLCalibrator: mu0.size() != M");
            mu = mu0;
        }

        // Working buffers — allocated once, reused each iteration
        std::vector<std::vector<double>> q(
            static_cast<std::size_t>(N),
            std::vector<double>(static_cast<std::size_t>(n + 1)));
        std::vector<double> Atmu(static_cast<std::size_t>(dim));
        std::vector<double> grad(static_cast<std::size_t>(M));
        std::vector<std::vector<double>> H(
            static_cast<std::size_t>(M),
            std::vector<double>(static_cast<std::size_t>(M)));
        std::vector<double> ebar(static_cast<std::size_t>(M));

        KLResult res;
        res.converged  = false;
        res.iterations = 0;

        for (int iter = 0; iter < m_max_iter; ++iter) {

            // ── 1. Precompute (Aᵀμ)[j] = Σ_m A[m][j]·μ[m] ──────────────────
            std::fill(Atmu.begin(), Atmu.end(), 0.0);
            for (int m = 0; m < M; ++m)
                for (int j = 0; j < dim; ++j)
                    Atmu[static_cast<std::size_t>(j)] +=
                        cm.A[static_cast<std::size_t>(m)][static_cast<std::size_t>(j)]
                        * mu[static_cast<std::size_t>(m)];

            // ── 2. q*(μ) via exponential tilting with log-sum-exp ────────────
            for (int i = 1; i <= N; ++i) {
                const double wi = weights[static_cast<std::size_t>(i - 1)];

                // Shift by max_k(−Atmu[idx]/wi) to prevent exp overflow
                double max_arg = std::numeric_limits<double>::lowest();
                for (int k = 0; k <= n; ++k)
                    max_arg = std::max(max_arg,
                        -Atmu[static_cast<std::size_t>(idx(i, k, n))] / wi);

                double Zi = 0.0;
                for (int k = 0; k <= n; ++k) {
                    const double val =
                        prior[static_cast<std::size_t>(i - 1)][static_cast<std::size_t>(k)]
                        * std::exp(-Atmu[static_cast<std::size_t>(idx(i, k, n))] / wi - max_arg);
                    q[static_cast<std::size_t>(i - 1)][static_cast<std::size_t>(k)] = val;
                    Zi += val;
                }

                if (Zi < 1e-300) {
                    std::cerr << "[KLCalibrator] Guard: Z_" << i
                              << " underflowed at iteration " << iter << "\n";
                    throw std::runtime_error(
                        "KLCalibrator: partition function Z_" + std::to_string(i) + " underflowed");
                }

                const double inv_Zi = 1.0 / Zi;
                for (int k = 0; k <= n; ++k)
                    q[static_cast<std::size_t>(i - 1)][static_cast<std::size_t>(k)] *= inv_Zi;
            }

            // ── 3. Gradient ∇g = Aq*−b and Hessian ∇²g in a single pass ─────
            std::fill(grad.begin(), grad.end(), 0.0);
            for (auto& row : H) std::fill(row.begin(), row.end(), 0.0);

            for (int i = 1; i <= N; ++i) {
                const double wi_inv = 1.0 / weights[static_cast<std::size_t>(i - 1)];

                // ebar[m] = E_{q*(t_i)}[A_m] = Σ_k A[m][idx(i,k)]·q[i-1][k]
                std::fill(ebar.begin(), ebar.end(), 0.0);
                for (int m = 0; m < M; ++m)
                    for (int k = 0; k <= n; ++k)
                        ebar[static_cast<std::size_t>(m)] +=
                            cm.A[static_cast<std::size_t>(m)]
                               [static_cast<std::size_t>(idx(i, k, n))]
                            * q[static_cast<std::size_t>(i - 1)][static_cast<std::size_t>(k)];

                for (int m = 0; m < M; ++m)
                    grad[static_cast<std::size_t>(m)] += ebar[static_cast<std::size_t>(m)];

                // H[m][l] -= (1/wi)·Cov[Am, Al]
                // STABLE FORMULA: Cov = Σ_k (A_m − ē_m)(A_l − ē_l)·q_k
                // Avoids catastrophic cancellation in E[XY]−E[X]E[Y]
                // when the variance is near zero (e.g. equity tranche at short dates
                // where nearly all mass is at k=0 or k≥7, so l_k^0 ≈ constant).
                for (int m = 0; m < M; ++m) {
                    for (int l = m; l < M; ++l) {
                        double cov = 0.0;
                        for (int k = 0; k <= n; ++k) {
                            const double da =
                                cm.A[static_cast<std::size_t>(m)][static_cast<std::size_t>(idx(i, k, n))]
                                - ebar[static_cast<std::size_t>(m)];
                            const double db =
                                cm.A[static_cast<std::size_t>(l)][static_cast<std::size_t>(idx(i, k, n))]
                                - ebar[static_cast<std::size_t>(l)];
                            cov += da * db
                                 * q[static_cast<std::size_t>(i - 1)][static_cast<std::size_t>(k)];
                        }
                        H[static_cast<std::size_t>(m)][static_cast<std::size_t>(l)] -= wi_inv * cov;
                        if (l != m)
                            H[static_cast<std::size_t>(l)][static_cast<std::size_t>(m)] -= wi_inv * cov;
                    }
                }
            }
            for (int m = 0; m < M; ++m)
                grad[static_cast<std::size_t>(m)] -= cm.b[static_cast<std::size_t>(m)];

            // ── 4. Verbose iteration log ──────────────────────────────────────
            if (m_verbose) {
                const double gn = norm_inf(grad, M);
                std::cerr << std::fixed << std::setprecision(3)
                          << "  [NR iter=" << std::setw(3) << iter
                          << "]  ‖∇g‖∞=" << std::scientific << std::setprecision(4) << gn
                          << "  H_diag=[";
                for (int m = 0; m < M; ++m)
                    std::cerr << std::setprecision(3)
                              << H[static_cast<std::size_t>(m)][static_cast<std::size_t>(m)]
                              << (m < M - 1 ? "  " : "]\n");
            }

            // ── 5. Convergence check ─────────────────────────────────────────
            res.iterations = iter + 1;
            if (norm_inf(grad, M) < m_tol) {
                res.converged  = true;
                res.mu_star    = mu;
                res.q_star     = q;
                res.grad_final = grad;
                res.dual_value = dual_value(mu, cm, prior, weights, N, n, M);
                return res;
            }

            // ── 6. Newton step: solve H·Δ = ∇g, then μ ← μ − Δ ─────────────
            // Maximising concave g : max_Δ [∇gᵀΔ + ½ΔᵀHΔ] gives H·Δ = -∇g
            // Equivalently : solve H·Δ = +∇g, then SUBTRACT Δ from μ.
            // The document §7.5 writes "μ += Δ" — that is a sign error; the
            // correct ascent step requires subtracting H⁻¹∇g.
            const std::vector<double> delta_mu = solve_linear(H, grad, M);
            for (int m = 0; m < M; ++m)
                mu[static_cast<std::size_t>(m)] -= delta_mu[static_cast<std::size_t>(m)];
        }

        // Max iterations reached — q and grad are from the last iteration.
        // mu has been updated one Newton step past them; still a usable result.
        std::cerr << "[KLCalibrator] Guard: max iterations (" << m_max_iter
                  << ") reached without convergence.  ‖∇g‖∞ = "
                  << norm_inf(grad, M) << "\n";

        res.mu_star    = mu;
        res.q_star     = q;
        res.grad_final = grad;
        res.dual_value = dual_value(mu, cm, prior, weights, N, n, M);
        return res;
    }

private:
    double m_tol;
    int    m_max_iter;
    bool   m_verbose;

    // ── g(μ) = −μᵀb − Σ_i w_i log Z_i(μ) ────────────────────────────────────
    [[nodiscard]] static double dual_value(
        const std::vector<double>&               mu,
        const ConstraintMatrix&                  cm,
        const std::vector<std::vector<double>>&  prior,
        const std::vector<double>&               weights,
        const int N, const int n, const int M)
    {
        const int dim = N * (n + 1);

        std::vector<double> Atmu(static_cast<std::size_t>(dim), 0.0);
        for (int m = 0; m < M; ++m)
            for (int j = 0; j < dim; ++j)
                Atmu[static_cast<std::size_t>(j)] +=
                    cm.A[static_cast<std::size_t>(m)][static_cast<std::size_t>(j)]
                    * mu[static_cast<std::size_t>(m)];

        double g = 0.0;
        for (int m = 0; m < M; ++m)
            g -= mu[static_cast<std::size_t>(m)] * cm.b[static_cast<std::size_t>(m)];

        for (int i = 1; i <= N; ++i) {
            const double wi = weights[static_cast<std::size_t>(i - 1)];

            double max_arg = std::numeric_limits<double>::lowest();
            for (int k = 0; k <= n; ++k)
                max_arg = std::max(max_arg,
                    -Atmu[static_cast<std::size_t>(idx(i, k, n))] / wi);

            double Zi_s = 0.0;
            for (int k = 0; k <= n; ++k)
                Zi_s += prior[static_cast<std::size_t>(i - 1)][static_cast<std::size_t>(k)]
                      * std::exp(-Atmu[static_cast<std::size_t>(idx(i, k, n))] / wi - max_arg);

            // log(Z_actual) = log(Z_shifted) + max_arg
            g -= wi * (std::log(Zi_s) + max_arg);
        }
        return g;
    }

    // ── Gaussian elimination with partial pivoting — solves A·x = b (M×M) ───
    // Throws if any pivot is numerically zero (singular Hessian).
    [[nodiscard]] static std::vector<double>
    solve_linear(std::vector<std::vector<double>> A, std::vector<double> b, const int M)
    {
        for (int col = 0; col < M; ++col) {
            // Partial pivot
            int piv = col;
            for (int row = col + 1; row < M; ++row)
                if (std::abs(A[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)])
                  > std::abs(A[static_cast<std::size_t>(piv)][static_cast<std::size_t>(col)]))
                    piv = row;
            std::swap(A[static_cast<std::size_t>(col)], A[static_cast<std::size_t>(piv)]);
            std::swap(b[static_cast<std::size_t>(col)], b[static_cast<std::size_t>(piv)]);

            const double diag = A[static_cast<std::size_t>(col)][static_cast<std::size_t>(col)];
            if (std::abs(diag) < 1e-14) {
                std::cerr << "[KLCalibrator] Guard: singular Hessian pivot at column "
                          << col << " = " << diag << "\n";
                throw std::runtime_error(
                    "KLCalibrator: singular Hessian at column " + std::to_string(col));
            }

            for (int row = col + 1; row < M; ++row) {
                const double fac =
                    A[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)] / diag;
                for (int c2 = col; c2 < M; ++c2)
                    A[static_cast<std::size_t>(row)][static_cast<std::size_t>(c2)] -=
                        fac * A[static_cast<std::size_t>(col)][static_cast<std::size_t>(c2)];
                b[static_cast<std::size_t>(row)] -= fac * b[static_cast<std::size_t>(col)];
            }
        }
        // Back-substitution
        std::vector<double> x(static_cast<std::size_t>(M), 0.0);
        for (int row = M - 1; row >= 0; --row) {
            double s = b[static_cast<std::size_t>(row)];
            for (int col = row + 1; col < M; ++col)
                s -= A[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)]
                   * x[static_cast<std::size_t>(col)];
            x[static_cast<std::size_t>(row)] =
                s / A[static_cast<std::size_t>(row)][static_cast<std::size_t>(row)];
        }
        return x;
    }

    [[nodiscard]] static double norm_inf(const std::vector<double>& v, const int M) noexcept
    {
        double s = 0.0;
        for (int i = 0; i < M; ++i)
            s = std::max(s, std::abs(v[static_cast<std::size_t>(i)]));
        return s;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// KLTranchePricer  — computes par-spreads and NPVs from calibrated q*
// ─────────────────────────────────────────────────────────────────────────────
class KLTranchePricer {
public:
    KLTranchePricer(
        const int    n_credit,
        const double recovery_rate,
        const std::vector<std::vector<double>>& q_star,
        const TimeGrid&                          grid)
        : m_n(n_credit), m_R(recovery_rate),
          m_q(q_star), m_grid(grid),
          m_unit((1.0 - recovery_rate) / n_credit)
    {}

    // E[min(L(t_i), K)] from calibrated marginals
    [[nodiscard]] double expected_loss_capped(const double K, const int ti) const {
        double el = 0.0;
        for (int k = 0; k <= m_n; ++k)
            el += std::min(static_cast<double>(k) * m_unit, K)
                * m_q[static_cast<std::size_t>(ti)][static_cast<std::size_t>(k)];
        return el;
    }

    // Tranche [K1, K2] survival at grid node ti
    [[nodiscard]] double tranche_survival(const double K1, const double K2, const int ti) const {
        return 1.0 - (expected_loss_capped(K2, ti) - expected_loss_capped(K1, ti)) / (K2 - K1);
    }

    // Par spread — EXACT formula from Proposition 7.4, consistent with build_constraint_matrix.
    //
    // Premium leg : PL = Σ_{i: is_prem}  df_i · δ^P_i · Q_m(t_i)
    // Default leg : DL = Σ_{i=1}^N  [df_i − df_{i+1}·1_{i<N}] · (1 − Q_m(t_i))
    //
    // When Newton has converged (Aq* = b), par_spread returns s_m^market up to
    // the Newton tolerance.  The previous trapezoidal approximation introduced a
    // systematic ~30 bps bias on the equity tranche because Q_m varies fast there.
    [[nodiscard]] double par_spread(const Market::Index_tranche& tr) const {
        const int N = m_grid.N;

        // Premium leg
        double prem = 0.0;
        for (int i = 0; i < N; ++i) {
            const auto& nd = m_grid.nodes[static_cast<std::size_t>(i)];
            if (!nd.is_prem) continue;
            prem += nd.df * nd.delta_P * tranche_survival(tr.K1, tr.K2, i);
        }

        // Default leg  (df_{N+1} ≡ 0)
        double prot = 0.0;
        for (int i = 0; i < N; ++i) {
            const auto& nd     = m_grid.nodes[static_cast<std::size_t>(i)];
            const double df_next = (i < N - 1)
                ? m_grid.nodes[static_cast<std::size_t>(i + 1)].df : 0.0;
            prot += (nd.df - df_next) * (1.0 - tranche_survival(tr.K1, tr.K2, i));
        }

        return (prem * tr.nominal > 1e-15) ? prot / prem : 0.0;
    }

    // NPV — same exact legs as par_spread
    [[nodiscard]] double npv(const Market::Index_tranche& tr) const {
        const int N = m_grid.N;

        double prem = 0.0;
        for (int i = 0; i < N; ++i) {
            const auto& nd = m_grid.nodes[static_cast<std::size_t>(i)];
            if (!nd.is_prem) continue;
            prem += nd.df * nd.delta_P * tranche_survival(tr.K1, tr.K2, i);
        }

        double prot = 0.0;
        for (int i = 0; i < N; ++i) {
            const auto& nd     = m_grid.nodes[static_cast<std::size_t>(i)];
            const double df_next = (i < N - 1)
                ? m_grid.nodes[static_cast<std::size_t>(i + 1)].df : 0.0;
            prot += (nd.df - df_next) * (1.0 - tranche_survival(tr.K1, tr.K2, i));
        }

        // NPV = DL - s_contract · PL   (protection seller convention)
        return (prot - tr.contractual_spread * prem) * tr.nominal;
    }

private:
    int    m_n;
    double m_R;
    const std::vector<std::vector<double>>& m_q;
    const TimeGrid&                         m_grid;
    double m_unit;
};

} // namespace KL