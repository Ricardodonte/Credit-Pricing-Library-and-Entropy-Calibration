
#include <iostream>
#include <iomanip>
#include <vector>
#include <chrono>
#include <string>
#include <cmath>

#include "Core/Dates.h"
#include "Core/types.h"
#include "Core/numericals.h"
#include "Market/instruments.h"
#include "Market/YieldCurve.h"
#include "Market/CreditCurve.h"
#include "Market/tranches_instruments.h"
#include "Pricers/full_recursion_pricing.h"
#include "Pricers/binomial_pricer.h"

static void section(const std::string& s) {
    std::cout << "\n" << std::string(72, '=') << "\n  " << s
              << "\n" << std::string(72, '=') << "\n";
}

static void check(const std::string& name, double val, double exp, double tol = 1e-6) {
    bool ok = std::abs(val - exp) < tol;
    std::cout << "  " << std::left << std::setw(54) << name
              << std::right << std::fixed << std::setprecision(8)
              << std::setw(14) << val
              << "  exp " << std::setw(13) << exp
              << "  " << (ok ? "[OK]" : "[FAIL]") << "\n";
}

struct Timer {
    std::string label;
    std::chrono::high_resolution_clock::time_point t0;
    explicit Timer(std::string l)
        : label(std::move(l)), t0(std::chrono::high_resolution_clock::now()) {}
    ~Timer() {
        double ms = std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - t0).count();
        std::cout << "  [" << label << "]  "
                  << std::fixed << std::setprecision(3) << ms << " ms\n";
    }
};

// ─── Courbes marche ───────────────────────────────────────────────────────────

static Market::YieldCurve build_yc(const Core::Date& ref) {
    Market::YieldCurveBoot b(ref);
    b.add_deposits({{0.25,0.05},{0.5,0.05},{1.0,0.05}});
    b.add_swaps({
        {2.,0.05,1.,0.25},{3.,0.05,1.,0.25},{5.,0.05,1.,0.25},
        {7.,0.05,1.,0.25},{10.,0.05,1.,0.25}
    });
    return b.curve();
}

static Market::CreditBoot build_cb(const Core::Date& ref,
                                    const Market::YieldCurve& yc) {
    Market::CDSMarketData md;
    md.name="IDX"; md.effectiveDate=ref; md.valuationDate=ref;
    md.recoveryRate=0.40; md.frequency=Core::Frequency::QUARTERLY;
    md.quotes={{1.,100e-4},{3.,100e-4},{5.,100e-4},{7.,100e-4},{10.,100e-4}};
    return {md, yc, ref};
}

static Market::Index_tranche make_tr(double K1, double K2, double spd_bps,
                                      const Core::Date& ref) {
    Market::Index_tranche tr;
    tr.K1=K1; tr.K2=K2;
    tr.contractual_spread=spd_bps*1e-4; tr.fair_spread=spd_bps*1e-4;
    tr.upfront=0.; tr.quoted_upfront=false;
    tr.nominal=(K2-K1)*1e7; tr.maturity=5.;
    tr.effective_date=ref; tr.valuation_date=ref;
    tr.frequency=Core::Frequency::QUARTERLY;
    tr.day_count=Core::DayCount::ACT_360;
    return tr;
}

