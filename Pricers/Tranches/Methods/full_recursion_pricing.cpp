//
// Created by ricar on 16/04/2026.
//

#include "full_recursion_pricing.h"

namespace Pricer {


    double RecursionPricer::cond_pd(const double C_t, const double Z, const double rho) noexcept {

        return Core::norm_cdf((C_t - std::sqrt(rho) * Z) / sqrt(1 - rho)) ;

    }

    double RecursionPricer::expected_min_loss(const double K, double t, const double rho) const {

        if (K <= 0)
            return 0.0 ;

        const int g = std::min(
            static_cast<int>(std::ceil( K / m_unit ))
            , m_n_credit );

        std::vector<double> C_thresh(static_cast<std::size_t>(m_n_credit));
        for (int j = 0; j < m_n_credit; ++j) {
            const double p_j = 1.0 - m_credit_curves[static_cast<std::size_t>(j)]
                                          .survival_probability(t);
            if (p_j <= 1e-12)      C_thresh[j] = -10.0;
            else if (p_j >= 1.0-1e-12) C_thresh[j] =  10.0;
            else                   C_thresh[j] = Core::norm_inv(p_j);
        }

        std::vector<double> f_buf(static_cast< std::size_t>(g+1)) ;

        double E_min = 0.0 ;
        constexpr int GH_number =  Core::N_GH ;

        for (std::size_t gi = 0; gi < Core::N_GH; ++gi) {

            const double Z = std::sqrt(2.0) * Core::GH_NODES[gi];
            const double w = Core::GH_WEIGHTS[gi] / std::sqrt(MathConstants::PI);

            std::ranges::fill(f_buf, 0.0);
            f_buf[0] = 1.0;

            for (int j = 1; j <= m_n_credit; ++j) {
                const double pd = cond_pd(C_thresh[static_cast<std::size_t>(j)-1], Z, rho);
                const double q  = 1.0 - pd;
                const int upper = std::min(j, g);

                for (int k = upper; k >= 1; --k)
                    f_buf[k] = f_buf[k] * q + f_buf[k-1] * pd;
                f_buf[0] *= q;
            }

            double ev      = 0.0;
            double lower_than_k = 0.0;
            for (int k = 0; k <= g; ++k) {
                const double loss_k = k * m_unit;
                ev      += (loss_k < K ? loss_k : K) * f_buf[static_cast<std::size_t>(k)];
                lower_than_k += f_buf[static_cast<std::size_t>(k)];
            }

            ev += K * (1.0 - lower_than_k);
            E_min += w * ev;
        }

        return E_min ;
    }


}
