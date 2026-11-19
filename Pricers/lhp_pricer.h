//
// Created by ricar on 16/04/2026.
//

#pragma once
#include "base_tranche_pricer.h"
#include "Market/CreditCurve.h"

namespace Pricer {

    class lhp_pricer : public BaseTranchePricer {

        public:

            lhp_pricer(const Market::CreditCurve & index_curve , const double recovery_rate ) :
            m_index_curve(index_curve) , m_recovery_rate(recovery_rate)
            {}

            [[nodiscard]] double expected_min_loss(double K, double t, double rho) const override;

        private :
            const Market::CreditCurve &m_index_curve;
            double m_recovery_rate ;
        
    };

}

