//
// Created by ricar on 16/04/2026.
//

#include "base_tranche_pricer.h"

#include <cmath>


namespace Pricer {

    double BaseTranchePricer::tranche_survival(const double t, const double K1, const double K2,
        const double rho1, const double rho2) const {

        const double el_K2  = expected_min_loss(K2 , t , rho2);
        const double el_K1  = expected_min_loss(K1 , t , rho1);
        const double el = ( el_K2 - el_K1) / (K2 -K1);
        return 1.0 - el;
    }


    double BaseTranchePricer::base_tranche_survival(const double t, const double K2, const double rho) const {
        const double el_K2  = expected_min_loss(K2 , t , rho);
        return 1.0 - el_K2 / K2;
    }

    double BaseTranchePricer::premium_leg(const Market::Index_tranche &tranche, const Market::TranchesGrid &grid,
        const double rho1, const double rho2) const {

        double pv = 0;
        const std::size_t N = grid.premium_times.size();
        double Q_prev = 1.0;

        for (std::size_t i = 0; i < N; ++i) {
            const double t_i = grid.premium_times[i];
            const double delta = grid.premium_accrual[i];
            const double DF_i = grid.premium_dFactors[i];
            const double Q_i = tranche_survival(t_i, tranche.K1 , tranche.K2 , rho1 , rho2) ;

            pv += delta * DF_i * (Q_i + Q_prev) ;
            Q_prev = Q_i ;
        }

        if (tranche.quoted_upfront) {
            return tranche.upfront* tranche.nominal +  tranche.contractual_spread * 0.5 * pv * tranche.nominal;
        }

        return tranche.fair_spread * 0.5 * pv * tranche.nominal;
    }

    double BaseTranchePricer::protection_leg(const Market::Index_tranche &tranche, const Market::TranchesGrid &grid,
        const double rho1, const double rho2) const {

        double pv = 0;
        double Q_prev = 1.0;
        const std::size_t N = grid.default_times.size();
        double DF_prev = 1.0;

        for (std::size_t i = 0; i < N; ++i) {
            const double t_i = grid.default_times[i];
            const double DF_i = grid.default_dFactors[i];
            const double Q_i = tranche_survival(t_i , tranche.K1 , tranche.K2 , rho1 , rho2);

            pv += 0.5*(DF_prev + DF_i ) * (Q_prev-Q_i);
            Q_prev = Q_i ;
            DF_prev = DF_i ;
        }

        return pv * tranche.nominal;

    }

    double BaseTranchePricer::rpv01(const Market::Index_tranche &tranche, const Market::TranchesGrid &grid,
        const double rho1, const double rho2) const {

        double risk_premium = 0;
        const std::size_t N = grid.premium_times.size();
        double Q_prev = 1.0;

        for (std::size_t i = 0; i < N; ++i) {
            const double t_i = grid.premium_times[i];
            const double delta = grid.premium_accrual[i];
            const double DF_i = grid.premium_dFactors[i];
            const double Q_i = tranche_survival(t_i, tranche.K1 , tranche.K2 , rho1 , rho2) ;

            risk_premium += delta * DF_i * (Q_i + Q_prev) ;
            Q_prev = Q_i ;
        }

        return 0.5 * risk_premium * tranche.nominal;

    }

    double BaseTranchePricer::npv(const Market::Index_tranche &tranche, const Market::TranchesGrid &grid,
                                  const double rho1, const double rho2) const
    {
        return premium_leg(tranche , grid , rho1 , rho2) - protection_leg(tranche , grid , rho1 , rho2) ;
    }

    double BaseTranchePricer::par_spread(const Market::Index_tranche &tranche, const Market::TranchesGrid &grid,
        const double rho1, const double rho2) const {

        const double risk_premium = rpv01(tranche , grid, rho1 , rho2) ;
        const double default_leg = protection_leg(tranche , grid , rho1 , rho2) ;

        return default_leg / risk_premium ;
    }

    }




