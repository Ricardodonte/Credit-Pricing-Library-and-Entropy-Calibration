//
// Created by ricar on 15/04/2026.
//

#include "BaseCorrelationCurve.h"

#include <stdexcept>

namespace Market {

    void BaseCorrelationCurve::add_point(double const K, const double rho) {

        if (rho < 0.0 || rho > 1.0)
            throw std::invalid_argument("rho should be between 0 and 1.");
        const auto it = std::lower_bound( m_rhos_curve.begin(), m_rhos_curve.end(), K ,
            [](const BaseCorrPoint & p , const double val ) {return p.K  < val;});
        m_rhos_curve.insert(it, {K,rho});
    }

    double BaseCorrelationCurve::rho(const double K) const {  /// This type of interpolation is not good, some arbitrage constrains
        // may be violated

        if (m_rhos_curve.empty())
            throw std::invalid_argument("rhos_curve is empty");

        if (K<=m_rhos_curve.front().K) return m_rhos_curve.front().rho;

        if (K>=m_rhos_curve.back().K) return m_rhos_curve.back().rho;

        const auto it = std::lower_bound(m_rhos_curve.begin(), m_rhos_curve.end(), K ,
            [] (const BaseCorrPoint & p , const double val) {return p.K  < val;});

        const auto &p1 = *std::prev(it);
        const auto &p2 = *it;
        const double alpha = (K - p1.K) / (p2.K - p1.K);
        return p1.rho + alpha * (p2.rho - p1.rho);

    }

     
}
