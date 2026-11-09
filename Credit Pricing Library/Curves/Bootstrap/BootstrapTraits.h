// Curve specific solver information : admissible range of the pillar value and initial bracket.
// Specialise BootstrapTraits for every new bootstrappable curve

#pragma once
#include <algorithm>
#include <utility>

#include "Core/day_counter.h"
#include "Curves/CreditCurve.h"
#include "Curves/YieldCurve.h"

namespace Curves::Bootstrap {

    template <class Curve>
    struct BootstrapTraits;

    /// @brief Pillar value is the discount factor, strictly positive, we also allow discount factors
    /// greater than 1 for negative rates
    template <Core::DayCountConvention DC>
    struct BootstrapTraits<YieldCurve<DC>> {
        static constexpr double lower_limit = 1e-10;
        static constexpr double upper_limit = 10.0;
        /// @brief Function that gives a lower and upper bounds for a new candidate for the YC bootstrap
        [[nodiscard]] static std::pair<double, double> initial_bracket(const YieldCurve<DC>& curve, double t_new) {
            const double guess = curve.discount(t_new);
            return {std::max(lower_limit, 0.9 * guess), std::min(upper_limit, 1.1 * guess)};
        }
    };

    /// @brief Pillar value is the hazard rate on the new segment which is non-negative.
    template <Core::DayCountConvention DC>
    struct BootstrapTraits<CreditCurve<DC>> {
        static constexpr double lower_limit = 0.0;
        static constexpr double upper_limit = 50.0;
        // @brief A fonction to give a lower and upper bounds for a new candidate for the Credit curve bootsrap
        [[nodiscard]] static std::pair<double, double> initial_bracket(const CreditCurve<DC>& curve, double) {
            return {0.0, std::max(0.05, 2.0 * curve.pillar_values().back())};
        }
    };

} // namespace Curves::Bootstrap
