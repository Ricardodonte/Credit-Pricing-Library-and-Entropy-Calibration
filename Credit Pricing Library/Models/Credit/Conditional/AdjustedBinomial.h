// Adjusted binomial approximation of the conditional loss (Hull-White style variance matching).
// The n heterogeneous names are replaced by n identical names of average loss a = sum l_i / n and loss-weighted
// default probability p = sum l_i p_i / sum l_i: the number of defaults D is Binomial(n, p), which matches the
// conditional expected loss exactly. Heterogeneity lowers the variance (in default-count units):
//     sigma_A^2 = n p (1 - p)               (binomial)
//     sigma_E^2 = sum l_i^2 p_i (1 - p_i) / a^2   (exact)
// The variance is matched by moving a mass 1 - alpha of the binomial onto the two integers around the mean
// m = n p, q = floor(m) and q + 1, keeping the mean unchanged:
//     f = alpha Binomial + eps_q delta_q + eps_q1 delta_{q+1},  eps_q = (1 - alpha)(q + 1 - m),  eps_q1 = (1 - alpha)(m - q)
//     alpha = (sigma_E^2 - c) / (sigma_A^2 - c),  c = (m - q)(q + 1 - m)   (variance of the two-point mass)
// alpha is clamped to [0, 1]: when the exact variance is below c, the variance cannot be matched with
// non-negative probabilities. For a homogeneous pool alpha = 1 and the result is exact.

#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>

namespace Models::Credit {

    struct AdjustedBinomial {
        [[nodiscard]] double expected_tranche_loss(std::span<const double> conditional_pd,
                                                   std::span<const double> loss_given_default,
                                                   double strike) const noexcept {
            const std::size_t n = conditional_pd.size();
            double total_loss = 0.0, expected_loss = 0.0, exact_variance = 0.0;
            for (std::size_t i = 0; i < n; ++i) {
                const double p = conditional_pd[i];
                const double l = loss_given_default[i];
                total_loss += l;
                expected_loss += l * p;
                exact_variance += l * l * p * (1.0 - p);
            }
            if (strike <= 0.0 || total_loss <= 0.0) return 0.0;

            const double p = expected_loss / total_loss;
            if (p <= 1e-14) return 0.0;
            if (p >= 1.0 - 1e-14) return std::min(total_loss, strike);
            if (strike >= total_loss) return expected_loss;

            const auto nd = static_cast<double>(n);
            const double unit = total_loss / nd;  // loss of one default in the binomial
            const double mean = nd * p;
            const double q = std::floor(mean);
            const double c = (mean - q) * (q + 1.0 - mean);
            const double binomial_variance = nd * p * (1.0 - p);
            const double target_variance = exact_variance / (unit * unit);

            double alpha = 1.0;
            if (std::abs(binomial_variance - c) > 1e-14)
                alpha = std::clamp((target_variance - c) / (binomial_variance - c), 0.0, 1.0);
            const double eps_q = (1.0 - alpha) * (q + 1.0 - mean);
            const double eps_q1 = (1.0 - alpha) * (mean - q);
            const auto q_index = static_cast<std::size_t>(q);

            // E[min(D a, K)] = sum_{k a < K} k a f(k) + K P(D a >= K). Binomial pmf in logs (no underflow).
            const double log_odds = std::log(p) - std::log1p(-p);
            double log_pmf = nd * std::log1p(-p);
            double result = 0.0, below = 0.0;
            for (std::size_t k = 0; k <= n; ++k) {
                if (k > 0) log_pmf += log_odds + std::log((nd - static_cast<double>(k) + 1.0) / static_cast<double>(k));
                const double loss = static_cast<double>(k) * unit;
                if (loss >= strike) break;
                double f = alpha * std::exp(log_pmf);
                if (k == q_index) f += eps_q;
                if (k == q_index + 1) f += eps_q1;
                result += loss * f;
                below += f;
            }
            return std::max(result + strike * (1.0 - below), 0.0);
        }
    };

} // namespace Models::Credit
