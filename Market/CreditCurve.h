//
// Created by ricar on 10/04/2026.
//

#pragma once
#include <string>
#include <vector>

#include "instruments.h"
#include "YieldCurve.h"
#include "Core/types.h"


namespace  Market {

    class CreditCurve {

    public:

        CreditCurve(std::string name_id , const Core::Date ref_Date) {
            m_name_id = std::move(name_id);
            m_ref_Date = ref_Date;
            intensity = {};
        }

        [[nodiscard]] double survival_probability(double t) const;
        [[nodiscard]] double survival_probability(double t, double T) const;



    private :

        std::string m_name_id;
        std::vector<Core::Point> intensity;
        Core::Date m_ref_Date;

        friend class CreditBoot;

    };

    class CreditBoot {

    public:

        CreditBoot(CDSMarketData MarketData, const YieldCurve& yieldCurve, const Core::Date ref_Date)
            : m_MarketData(std::move(MarketData)),
              m_creditCurve(m_MarketData.name, ref_Date),
              m_yieldCurve(yieldCurve),
              m_solver(1e-8, 50)
        {
            bootstrap();
        }

        [[nodiscard]] const std::vector<Core::Point>& repricing_errors() const {
            return m_repricing_errors;
        }

        [[nodiscard]] const CreditCurve& curve() const { return m_creditCurve; }
        

    private:

        const CDSMarketData  m_MarketData;
        CreditCurve          m_creditCurve;
        const YieldCurve&    m_yieldCurve;
        Core::Brent  m_solver;
        std::vector<Core::Point> m_repricing_errors;

        void bootstrap();

        [[nodiscard]] std::pair<std::vector<double>,std::vector<double>> discount_grid(const CDS &cds, const CDS::CDSGrids &grid) const;
        [[nodiscard]] double par_spread(const CDS &cds, const CDS::CDSGrids &grid ,
            const std::pair<std::vector<double> , std::vector<double>> &df_grid) const;
    };


};

