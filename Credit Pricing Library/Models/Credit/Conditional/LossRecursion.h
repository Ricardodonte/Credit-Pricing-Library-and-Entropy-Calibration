// Exact conditional loss distribution by recursion (Andersen, Sidenius and Basu, 2003).
// Losses are expressed on a lattice: u = min_i l_i / resolution and each name loses n_i = round(l_i / u) units
// (exact for a homogeneous pool with resolution 1). Adding the names one by one,
//     f_j(k) = f_{j-1}(k) (1 - p_j) + f_{j-1}(k - n_j) p_j,
// gives the distribution of the loss in units. Only E[min(L, K)] is needed, so the lattice is truncated at
// g = ceil(K / u): the last bucket accumulates P(L >= g u). Cost O(names x g), independent of the pool size
// beyond the strike.

#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <vector>

namespace Models::Credit {

    class LossRecursion {
    public:
        LossRecursion() = default;
        /// @param resolution number of lattice units for the smallest name loss (1 is exact for homogeneous pools;
        ///        raise it for heterogeneous recoveries or weights).
        explicit LossRecursion(int resolution) : resolution_(resolution) {
            if (resolution < 1) throw std::invalid_argument("LossRecursion: resolution must be at least 1");
        }

        [[nodiscard]] double expected_tranche_loss(std::span<const double> conditional_pd,
                                                   std::span<const double> loss_given_default, double strike) const {
            if (strike <= 0.0 || loss_given_default.empty()) return 0.0;
            const double unit = *std::ranges::min_element(loss_given_default) / resolution_;
            if (!(unit > 0.0)) throw std::invalid_argument("LossRecursion: losses given default must be positive");

            const auto g = static_cast<std::size_t>(std::ceil(strike / unit - 1e-12));
            std::vector<double> f(g + 1, 0.0);  // f[k] = P(L = k u) for k < g, f[g] = P(L >= g u)
            f[0] = 1.0;
            for (std::size_t i = 0; i < conditional_pd.size(); ++i) {
                const double p = conditional_pd[i];
                const double q = 1.0 - p;
                const auto n = static_cast<std::size_t>(std::max(1.0, std::round(loss_given_default[i] / unit)));
                // Absorbing bucket first, with the old values of f[g - n .. g - 1].
                double into_tail = 0.0;
                for (std::size_t k = (g > n ? g - n : 0); k < g; ++k) into_tail += f[k];
                f[g] += p * into_tail;
                // Descending order: f[k - n] is still the old value when f[k] is updated.
                for (std::size_t k = g; k-- > 0;) f[k] = f[k] * q + (k >= n ? f[k - n] * p : 0.0);
            }

            double result = 0.0;
            for (std::size_t k = 0; k < g; ++k) result += static_cast<double>(k) * unit * f[k];
            return result + strike * f[g];
        }

    private:
        int resolution_ = 1;
    };

} // namespace Models::Credit
