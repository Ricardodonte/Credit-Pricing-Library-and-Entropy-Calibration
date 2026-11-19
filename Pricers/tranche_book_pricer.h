//
// Created by ricar on 17/04/2026.
//

#pragma  once
#include "base_tranche_pricer.h"

namespace Pricer {
    class TrancheBookPricer {

    public :

        TrancheBookPricer( BaseTranchePricer &engine , const Market::BaseCorrelationCurve& base_corr_curve , const Market::YieldCurve& y_curve ) :
        m_engine(engine) , m_ccurve(base_corr_curve) , m_y_curve(y_curve) {}

        [[nodiscard]] Market::TranchePricingResult price (const Market::Index_tranche &tranche ) const ;


    private :
        BaseTranchePricer &m_engine ;
        const Market::BaseCorrelationCurve & m_ccurve ;
        const Market::YieldCurve & m_y_curve ;



    };

}
