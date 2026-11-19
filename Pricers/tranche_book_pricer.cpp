//
// Created by ricar on 17/04/2026.
//

#include "tranche_book_pricer.h"

#include "Market/BaseCorrelationCurve.h"

namespace Pricer {

    Market::TranchePricingResult TrancheBookPricer::price(const Market::Index_tranche &tranche) const {
        const Market::TranchesGrid grid = Market::build_time_grid(tranche,m_y_curve);
        const double rho_1 = (tranche.K1 < 1e-12) ? 0.0 : m_ccurve.rho(tranche.K1) ;
        const double rho_2 = m_ccurve.rho(tranche.K2);

        Market::TranchePricingResult res;
        res.rho_1 = rho_1;
        res.rho_2 = rho_2;

        res.protection_leg_pv = m_engine.protection_leg(tranche,grid,rho_1,rho_2);
        res.premium_leg_pv = m_engine.premium_leg(tranche,grid,rho_1,rho_2);
        res.npv = res.premium_leg_pv - res.protection_leg_pv ;
        res.par_spread = m_engine.par_spread(tranche, grid,rho_1,rho_2);

        return res;


    }
    



}
