//
// Created by ricar on 15/04/2026.
//

#pragma once
#include <vector>

#include "tranches_instruments.h"

namespace Market {

    class BaseCorrelationCurve {

    public:

        BaseCorrelationCurve() = default;

        void add_point (double K, double rho);
        [[nodiscard]] double rho(double K) const ;
        [[nodiscard]] const std::vector<BaseCorrPoint> &rho_curve() const {return m_rhos_curve;}
        [[nodiscard]] std::size_t rho_curve_size() const {return m_rhos_curve.size();}

    private :
        std::vector<BaseCorrPoint> m_rhos_curve;
    };

    struct BaseCorrBootstrapResult {
        BaseCorrelationCurve curve ;
        std::vector<double> par_spreads ;
        std::vector<double> residuals ;
    };
    
}
