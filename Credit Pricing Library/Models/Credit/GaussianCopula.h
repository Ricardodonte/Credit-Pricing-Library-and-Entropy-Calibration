// One-factor Gaussian copula (Li, Vasicek), the market standard behind base correlation.
// Name i defaults before t when sqrt(rho) Z + sqrt(1 - rho) eps_i <= c_i(t) = N^-1(p_i(t)), Z the common factor.
// Conditionally on Z the defaults are independent with probabilities
//     p_i(Z) = N((c_i - sqrt(rho) Z) / sqrt(1 - rho)),
// and E[min(L, K)] = E_Z[ E[min(L, K) | Z] ]. The conditional expectation is delegated to a ConditionalLossModel
// (NormalApproximation, AdjustedBinomial, LossRecursion...): the copula does not know how the conditional
// distribution is computed. The integral over Z is split in three (see FactorQuadrature.h):
//     Z < lo        every name with a finite threshold has defaulted: the integrand is constant
//     lo < Z < hi   adaptive composite Gauss-Legendre, resolving the transition whatever the correlation
//     Z > hi        every such name has survived: constant again
// with lo = (min c_i - 8.3 sigma) / beta and hi = (max c_i + 8.3 sigma) / beta (N(-8.3) = 5e-17).
// prepare() computes the thresholds c_i(t) once per date: inside the base correlation solver, only rho changes.

#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

#include "Core/numericals.h"
#include "Models/Credit/FactorQuadrature.h"
#include "Models/Credit/LossModel.h"

namespace Models::Credit {

    template <ConditionalLossModel Conditional>
    class GaussianCopula {
    public:
        /// @brief Pool at one date: default thresholds and losses given default.
        struct state_type {
            std::vector<double> threshold;  ///< c_i = N^-1(p_i), +-infinity for p_i = 0 or 1
            std::vector<double> loss_given_default;
            double min_threshold;           ///< extreme finite thresholds (bounds of the non-degenerate region)
            double max_threshold;
            bool has_finite_threshold;
        };

        GaussianCopula() = default;
        explicit GaussianCopula(Conditional conditional, FactorQuadrature quadrature = {})
            : conditional_(std::move(conditional)), quadrature_(quadrature) {
            quadrature_.validate();
        }

        [[nodiscard]] const Conditional& conditional_model() const noexcept { return conditional_; }
        [[nodiscard]] const FactorQuadrature& quadrature() const noexcept { return quadrature_; }

        [[nodiscard]] state_type prepare(const PoolSnapshot& pool) const {
            constexpr double inf = std::numeric_limits<double>::infinity();
            state_type state{{}, {}, inf, -inf, false};
            state.threshold.reserve(pool.default_probability.size());
            for (const double p : pool.default_probability) {
                const double c = p <= 0.0 ? -inf : p >= 1.0 ? inf : Core::norm_inv(p);
                state.threshold.push_back(c);
                if (std::isfinite(c)) {
                    state.min_threshold = std::min(state.min_threshold, c);
                    state.max_threshold = std::max(state.max_threshold, c);
                    state.has_finite_threshold = true;
                }
            }
            state.loss_given_default.assign(pool.loss_given_default.begin(), pool.loss_given_default.end());
            return state;
        }

        /// @brief E[min(L, K)] at the date of `state`, for the flat correlation rho in [0, 1].
        [[nodiscard]] double expected_tranche_loss(const state_type& state, double strike, double rho) const {
            if (strike <= 0.0) return 0.0;
            rho = std::clamp(rho, 0.0, max_correlation);
            const double beta = std::sqrt(rho);
            const double sigma = std::sqrt(1.0 - rho);
            const double inv_sigma = 1.0 / sigma;

            std::vector<double> conditional_pd(state.threshold.size());
            const auto conditional_loss = [&](double z) {
                const double shift = beta * z;
                for (std::size_t i = 0; i < conditional_pd.size(); ++i)
                    conditional_pd[i] = Core::norm_cdf((state.threshold[i] - shift) * inv_sigma);
                return conditional_.expected_tranche_loss(conditional_pd, state.loss_given_default, strike);
            };
            // Independent defaults, or nothing random left: the integrand does not depend on Z.
            if (beta < 1e-12 || !state.has_finite_threshold) return conditional_loss(0.0);

            constexpr double tail = 8.3;
            const double bound = quadrature_.bound;
            const double lo = std::clamp((state.min_threshold - tail * sigma) / beta, -bound, bound);
            const double hi = std::clamp((state.max_threshold + tail * sigma) / beta, lo, bound);
            return conditional_loss(lo) * Core::norm_cdf(lo) + conditional_loss(hi) * Core::norm_cdf(-hi)
                 + FactorQuadrature::integrate(conditional_loss, lo, hi, quadrature_.step(sigma / beta));
        }

        /// @brief Correlations are capped just below 1, where the conditional law degenerates.
        static constexpr double max_correlation = 1.0 - 1e-10;

    private:
        Conditional conditional_{};
        FactorQuadrature quadrature_{};
    };

} // namespace Models::Credit
