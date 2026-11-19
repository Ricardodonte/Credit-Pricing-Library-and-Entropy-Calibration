// Calibration KL — iTraxx Europe Série 42, 5Y  (sections 7.3–7.5 du mémoire)
//

#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <cmath>
#include <stdexcept>
#include <numeric>
#include <algorithm>

#include "Core/numericals.h"
#include "Core/types.h"
#include "Core/Dates.h"
#include "Market/Curves/YieldCurve/YieldCurve.h"
#include "Market/Curves/CDS/CreditCurve.h"
#include "Market/Instruments/Tranches/tranches_instruments.h"
#include "KL_pricer.h"

// ─────────────────────────────────────────────────────────────────────────────
// Console helpers
// ─────────────────────────────────────────────────────────────────────────────
static void separator(char c = '-', int w = 62) {
    std::cout << "  " << std::string(static_cast<std::size_t>(w), c) << "\n";
}
static void print_header(const std::string& title) {
    std::cout << "\n╔══════════════════════════════════════════════════════╗\n"
              << "║  " << std::left << std::setw(52) << title << "║\n"
              << "╚══════════════════════════════════════════════════════╝\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// EUR yield curve — EURIBOR deposits + €STR swaps, juin 2026
// ─────────────────────────────────────────────────────────────────────────────
static Market::YieldCurve build_eur_yield_curve(const Core::Date& ref)
{
    Market::YieldCurveBoot boot(ref);
    boot.add_deposits({
        { 1.0/12.0, 0.0225 },
        { 3.0/12.0, 0.0235 },
        { 6.0/12.0, 0.0245 },
    });
    boot.add_swaps({
        {  1.0, 0.0255, 1.0, 1.0 },
        {  2.0, 0.0265, 1.0, 1.0 },
        {  3.0, 0.0275, 1.0, 1.0 },
        {  5.0, 0.0290, 1.0, 1.0 },
        {  7.0, 0.0300, 1.0, 1.0 },
        { 10.0, 0.0310, 1.0, 1.0 },
    });
    return boot.curve();
}

// ─────────────────────────────────────────────────────────────────────────────
// Courbe de crédit — structure par terme upward-sloping calibrée sur iTraxx
// ─────────────────────────────────────────────────────────────────────────────
static Market::CreditCurve build_credit_curve(
    const std::string&        name,
    const double              s5y_bps,
    const double              recovery,
    const Core::Date&         ref,
    const Market::YieldCurve& yc)
{
    const double s = s5y_bps / 10'000.0;
    Market::CDSMarketData md;
    md.name          = name;
    md.effectiveDate = ref;
    md.valuationDate = ref;
    md.recoveryRate  = recovery;
    md.frequency     = Core::Frequency::QUARTERLY;
    md.quotes = {
        { 1.0, s * 0.55 },
        { 2.0, s * 0.75 },
        { 3.0, s * 0.88 },
        { 5.0, s * 1.00 },
        { 7.0, s * 1.08 },
    };
    return Market::CreditBoot(md, yc, ref).curve();
}

// ─────────────────────────────────────────────────────────────────────────────
// Composition iTraxx Europe S42 (125 noms, 4 buckets)
// ─────────────────────────────────────────────────────────────────────────────
struct Bucket { int count; double s5y_bps; const char* label; };
static constexpr std::array<Bucket, 4> BUCKETS = {{
    { 25,  45.0, "HQ"   },
    { 40,  65.0, "IG"   },
    { 35,  88.0, "Cycl" },
    { 25, 118.0, "Beta" },
}};

// ─────────────────────────────────────────────────────────────────────────────
// Diagnostics helpers
// ─────────────────────────────────────────────────────────────────────────────

struct PriorStats {
    double mean_loss;
    double var_loss;
    double p_zero;
    double p_above_eq;
};

static PriorStats prior_stats(
    const std::vector<double>& pi,
    const int n, const double unit)
{
    double mean = 0.0, e2 = 0.0, p0 = pi[0];
    for (int k = 0; k <= n; ++k) {
        const double l = k * unit;
        mean += l * pi[static_cast<std::size_t>(k)];
        e2   += l * l * pi[static_cast<std::size_t>(k)];
    }
    const int k_eq = static_cast<int>(std::ceil(0.03 / unit));
    double p_above = 0.0;
    for (int k = k_eq; k <= n; ++k)
        p_above += pi[static_cast<std::size_t>(k)];
    return { mean, e2 - mean * mean, p0, p_above };
}

static double row_norm(const std::vector<double>& row) {
    double s = 0.0;
    for (const double v : row) s += v * v;
    return std::sqrt(s);
}

static void print_constraint_diagnostics(
    const KL::ConstraintMatrix& cm,
    const std::vector<Market::Index_tranche>& tranches)
{
    print_header("Diagnostics — Matrice de contraintes");
    std::cout << "\n  Dimension : A ∈ R^{" << cm.M << " × "
              << cm.N * (cm.n + 1) << "}    b ∈ R^{" << cm.M << "}\n\n";
    separator();
    std::cout << "  " << std::left
              << std::setw(10) << "Tranche"
              << std::setw(14) << "||A_m||_2"
              << std::setw(14) << "||A_m||_inf"
              << std::setw(14) << "b_m"
              << std::setw(14) << "b_m/||A_m||"
              << "\n";
    separator();
    double min_rn = 1e300, max_rn = 0.0;
    for (int m = 0; m < cm.M; ++m) {
        const auto&  row = cm.A[static_cast<std::size_t>(m)];
        const double rn  = row_norm(row);
        const double ri  = *std::max_element(row.begin(), row.end(),
                               [](double a, double b){ return std::abs(a) < std::abs(b); });
        const double bm  = cm.b[static_cast<std::size_t>(m)];
        const std::string name =
            "[" + std::to_string(static_cast<int>(
                      tranches[static_cast<std::size_t>(m)].K1 * 100)) + "-"
                + std::to_string(static_cast<int>(
                      tranches[static_cast<std::size_t>(m)].K2 * 100)) + "%]";
        std::cout << "  " << std::left  << std::setw(10) << name
                  << std::right << std::scientific << std::setprecision(4)
                  << std::setw(14) << rn
                  << std::setw(14) << std::abs(ri)
                  << std::setw(14) << bm
                  << std::setw(14) << (rn > 0 ? bm / rn : 0.0) << "\n";
        min_rn = std::min(min_rn, rn);
        max_rn = std::max(max_rn, rn);
    }
    separator();
    std::cout << "  Ratio max/min ||A_m|| (proxy cond) : "
              << std::fixed << std::setprecision(2) << max_rn / min_rn << "\n";
}

static void print_prior_diagnostics(
    const std::vector<std::vector<double>>& prior,
    const KL::TimeGrid& grid,
    const int n, const double unit)
{
    print_header("Diagnostics — Distribution prior p(t_i)");

    const int k_eq = static_cast<int>(std::ceil(0.03 / unit));
    std::cout << "\n  (k_eq = " << k_eq << ", x_{k_eq} = "
              << std::fixed << std::setprecision(4) << k_eq * unit
              << " > K2_equity = 0.03)\n\n";
    separator();
    std::cout << "  " << std::left
              << std::setw(9)  << "t (yr)"
              << std::setw(16) << "E[L(t)]"
              << std::setw(16) << "Var[L(t)]"
              << std::setw(12) << "P(k=0)"
              << std::setw(16) << "P(k>=k_eq)"
              << std::setw(16) << "P(k=1..k_eq-1)"
              << "\n";
    separator();

    const int step = std::max(1, grid.N / 8);
    for (int i = 0; i < grid.N; i += step) {
        const auto s = prior_stats(prior[static_cast<std::size_t>(i)], n, unit);
        const double p_mid = 1.0 - s.p_zero - s.p_above_eq;
        std::cout << "  " << std::fixed << std::setprecision(3)
                  << std::setw(8)  << grid.nodes[static_cast<std::size_t>(i)].t
                  << std::scientific << std::setprecision(4)
                  << std::setw(16) << s.mean_loss
                  << std::setw(16) << s.var_loss
                  << std::fixed << std::setprecision(6)
                  << std::setw(12) << s.p_zero
                  << std::setw(16) << s.p_above_eq
                  << std::setw(16) << p_mid << "\n";
    }
    separator();
    std::cout << "  NOTE: P(k=1..k_eq-1) est la masse dans la zone equity-sensitive.\n"
              << "        Si proche de 0 a toutes dates → Var[A_0] ≈ 0 → H quasi-singuliere.\n";
}

static double prior_par_spread(
    const std::vector<std::vector<double>>& prior,
    const KL::TimeGrid& grid,
    const Market::Index_tranche& tr,
    const int n, const double recovery)
{
    KL::KLTranchePricer pricer(n, recovery, prior, grid);
    return pricer.par_spread(tr);
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────
int main()
{
    try {
        print_header("KL Tranche Calibration — iTraxx Europe S42, 5Y");

        // ── Dates ────────────────────────────────────────────────────────────
        // IMPORTANT : Core::Date() sans argument = TODAY (via system_clock).
        // Il faut TOUJOURS renseigner les deux dates explicitement.
        // val_date = 2026-06-02 (lundi, date business)
        // mat_date = 2031-06-20 (prochaine IMM date 5Y après val_date)
        //            IMM = 20 mars/juin/septembre/décembre
        //            → première IMM ≥ 2031-06-02 = 2031-06-20 ✓
        const Core::Date val_date{2026, 6, 2};
        const Core::Date mat_date{2031, 6, 20};   // ← OBLIGATOIRE, sinon = aujourd'hui

        constexpr double recovery = 0.40;
        constexpr int    n_names  = 125;
        constexpr double T        = 5.0;
        const double     unit     = (1.0 - recovery) / n_names;   // 0.0048

        std::cout << "\n  val_date = " << val_date.to_string()
                  << "  mat_date = " << mat_date.to_string()
                  << "  T = " << std::fixed << std::setprecision(4)
                  << Core::year_fraction(val_date, mat_date, Core::DayCount::ACT_360)
                  << " ans (ACT/360)\n";

        // ─── 1. Yield curve ────────────────────────────────────────────────
        std::cout << "\n[1/7] Bootstrap yield curve EUR...\n";
        const auto yc = build_eur_yield_curve(val_date);
        std::cout << "        df(1Y)=" << std::fixed << std::setprecision(6)
                  << yc.discount(1.0)
                  << "  df(3Y)=" << yc.discount(3.0)
                  << "  df(5Y)=" << yc.discount(5.0) << "\n";

        // ─── 2. Credit curves ──────────────────────────────────────────────
        std::cout << "\n[2/7] Bootstrap 125 courbes de credit (4 buckets)...\n";
        std::vector<Market::CreditCurve> credit_curves;
        credit_curves.reserve(static_cast<std::size_t>(n_names));
        double sum_s5y = 0.0;
        for (const auto& bkt : BUCKETS) {
            for (int j = 0; j < bkt.count; ++j) {
                credit_curves.push_back(
                    build_credit_curve(
                        std::string(bkt.label) + "_" + std::to_string(j),
                        bkt.s5y_bps, recovery, val_date, yc));
                sum_s5y += bkt.s5y_bps;
            }
            std::cout << "        " << bkt.label << "  " << bkt.count
                      << " noms  s5Y=" << bkt.s5y_bps << " bps\n";
        }
        std::cout << "        → spread moyen 5Y = "
                  << std::fixed << std::setprecision(1) << sum_s5y / n_names << " bps\n";

        // ─── 3. Tranches ───────────────────────────────────────────────────
        std::cout << "\n[3/7] Definition des tranches iTraxx Europe 5Y...\n";

        // Lambda de construction — MaturityDate TOUJOURS renseignee
        auto make_tranche = [&](double K1, double K2, double mkt_bps) {
            Market::Index_tranche tr;
            tr.K1                 = K1;
            tr.K2                 = K2;
            tr.nominal            = 1.0;
            tr.contractual_spread = mkt_bps / 10'000.0;
            tr.fair_spread        = mkt_bps / 10'000.0;
            tr.frequency          = Core::Frequency::QUARTERLY;
            tr.day_count          = Core::DayCount::ACT_360;
            tr.valuation_date     = val_date;
            tr.effective_date     = val_date;
            tr.MaturityDate       = mat_date;   // ← CRITIQUE
            tr.quoted_upfront     = false;
            return tr;
        };

        const std::vector<Market::Index_tranche> tranches = {
            make_tranche(0.00, 0.03, 1350.0),
            make_tranche(0.03, 0.06,  220.0),
            make_tranche(0.06, 0.09,   85.0),
            make_tranche(0.09, 0.12,   42.0),
            make_tranche(0.12, 0.22,   18.0),
        };
        const int M = static_cast<int>(tranches.size());

        separator();
        std::cout << "  " << std::left
                  << std::setw(12) << "Tranche"
                  << std::setw(14) << "K1"
                  << std::setw(14) << "K2"
                  << std::setw(14) << "Mkt (bps)"
                  << std::setw(16) << "MaturityDate" << "\n";
        separator();
        for (int m = 0; m < M; ++m) {
            const auto& tr = tranches[static_cast<std::size_t>(m)];
            const std::string nm =
                "[" + std::to_string(static_cast<int>(tr.K1*100)) + "-"
                    + std::to_string(static_cast<int>(tr.K2*100)) + "%]";
            std::cout << "  " << std::left << std::setw(12) << nm
                      << std::right << std::fixed << std::setprecision(2)
                      << std::setw(12) << tr.K1 * 100 << "%"
                      << std::setw(13) << tr.K2 * 100 << "%"
                      << std::setw(12) << tr.fair_spread * 1e4
                      << "    " << tr.MaturityDate.to_string() << "\n";
        }

        // ─── 4. Grille temporelle ──────────────────────────────────────────
        std::cout << "\n[4/7] Construction grille T = T^P ∪ T^D (ISDA IMM)...\n";
        const KL::TimeGrid grid = KL::build_kl_time_grid(tranches[0], yc);
        std::cout << "        N=" << grid.N << "  NP=" << grid.NP
                  << "  T_last=" << std::fixed << std::setprecision(4)
                  << grid.nodes.back().t
                  << "  sum_prem_df_delta=" << std::setprecision(6)
                  << grid.sum_prem_df_delta << "\n";

        if (std::abs(grid.nodes.back().t - T) > 0.15)
            std::cerr << "  [WARN] T_last=" << grid.nodes.back().t
                      << " s'ecarte de T=" << T << " — verifier mat_date\n";

        // ─── 5. OLS sur ρ ──────────────────────────────────────────────────
        std::cout << "\n[5/7] Calibration rho_OLS (golden-section)...\n";
        KL::PriorBuilder prior_builder(credit_curves, n_names, recovery);

        std::vector<double> mkt_bps(static_cast<std::size_t>(M));
        for (int m = 0; m < M; ++m)
            mkt_bps[static_cast<std::size_t>(m)] =
                tranches[static_cast<std::size_t>(m)].fair_spread * 1e4;

        auto par_spread_fn = [&](const double rho, const int m) -> double {
            const auto p = prior_builder.build(grid, rho);
            return prior_par_spread(
                p, grid, tranches[static_cast<std::size_t>(m)],
                n_names, recovery) * 1e4;
        };

        const double rho_ols = KL::PriorBuilder::fit_rho_ols(
            tranches, mkt_bps, par_spread_fn, 0.01, 0.95, 100, 1e-7);
        std::cout << "        rho_OLS = "
                  << std::fixed << std::setprecision(6) << rho_ols << "\n";

        // ─── 6. Prior à rho_OLS + diagnostics ─────────────────────────────
        std::cout << "\n[6/7] Construction prior p(t_i) a rho_OLS...\n";
        const auto prior = prior_builder.build(grid, rho_ols);

        // Repricing prior vs marché
        std::cout << "\n  Prior par-spreads a rho_OLS = "
                  << std::fixed << std::setprecision(4) << rho_ols << ":\n";
        separator();
        std::cout << "  " << std::left
                  << std::setw(12) << "Tranche"
                  << std::setw(14) << "Mkt (bps)"
                  << std::setw(16) << "Prior (bps)"
                  << std::setw(14) << "Delta (bps)"
                  << std::setw(12) << "Delta (%)" << "\n";
        separator();
        for (int m = 0; m < M; ++m) {
            const double sp = prior_par_spread(
                prior, grid, tranches[static_cast<std::size_t>(m)],
                n_names, recovery) * 1e4;
            const double smkt = mkt_bps[static_cast<std::size_t>(m)];
            const std::string nm =
                "[" + std::to_string(static_cast<int>(
                          tranches[static_cast<std::size_t>(m)].K1 * 100)) + "-"
                    + std::to_string(static_cast<int>(
                          tranches[static_cast<std::size_t>(m)].K2 * 100)) + "%]";
            std::cout << "  " << std::left  << std::setw(12) << nm
                      << std::right << std::fixed << std::setprecision(1)
                      << std::setw(14) << smkt
                      << std::setw(16) << sp
                      << std::setw(14) << (sp - smkt)
                      << std::setw(12) << std::setprecision(1)
                      << (smkt > 0 ? (sp - smkt) / smkt * 100.0 : 0.0) << "%\n";
        }
        separator();
        std::cout << "  NOTE: Ecarts attendus — le modele gaussien ne reproduit pas\n"
                  << "        le smile. C'est le role de la calibration KL de corriger.\n";

        // Matrice de contraintes + diagnostics
        const KL::ConstraintMatrix cm =
            KL::build_constraint_matrix(tranches, grid, n_names, recovery);
        print_constraint_diagnostics(cm, tranches);
        print_prior_diagnostics(prior, grid, n_names, unit);

        // Poids temporels w_i = Delta_t_i
        std::vector<double> weights(static_cast<std::size_t>(grid.N));
        for (int i = 0; i < grid.N; ++i) {
            const double t_prev = (i > 0)
                ? grid.nodes[static_cast<std::size_t>(i - 1)].t : 0.0;
            weights[static_cast<std::size_t>(i)] =
                std::max(grid.nodes[static_cast<std::size_t>(i)].t - t_prev, 1e-8);
        }

        // ─── 7. Newton-Raphson dual ────────────────────────────────────────
        print_header("Dual Newton-Raphson  (KL calibration §7.5)");

        KL::KLCalibrator calibrator(1e-10, 300, /*verbose=*/true);

        std::cout << "\n  Parametres Newton :\n"
                  << "    tol        = 1e-10\n"
                  << "    max_iter   = 300\n"
                  << "    M (nb contraintes) = " << M << "\n"
                  << "    N (nb noeuds grille) = " << grid.N << "\n"
                  << "    dim = N*(n+1) = " << grid.N * (n_names + 1) << "\n\n";

        const KL::KLResult result = calibrator.calibrate(cm, prior, weights);

        // ─── Résultats convergence ─────────────────────────────────────────
        print_header("Resultats — Convergence");

        std::cout << "\n  Convergence   : "
                  << (result.converged ? "OUI" : "NON — max iter atteint") << "\n"
                  << "  Iterations    : " << result.iterations << "\n"
                  << "  g(mu*)        : "
                  << std::scientific << std::setprecision(8) << result.dual_value << "\n";

        double gnorm = 0.0;
        for (const double v : result.grad_final)
            gnorm = std::max(gnorm, std::abs(v));
        std::cout << "  ||grad g(mu*)||_inf : " << gnorm;
        if (gnorm > 1e-6)
            std::cout << "  <- ATTENTION : gradient residuel eleve";
        std::cout << "\n\n";

        std::cout << "  Solution duale mu* :\n";
        separator('-', 42);
        const std::vector<std::string> tnames =
            {"[0-3%]","[3-6%]","[6-9%]","[9-12%]","[12-22%]"};
        for (int m = 0; m < M; ++m)
            std::cout << "    mu[" << m << "]  "
                      << std::left << std::setw(10)
                      << tnames[static_cast<std::size_t>(m)]
                      << std::right << std::fixed << std::setprecision(8)
                      << result.mu_star[static_cast<std::size_t>(m)] << "\n";

        // ─── Repricing q* ──────────────────────────────────────────────────
        print_header("Repricing — Tranches iTraxx Europe 5Y");

        const KL::KLTranchePricer kl_pricer(n_names, recovery, result.q_star, grid);
        std::cout << "\n  " << std::left
                  << std::setw(12) << "Tranche"
                  << std::setw(14) << "Mkt (bps)"
                  << std::setw(16) << "KL q* (bps)"
                  << std::setw(14) << "Err (bps)"
                  << std::setw(12) << "Err (%)" << "\n";
        separator();

        double max_abs = 0.0, max_rel = 0.0;
        for (int m = 0; m < M; ++m) {
            const auto&  tr   = tranches[static_cast<std::size_t>(m)];
            const double skl  = kl_pricer.par_spread(tr) * 1e4;
            const double smkt = tr.fair_spread * 1e4;
            const double aerr = skl - smkt;
            const double rerr = (smkt > 0) ? std::abs(aerr) / smkt * 100.0 : 0.0;
            max_abs = std::max(max_abs, std::abs(aerr));
            max_rel = std::max(max_rel, rerr);
            const std::string nm =
                "[" + std::to_string(static_cast<int>(tr.K1 * 100)) + "-"
                    + std::to_string(static_cast<int>(tr.K2 * 100)) + "%]";
            std::cout << "  " << std::left  << std::setw(12) << nm
                      << std::right << std::fixed << std::setprecision(2)
                      << std::setw(12) << smkt << "  "
                      << std::setw(14) << skl  << "  "
                      << std::setw(12) << aerr << "  "
                      << std::setprecision(3) << std::setw(9) << rerr << "%\n";
        }
        separator();
        std::cout << "  Max erreur absolue  : "
                  << std::fixed << std::setprecision(4) << max_abs << " bps\n"
                  << "  Max erreur relative : "
                  << std::setprecision(3) << max_rel << " %\n";

        if (max_abs > 1.0)
            std::cout << "\n  [WARN] Erreur > 1 bps — verifier convergence Newton "
                         "et coherence du prior.\n";

        // ─── Prior vs postérieur — espérance de perte ─────────────────────
        print_header("Prior vs Posterieur — E[L(t)] et KL(q*||p)");

        const int step = std::max(1, grid.N / 8);
        double kl_total = 0.0;

        std::cout << "\n  " << std::left
                  << std::setw(9)  << "t (yr)"
                  << std::setw(16) << "E_p[L]"
                  << std::setw(16) << "E_q*[L]"
                  << std::setw(14) << "Delta=q*-p"
                  << std::setw(18) << "KL(q*(t_i)||p(t_i))" << "\n";
        separator();

        for (int i = 0; i < grid.N; ++i) {
            double ep = 0.0, eq = 0.0, kl_i = 0.0;
            for (int k = 0; k <= n_names; ++k) {
                const double l   = k * unit;
                const double pik = prior        [static_cast<std::size_t>(i)][static_cast<std::size_t>(k)];
                const double qik = result.q_star[static_cast<std::size_t>(i)][static_cast<std::size_t>(k)];
                ep += l * pik;
                eq += l * qik;
                if (qik > 1e-300 && pik > 1e-300)
                    kl_i += qik * std::log(qik / pik);
            }
            kl_total += kl_i;
            if (i % step == 0)
                std::cout << "  " << std::fixed << std::setprecision(3)
                          << std::setw(8) << grid.nodes[static_cast<std::size_t>(i)].t
                          << std::scientific << std::setprecision(4)
                          << std::setw(16) << ep
                          << std::setw(16) << eq
                          << std::setw(14) << (eq - ep)
                          << std::setw(18) << kl_i << "\n";
        }
        separator();
        std::cout << "  KL(q*||p) total : "
                  << std::scientific << std::setprecision(6) << kl_total << "\n";

        // ─── Synthèse ──────────────────────────────────────────────────────
        print_header("Synthese diagnostique");
        std::cout << "\n"
                  << "  val_date             : " << val_date.to_string() << "\n"
                  << "  mat_date             : " << mat_date.to_string() << "\n"
                  << "  Convergence          : " << (result.converged ? "OUI" : "NON") << "\n"
                  << "  Iterations Newton    : " << result.iterations << "\n"
                  << "  ||grad g||_inf final : " << std::scientific << gnorm << "\n"
                  << "  g(mu*) dual          : " << result.dual_value << "\n"
                  << "  KL total (q*||p)     : " << kl_total << "\n"
                  << "  Max repr. abs.       : " << std::fixed
                  << std::setprecision(4) << max_abs << " bps\n"
                  << "  Max repr. rel.       : "
                  << std::setprecision(3) << max_rel << " %\n"
                  << "  rho_OLS              : "
                  << std::setprecision(6) << rho_ols << "\n\n";

        std::cout << "[OK] Calibration KL terminee.\n\n";
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "\n[ERREUR] " << e.what() << "\n";
        return 1;
    }
}