//
// Created by ricar on 10/04/2026.
//

#pragma once

#include "../../Market/Instruments/CDS/instruments.h"
#include "../../Market/Curves/CDS/CreditCurve.h"
#include "Core/types.h"

namespace Pricer {

    class CDSPricer {

    public:

        CDSPricer(const Market::CDS&          cds,
                  const Market::YieldCurve&   y_curve,
                  const Market::CreditCurve&  credit_curve)
            : m_cds(cds),
              m_y_curve(y_curve),
              m_credit_curve(credit_curve) {
              m_grid = m_cds.buildCDSGrids(1);
        }

        [[nodiscard]] double par_spread()  const;

        [[nodiscard]] double rpv01(Core::Date valuation_date) const;

        [[nodiscard]] Market::CDS::CDSGrids get_grid() const {return m_grid; };

        [[nodiscard]] double npv()         const;

        [[nodiscard]] double default_leg(Core::Date valuation_date) const;

        [[nodiscard]] double upfront()     const;

    private:

        const Market::CDS&         m_cds;
        const Market::YieldCurve&  m_y_curve;
        const Market::CreditCurve& m_credit_curve;
        Market::CDS::CDSGrids m_grid;





    };

}