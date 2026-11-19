//
// Created by ricar on 16/04/2026.
//

#pragma once
#include "../../../Market/Instruments/Tranches/tranches_instruments.h"


namespace Pricer {

    class BaseTranchePricer {

        public:

            virtual ~BaseTranchePricer() = default;

            [[nodiscard]] virtual double expected_min_loss(double K, double t, double rho) const = 0;

            [[nodiscard]] double tranche_survival(double t, double K1, double K2 , double rho1 , double rho2) const;

            [[nodiscard]] double base_tranche_survival(double t, double K2 , double rho) const;

            [[nodiscard]] double premium_leg(const Market::Index_tranche &tranche, const Market::TranchesGrid & grid ,
                double rho1 , double rho2) const;

            [[nodiscard]] double protection_leg (const Market::Index_tranche &tranche,const Market::TranchesGrid & grid
                , double rho1 , double rho2) const;

            [[nodiscard]] double npv(const Market::Index_tranche &tranche, const Market::TranchesGrid & grid ,
                double rho1 , double rho2) const;

            [[nodiscard]] double rpv01(const Market::Index_tranche &tranche, const Market::TranchesGrid & grid ,
                double rho1 , double rho2) const;

            [[nodiscard]] double par_spread(const Market::Index_tranche &tranche ,const Market::TranchesGrid & grid
                , double rho1 , double rho2 ) const;
    };

}
