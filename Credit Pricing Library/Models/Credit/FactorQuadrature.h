// Integration over the common factor Z ~ N(0, 1) of one-factor copulas.
// Conditionally on Z, the default probability of a name is N((c - sqrt(rho) Z) / sqrt(1 - rho)): a smooth step
// in Z of width s = sqrt(1 - rho) / sqrt(rho), which becomes very steep for the correlations of senior tranches
// (s = 0.33 at rho = 0.9). Fixed rules fail there (measured on E[min(L, 3%)], 125 names, rho = 0.9: 20-point
// Gauss-Hermite 5% error, 160-point Gauss-Legendre 1%). Hence an adaptive rule:
//  - composite 10-point Gauss-Legendre, with intervals no wider than resolution x s (and max_step): the step is
//    resolved whatever the correlation (relative error below 1e-5 from rho = 0.1 to 0.99 with the defaults);
//  - only on the region where the conditional law is not degenerate: outside, every name has (numerically)
//    defaulted or survived, and the integrand is a known constant, integrated in closed form by the caller.

#pragma once
#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "Core/numericals.h"

namespace Models::Credit {

    struct FactorQuadrature {
        double bound = 6.0;       ///< the factor is truncated to [-bound, bound] (P(|Z| > 6) = 2e-9)
        double resolution = 2.0;  ///< interval width in units of the transition width sqrt(1 - rho) / sqrt(rho)
        double max_step = 2.0;    ///< interval width cap (low correlations)

        void validate() const {
            if (!(bound > 0.0 && resolution > 0.0 && max_step > 0.0))
                throw std::invalid_argument("FactorQuadrature: bound, resolution and max_step must be positive");
        }

        /// @brief Integration step for a transition of width `transition_width`.
        [[nodiscard]] double step(double transition_width) const noexcept {
            return std::min(max_step, resolution * transition_width);
        }

        /// @brief E[f(Z) 1{lo < Z < hi}] by composite Gauss-Legendre with intervals of width at most `step`.
        template <class F>
        [[nodiscard]] static double integrate(F&& f, double lo, double hi, double step) {
            static constexpr double x[5] = {0.1488743389816312, 0.4333953941292472, 0.6794095682990244,
                                            0.8650633666889845, 0.9739065285171717};
            static constexpr double w[5] = {0.2955242247147529, 0.2692667193099963, 0.2190863625159820,
                                            0.1494513491505806, 0.0666713443086881};
            if (!(lo < hi)) return 0.0;
            const int n = std::max(1, static_cast<int>(std::ceil((hi - lo) / step)));
            const double h = (hi - lo) / n;
            double sum = 0.0;
            for (int k = 0; k < n; ++k) {
                const double mid = lo + (k + 0.5) * h;
                for (int i = 0; i < 5; ++i) {
                    const double dz = 0.5 * h * x[i];
                    sum += w[i] * (Core::norm_pdf(mid - dz) * f(mid - dz) + Core::norm_pdf(mid + dz) * f(mid + dz));
                }
            }
            return 0.5 * h * sum;
        }
    };

} // namespace Models::Credit
