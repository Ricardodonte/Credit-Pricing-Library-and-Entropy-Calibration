//
// Created by ricar on 15/04/2026.
//

#pragma once

#include <cmath>
#include <stdexcept>

#include "../../Curves/YieldCurve/YieldCurve.h"
#include "Core/types.h"
#include "Core/Dates.h"
#include "../CDS/instruments.h"


namespace Market {
    class BaseCorrelationCurve;

    struct BaseCorrPoint {
        double K;
        double rho;
    };

    struct Index_tranche {

        double K1{} ;
        double K2{} ;
        double contractual_spread{} ;
        double upfront {};
        double fair_spread{} ;
        double nominal{}; // The tranche nominal value = Nominal total * (K2 - K1), that's the assumption i'm working with
        bool quoted_upfront = false ;
        Core::Date effective_date{} ;
        Core::Date valuation_date{} ;
        Core::Date MaturityDate{};
        Core::Frequency frequency{} ;
        Core::DayCount day_count{} ;

    };

    struct Tranches_MarketData {
        std::string index_name ;
        int n_credits ;
        double recovery_rate ;
        std::vector<Index_tranche> quoted_tranches ;
    };

    struct TranchesGrid {
        std::vector<double> premium_times;
        std::vector<double> premium_accrual;
        std::vector<double> premium_dFactors;
        std::vector<double> default_times;
        std::vector<double> default_dFactors;
    };

    inline TranchesGrid build_time_grid(const Index_tranche& tranche,
                                        const YieldCurve&    y_curve)
    {
        TranchesGrid grid;

        // Maturité fournie directement par l'utilisateur (date IMM ISDA)
        const Core::Date mat_date = tranche.MaturityDate;

        // ── Default leg : mensuel, comme buildCDSGrids(1) ────────────────────
        {
            Core::Date cur = tranche.valuation_date;
            while (true) {
                cur = cur.add_months(1);
                if (cur >= mat_date) {
                    // Dernier pilier = date de maturité exacte
                    const double t_yf = Core::year_fraction(
                        tranche.valuation_date, mat_date, tranche.day_count);
                    grid.default_times  .push_back(t_yf);
                    grid.default_dFactors.push_back(y_curve.discount(t_yf));
                    break;
                }

                const double t_yf = Core::year_fraction(
                    tranche.valuation_date, cur, tranche.day_count);
                grid.default_times  .push_back(t_yf);
                grid.default_dFactors.push_back(y_curve.discount(t_yf));
            }
        }

        // ── Premium leg : dates IMM trimestrielles ────────────────────────────
        {
            Core::Date prev = tranche.valuation_date;
            Core::Date cur  = CDS::firstIMMOnOrAfter(tranche.valuation_date);

            while (cur < mat_date) {
                const double tau  = Core::year_fraction(prev, cur, tranche.day_count);
                const double t_yf = Core::year_fraction(
                    tranche.valuation_date, cur, tranche.day_count);

                grid.premium_times   .push_back(t_yf);
                grid.premium_accrual .push_back(tau);
                grid.premium_dFactors.push_back(y_curve.discount(t_yf));

                prev = cur;

                // Roll trimestriel — exactement comme buildCDSGrids
                auto [y, m, d] = cur.toGregorian();
                m += 3;
                if (m > 12) { m -= 12; y++; }
                cur = Core::Date(y, m, 20);
            }

            // Dernier coupon = maturité (date IMM finale)
            {
                const double tau  = Core::year_fraction(prev, mat_date, tranche.day_count);
                const double t_yf = Core::year_fraction(
                    tranche.valuation_date, mat_date, tranche.day_count);
                grid.premium_times   .push_back(t_yf);
                grid.premium_accrual .push_back(tau);
                grid.premium_dFactors.push_back(y_curve.discount(t_yf));
            }
        }

        return grid;
    }

    struct TranchePricingResult {

        double par_spread ;
        double npv ;
        double protection_leg_pv ;
        double premium_leg_pv ;
        double rho_1;
        double rho_2;
        std::string pricer_name;
    };






}