// ─── Tests ────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "\n  AdjustedBinomialPricer — test suite\n";

    const Core::Date ref(2026,4,17);
    constexpr int    N  = 125;
    constexpr double RR = 0.40;
    constexpr double lgd = 1.0 - RR;

    const auto yc  = build_yc(ref);
    const auto cb  = build_cb(ref, yc);
    const auto& cc = cb.curve();

    const std::vector<Market::CreditCurve> curves(N, cc);
    const std::vector<double>              rr_vec(N, RR);

    Pricer::AdjustedBinomialPricer adj_uni(curves, N, RR);
    Pricer::AdjustedBinomialPricer adj_vec(curves, N, rr_vec);
    Pricer::RecursionPricer         rec   (curves, N, RR);

    const double rho = 0.30, T = 5.0;

    // =========================================================================
    section("1. Proprietes fondamentales");
    // =========================================================================

    std::cout << "\n  a. EML(K=0) = 0 :\n";
    check("EML(K=0,   rho=0.30)", adj_uni.expected_min_loss(0.0, T, rho), 0.0, 1e-14);
    check("EML(K=0,   rho=0.00)", adj_uni.expected_min_loss(0.0, T, 0.0), 0.0, 1e-14);
    check("EML(K=0,   rho=0.99)", adj_uni.expected_min_loss(0.0, T, 0.99),0.0, 1e-14);

    std::cout << "\n  b. EML(K) >= 0 :\n";
    bool pos = true;
    for (double K : {0.01,0.03,0.06,0.09,0.12,0.22,1.00})
        if (adj_uni.expected_min_loss(K,T,rho) < -1e-12) { pos=false; break; }
    std::cout << "  EML(K) >= 0 pour tout K dans {0.01..1.00} : "
              << (pos ? "[OK]" : "[FAIL]") << "\n";

    std::cout << "\n  c. EML(K) <= K :\n";
    bool leK = true;
    for (double K : {0.01,0.03,0.06,0.09,0.12,0.22,1.00})
        if (adj_uni.expected_min_loss(K,T,rho) > K+1e-10) { leK=false; break; }
    std::cout << "  EML(K) <= K pour tout K          : "
              << (leK ? "[OK]" : "[FAIL]") << "\n";

    std::cout << "\n  d. EML(K) monotone croissante en K :\n";
    bool mono = true; double prev = 0;
    for (double K : {0.01,0.02,0.05,0.10,0.15,0.20,0.30,0.50,1.00}) {
        double v = adj_uni.expected_min_loss(K,T,rho);
        if (v < prev-1e-12) { mono=false; break; }
        prev = v;
    }
    std::cout << "  Monotonie en K                   : "
              << (mono ? "[OK]" : "[FAIL]") << "\n";

    std::cout << "\n  e. Sens correct de EML vs rho :\n";
    {
        // EL = lgd * p(T) ~ 4.8%. Sous la copule gaussienne a un facteur:
        // - Pour K < EL (equity pure)  : EML est DECROISSANTE en rho.
        //   Plus de correlation -> pertes concentrees sur quelques scenarios
        //   -> protection moins chere pour l'acheteur d'equity.
        // - Pour K > EL (mezz/senior)  : EML est aussi DECROISSANTE en rho.
        //   La distribution de pertes s'ecarte de la moyenne -> moins de masse
        //   entre 0 et K -> EML(K) = E[min(L,K)] diminue.
        // Dans les deux cas: EML decroit avec rho pour ce portefeuille.
        double e1 = adj_uni.expected_min_loss(0.09, T, 0.10);
        double e2 = adj_uni.expected_min_loss(0.09, T, 0.50);
        std::cout << "  EML(K=9%, rho=10%) = " << std::setprecision(6) << e1 << "\n"
                  << "  EML(K=9%, rho=50%) = " << e2
                  << "  (decroissante en rho : "
                  << (e1 > e2 ? "[OK])" : "[FAIL])") << "\n";
        // Equity K=1% : aussi decroissante
        double eq1 = adj_uni.expected_min_loss(0.01, T, 0.10);
        double eq2 = adj_uni.expected_min_loss(0.01, T, 0.50);
        std::cout << "  EML(K=1%, rho=10%) = " << eq1 << "\n"
                  << "  EML(K=1%, rho=50%) = " << eq2
                  << "  (decroissante en rho : "
                  << (eq1 > eq2 ? "[OK])" : "[FAIL])") << "\n";
    }

    // =========================================================================
    section("2. Constructeur uniforme == constructeur vecteur");
    // =========================================================================
    std::cout << "\n";
    for (double K : {0.01,0.03,0.06,0.09,0.12,0.22,1.00}) {
        double u = adj_uni.expected_min_loss(K,T,rho);
        double v = adj_vec.expected_min_loss(K,T,rho);
        check("  |uni - vec|  K=" + std::to_string(K).substr(0,4),
              std::abs(u-v), 0.0, 1e-15);
    }

    // =========================================================================
    section("3. Cas homogene : AdjBinom = Recursion exactement");
    // =========================================================================

    std::cout << "\n  En cas homogene (lgd_i = lgd pour tout i), le binomial ajuste\n"
              << "  reduit a un binomial simple (mass transfer = identite car sigma2_A = sigma2_E).\n"
              << "  Il doit etre IDENTIQUE a la recursion a la precision machine (< 0.001 bp).\n\n";

    std::cout << "  " << std::setw(8) << "K"
              << std::setw(16) << "AdjBinom"
              << std::setw(16) << "Recursion"
              << std::setw(14) << "Diff (bp)"
              << std::setw(14) << "Statut"
              << "\n  " << std::string(68,'-') << "\n";

    bool all_match = true;
    for (double K : {0.01,0.03,0.06,0.09,0.12,0.22,1.00}) {
        double eb = adj_uni.expected_min_loss(K,T,rho);
        double er = rec    .expected_min_loss(K,T,rho);
        double diff_bp = (eb-er)*1e4;
        bool ok = std::abs(diff_bp) < 0.001;  // seuil : 0.001 bp = precision numerique
        if (!ok) all_match = false;
        std::cout << "  " << std::setw(8) << K
                  << std::setw(16) << std::setprecision(8) << eb
                  << std::setw(16) << er
                  << std::setw(14) << std::setprecision(5) << diff_bp
                  << std::setw(14) << (ok ? "[OK]" : "[FAIL]") << "\n";
    }
    std::cout << "\n  Ecart max < 0.001 bp (precision machine) : "
              << (all_match ? "[OK]" : "[FAIL]") << "\n";

    // =========================================================================
    section("4. EML(K=100%) = EL bootstrappe du portefeuille");
    // =========================================================================

    // Pour K = 100%, EML doit etre egal a l'expected loss du portefeuille
    // = lgd * (1 - survival_probability(T)) issu du bootstrap
    const double p_T = 1.0 - cc.survival_probability(T);
    const double el  = lgd * p_T;
    std::cout << "\n  EL bootstrappe = lgd * p(T) = " << std::setprecision(8) << el << "\n\n";
    check("AdjBinom EML(K=100%)", adj_uni.expected_min_loss(1.0,T,rho), el, 1e-6);
    check("Recursion EML(K=100%)",rec    .expected_min_loss(1.0,T,rho), el, 1e-6);

    // Test sur plusieurs rho
    std::cout << "\n  Sur differents rho (AdjBinom) :\n";
    // Note: pour rho eleve (>= 0.70), la distribution de Z devient tres etalee
    // et l'integrale GH a 20 noeuds accumule ~1bp d'erreur — comportement normal.
    for (double r : {0.05, 0.20, 0.50}) {
        double eml = adj_uni.expected_min_loss(1.0, T, r);
        check("  EML(100%, rho="+std::to_string(r).substr(0,4)+")", eml, el, 1e-6);
    }
    // Tolerance plus large pour rho eleve : erreur de quadrature GH inherente
    {
        double eml = adj_uni.expected_min_loss(1.0, T, 0.80);
        check("  EML(100%, rho=0.80) [tol=1bp]", eml, el, 1e-4);
    }

    // =========================================================================
    section("5. Par spreads : AdjBinom vs Recursion (ecart attendu < 0.01bp)");
    // =========================================================================

    const Market::YieldCurve& yc_ref = yc;

    std::cout << "\n  N=125, R=40%, rho=30%, T=5Y\n\n";
    std::cout << "  " << std::setw(14) << "Tranche"
              << std::setw(18) << "AdjBinom (bp)"
              << std::setw(18) << "Recursion (bp)"
              << std::setw(16) << "Diff (bp)"
              << std::setw(14) << "Statut"
              << "\n  " << std::string(80,'-') << "\n";

    struct Tr { double K1,K2; const char* name; };
    Tr trs[] = {
        {0.00,0.03,"[0-3%]"},
        {0.03,0.06,"[3-6%]"},
        {0.06,0.09,"[6-9%]"},
        {0.09,0.12,"[9-12%]"},
        {0.12,0.22,"[12-22%]"},
        {0.22,1.00,"[22-100%]"},
    };

    bool spreads_ok = true;
    for (auto& t : trs) {
        auto tr   = make_tr(t.K1, t.K2, 100.0, ref);
        auto grid = Market::build_time_grid(tr, yc_ref);
        double sb = adj_uni.par_spread(tr, grid, rho, rho);
        double sr = rec    .par_spread(tr, grid, rho, rho);
        double diff = (sb-sr)*1e4;
        bool ok = std::abs(diff) < 0.01;
        if (!ok) spreads_ok = false;
        std::cout << "  " << std::setw(14) << t.name
                  << std::setw(18) << std::setprecision(4) << sb*1e4
                  << std::setw(18) << sr*1e4
                  << std::setw(16) << std::setprecision(6) << diff
                  << std::setw(14) << (ok ? "[OK]" : "[FAIL]") << "\n";
    }
    std::cout << "\n  Tous les ecarts < 0.01bp : "
              << (spreads_ok ? "[OK]" : "[FAIL]") << "\n";

    // =========================================================================
    section("6. EML en fonction de rho et K (table)");
    // =========================================================================

    std::cout << "\n  AdjBinom EML — colonnes = K, lignes = rho\n\n";
    const double Ks[]   = {0.03, 0.06, 0.09, 0.12, 0.22, 1.00};
    const double rhos[] = {0.05, 0.20, 0.30, 0.50, 0.70};

    std::cout << "  " << std::setw(8) << "rho\\K";
    for (double K : Ks) std::cout << std::setw(12) << K;
    std::cout << "\n  " << std::string(8+12*6,'-') << "\n";

    for (double r : rhos) {
        std::cout << "  " << std::setw(8) << r;
        for (double K : Ks)
            std::cout << std::setw(12) << std::setprecision(6)
                      << adj_uni.expected_min_loss(K,T,r);
        std::cout << "\n";
    }

    // =========================================================================
    section("7. Timing");
    // =========================================================================
    std::cout << "\n";
    { Timer tm("1000x EML AdjBinom K=6%, rho=30%");
      volatile double d=0;
      for(int i=0;i<1000;++i) d+=adj_uni.expected_min_loss(0.06,T,rho); }

    { Timer tm("1000x EML Recursion K=6%, rho=30%");
      volatile double d=0;
      for(int i=0;i<1000;++i) d+=rec.expected_min_loss(0.06,T,rho); }

    { Timer tm("6 par_spreads AdjBinom x 100 reps");
      volatile double d=0;
      for(int rep=0;rep<100;++rep)
        for(auto& t : trs) {
            auto tr=make_tr(t.K1,t.K2,100.,ref);
            auto g=Market::build_time_grid(tr,yc_ref);
            d+=adj_uni.par_spread(tr,g,rho,rho);
        }
    }

    { Timer tm("6 par_spreads Recursion  x 100 reps");
      volatile double d=0;
      for(int rep=0;rep<100;++rep)
        for(auto& t : trs) {
            auto tr=make_tr(t.K1,t.K2,100.,ref);
            auto g=Market::build_time_grid(tr,yc_ref);
            d+=rec.par_spread(tr,g,rho,rho);
        }
    }

    std::cout << "\n" << std::string(72,'=')
              << "\n  Tests AdjustedBinomialPricer termines.\n"
              << std::string(72,'=') << "\n\n";
    return 0;
}