// Conditional loss approximated by a normal law with the exact conditional mean and variance:
//     mu = sum l_i p_i,   sigma^2 = sum l_i^2 p_i (1 - p_i)
//     E[min(X, K)] = mu N(a) - sigma n(a) + K (1 - N(a)),   a = (K - mu) / sigma
// Fast (O(names)), accurate for large and granular pools, poor for small or concentrated ones.

#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>

#include "Core/numericals.h"

namespace Models::Credit {

    struct NormalApproximation {
        [[nodiscard]] double expected_tranche_loss(std::span<const double> conditional_pd,
                                                   std::span<const double> loss_given_default,
                                                   double strike) const noexcept {
            double mean = 0.0, variance = 0.0;
            for (std::size_t i = 0; i < conditional_pd.size(); ++i) {
                const double p = conditional_pd[i];
                const double l = loss_given_default[i];
                mean += l * p;
                variance += l * l * p * (1.0 - p);
            }
            if (variance < 1e-28) return std::min(mean, strike);
            const double sigma = std::sqrt(variance);
            const double a = (strike - mean) / sigma;
            return mean * Core::norm_cdf(a) - sigma * Core::norm_pdf(a) + strike * Core::norm_cdf(-a);
        }
    };

} // namespace Models::Credit
