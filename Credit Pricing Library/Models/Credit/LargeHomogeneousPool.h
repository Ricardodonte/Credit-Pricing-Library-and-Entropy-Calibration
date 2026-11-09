// Large homogeneous pool (Vasicek): Gaussian copula with infinitely many identical names.
// The pool is reduced to one average name: total loss given default lgd = sum l_i and loss-weighted default
// probability p = sum l_i p_i / lgd (which keeps E[L] exact). Conditionally on Z the loss is deterministic,
// L(Z) = lgd N((c - sqrt(rho) Z) / sqrt(1 - rho)) with c = N^-1(p), hence the closed form, with k = K / lgd,
//     E[min(L, K)] = lgd N2(c, -A; -sqrt(rho)) + K N(A),   A = (c - sqrt(1 - rho) N^-1(k)) / sqrt(rho).
// No integration: the fastest model, used for quick estimates and as a reference for large indices.

#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>

#include "Core/numericals.h"
#include "Models/Credit/LossModel.h"

namespace Models::Credit {

    class LargeHomogeneousPool {
    public:
        struct state_type {
            double default_probability;  ///< loss-weighted average p
            double loss_given_default;   ///< total loss if every name defaults
            double threshold;            ///< N^-1(p), only meaningful for 0 < p < 1
        };

        [[nodiscard]] state_type prepare(const PoolSnapshot& pool) const {
            double lgd = 0.0, expected_loss = 0.0;
            for (std::size_t i = 0; i < pool.default_probability.size(); ++i) {
                lgd += pool.loss_given_default[i];
                expected_loss += pool.loss_given_default[i] * pool.default_probability[i];
            }
            const double p = lgd > 0.0 ? expected_loss / lgd : 0.0;
            return {p, lgd, (p > 0.0 && p < 1.0) ? Core::norm_inv(p) : 0.0};
        }

        [[nodiscard]] double expected_tranche_loss(const state_type& state, double strike, double rho) const {
            const double p = state.default_probability;
            const double lgd = state.loss_given_default;
            if (strike <= 0.0 || p <= 1e-14) return 0.0;
            if (strike >= lgd) return lgd * p;
            if (p >= 1.0 - 1e-14) return strike;
            if (rho <= 1e-10) return std::min(lgd * p, strike);  // no dispersion: L = lgd p
            if (rho >= 1.0 - 1e-10) return strike * p;           // all or nothing: L = lgd with probability p

            const double beta = std::sqrt(rho);
            const double a = (state.threshold - std::sqrt(1.0 - rho) * Core::norm_inv(strike / lgd)) / beta;
            return lgd * Core::bivariate_norm_cdf(state.threshold, -a, -beta) + strike * Core::norm_cdf(a);
        }
    };

} // namespace Models::Credit
