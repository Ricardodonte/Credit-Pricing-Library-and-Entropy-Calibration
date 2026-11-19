//
// Created by ricar on 15/04/2026.
//

#pragma once

#include <cmath>
#include <stdexcept>

#include "YieldCurve.h"
#include "Core/types.h"
#include "Core/Dates.h"
#include "Market/instruments.h"


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
        double maturity {} ;
        bool quoted_upfront = false ;
        Core::Date effective_date{} ;
        Core::Date valuation_date{} ;
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

    inline TranchesGrid build_time_grid(const Index_tranche &tranche , const YieldCurve & y_curve) {

        Market::TranchesGrid grid ;
        const int freq_month = [&] {
            switch (tranche.frequency) {
                case Core::Frequency::QUARTERLY : return 3;
                case Core::Frequency::SEMI_ANNUAL : return 6;
                case Core::Frequency::ANNUAL : return 12;
                default: throw std::runtime_error("Unknown frequency");
            }
        }();

        const int n_prem = static_cast<int>(std::round(tranche.maturity) * 12 / freq_month  );
        const auto dt_prem = freq_month / 12.0;
        const int n_prot = static_cast<int>(std::round(tranche.maturity) * 12 / 1.0  );


        for (int i = 1 ; i <= n_prem ; ++i) {
            const double t_i = (i==n_prem) ? tranche.maturity : i*dt_prem;
            const double tau = Core::year_fraction(
            tranche.valuation_date.add_months(static_cast<double>((i-1) * freq_month)),
            tranche.valuation_date.add_months(static_cast<double>(i*freq_month)),
            tranche.day_count
            );
            const double t_yf = Core::year_fraction(
                tranche.valuation_date ,
                tranche.valuation_date.add_months(static_cast<double>(i*freq_month)) ,
                tranche.day_count) ;
            grid.premium_times.push_back(t_i);
            grid.premium_accrual.push_back(tau);
            grid.premium_dFactors.push_back( y_curve.discount(t_yf));
        }


        for (int i = 1 ; i <= n_prot ; ++i) {
            const double t_i = (i==n_prot) ? tranche.maturity : i/12.0;
            const double t_yf = Core::year_fraction(
                tranche.valuation_date ,
                tranche.valuation_date.add_months(static_cast<double>(i)) ,
                tranche.day_count) ;
            grid.default_times.push_back(t_i);
            grid.default_dFactors.push_back(y_curve.discount(t_yf));
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