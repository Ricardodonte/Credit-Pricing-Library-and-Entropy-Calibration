//
// Created by ricar on 16/04/2026.
//

#include "lhp_pricer.h"

namespace Pricer {

    double lhp_pricer::expected_min_loss(const double K, const double t, const double rho) const {

        if (K<=0) return 0.0 ;

        const double p_t = 1.0 - m_index_curve.survival_probability(t);
        if (p_t < 1e-12) return 0.0;
        const double lgd = 1.0 - m_recovery_rate;

        const double K_norm = K / lgd;
        if (K_norm >= 1.0 - 1e-12) return lgd * p_t;

        if (rho < 1e-10) return std::min(lgd * p_t, K);

        if (rho > 1.0 - 1e-10)
                return std::min(lgd, K) * p_t;

        const double C_t  = Core::norm_inv(p_t);
        const double beta = std::sqrt(rho);
        const double A_K = (C_t - std::sqrt(1.0 - rho) * Core::norm_inv(K_norm)) / beta;

        return lgd * Core::bivariate_norm_cdf(C_t, -A_K, -beta) + K * Core::norm_cdf(A_K);
    }

}


