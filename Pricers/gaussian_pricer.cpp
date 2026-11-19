//
// Created by ricar on 17/04/2026.
//

#include "gaussian_pricer.h"

namespace Pricer {
    std::vector<double> GaussianPricer::default_probability(const std::vector<Market::CreditCurve> &curves, const double t, const double Z, const double rho) const {

        const auto n = static_cast<std::size_t>(m_credits);
        std::vector<double> default_prob ;
        default_prob.reserve(n);

        for (std::size_t i = 0; i < n; ++i) {
            const double p_i = 1.0 - curves[i].survival_probability(t);
            if (p_i <= 1e-12)      { default_prob.push_back(0.0); continue; }
            if (p_i >= 1.0-1e-12)  { default_prob.push_back(1.0); continue; }
            const double C_t = Core::norm_inv(p_i);
            default_prob.push_back(Core::norm_cdf(( C_t - std::sqrt(rho) * Z) / sqrt(1 - rho)) ) ;
        }
        return default_prob ;
    }

    std::pair<double, double> GaussianPricer::mean_var(const std::vector<double> &pd) const {

        double mean = 0 , variance = 0 ;
        const auto n = static_cast<std::size_t>(m_credits);
        for (std::size_t i = 0; i < n; ++i) {
            const double lgd = 1.0 - m_recovery_rates[i];
            mean += pd[i] * lgd;
            variance += pd[i]*(1.0 - pd[i])*lgd*lgd;
        }
        mean /= static_cast<double>(n);
        variance /= (static_cast<double>(n) * static_cast<double>(n));
        return {mean, sqrt (std::max(variance , 0.0) ) } ;

    }


    double GaussianPricer::expected_min_loss(const double K, const double t, const double rho) const {
        if (K <= 0)
            return 0.0 ;

        double E_min = 0.0 ;
        constexpr auto GH_number = static_cast<std::size_t>(Core::N_GH) ;

        for (std::size_t gi = 0 ; gi < GH_number ; gi++) {

            const double Z = std::sqrt(2.0) * Core::GH_NODES[gi];
            const double w = Core::GH_WEIGHTS[gi] / std::sqrt(MathConstants::PI) ;

            const std::vector<double> dp = default_probability(m_credit_curves , t , Z , rho) ;
            auto [mean , std_error] = mean_var(dp) ;

            double E_given_Z;

            if (std_error < 1e-12) {
                E_given_Z = std::min(mean, K);
            }
            else {
                const double alpha = (K - mean) / std_error ;
                E_given_Z = mean * Core::norm_cdf(alpha) -
                            std_error * Core::norm_pdf(alpha)
                            + K * (1.0 - Core::norm_cdf(alpha)) ;
            }

            E_min += w * E_given_Z;
        }

        return std::max(E_min, 0.0);
    }

}

