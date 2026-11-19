//
// Created by ricar on 11/05/2026.
//

#pragma once
#include <utility>
#include <stdexcept>

#include "bumper.h"
#include "Core/Dates.h"
#include "Market/Curves/CDS/CreditCurve.h"
#include "Market/Curves/YieldCurve/YieldCurve.h"
#include "Market/Instruments/CDS/instruments.h"

namespace Risk {

    struct CDSRiskResults {
        double MtM_value;
        double Credit_DV01;
        double InterestRate_DV01;
        double SpreadGamma;
        double Theta;
    };


    class CDSRiskEngine {

    public :

        CDSRiskEngine(Market::CDSMarketData  mdata, Market::CreditCurve  creditCurve,
            Market::YieldCurve  yieldCurve, const Core::Date& ref_date ) :
            m_mdata(std::move(mdata)), m_creditCurve(std::move(creditCurve)), m_yieldCurve(std::move(yieldCurve)),
            m_ref_date(ref_date) {
            if (m_mdata.quotes.empty()) {
            throw std::runtime_error("CDSRiskEngine: No quotes in market data");
            }
        }

        [[nodiscard]] CDSRiskResults CDSGreeks(const Market::CDS& cds , const CDSBumpConfig &config = {}) const;

    private :
        const Market::CDSMarketData m_mdata;
        const Market::CreditCurve m_creditCurve;
        const Market::YieldCurve m_yieldCurve;
        const Core::Date m_ref_date;

        [[nodiscard]] Market::CreditCurve build_CreditCurve_Parallel_Shift(double h_decimal) const ;
        [[nodiscard]] Market::YieldCurve build_YieldCurve_Parallel_Shift(double h_decimal) const ;
        [[nodiscard]] Market::CreditCurve build_Bucket_CreditCurve_Shift(double h_decimal, std::size_t pillar_idx) const ;

    };

}
