
#pragma once
#include <cmath>
#include <stdexcept>

#include "Curves/BaseCurves.h"


namespace Curves {
    /// @brief YieldCurve : disocunt factors as values in pillars, with the log-linear interpolation
    /// by default (forward rates piecewise constant)
    template <Core::DayCountConvention DC = Core::Actual365Fixed>
    class YieldCurve : public CurveBase<YieldCurve<DC>, Core::LogLinearInterpolator, DC> {
        using Base = CurveBase<YieldCurve, Core::LogLinearInterpolator, DC>;
    public :
        explicit YieldCurve(Core::Date ref_date, DC day_counter = {}) : Base(ref_date, day_counter) {}

        [[nodiscard]] double discount(double t) const noexcept { return this->value(t); }
        [[nodiscard]] double discount(Core::Date d) const noexcept { return this->value(d); }

        /// @brief Constant forward rate between t1 and t2
        [[nodiscard]] double forward_rate(double t1, double t2) const {
            if (t2 <= t1)
                throw std::invalid_argument("YieldCurve::forward_rate: t2 must be strictly greater than t1");
            return -std::log(discount(t2) / discount(t1)) / (t2 - t1);
        }
        [[nodiscard]] double forward_rate(Core::Date d1, Core::Date d2) const {
            return forward_rate(this->time_from_reference(d1), this->time_from_reference(d2));
        }
        //@brief Discount factor for the yieldCurve for a candidate DF, for pure bootsrap purposes
        [[nodiscard]] double discount_with_candidate(double t, double t_new, double df_new) const noexcept {
            return this->value_with_candidate(t, t_new, df_new);
        }

        [[nodiscard]] static constexpr double initial_value() noexcept { return 1.0; }

        static void validate_pillar(double df) {
            if (!(df > 0.0 && std::isfinite(df))) {
                throw std::invalid_argument("YieldCurve: discount factor must be strictly positive and finite");
            }
        }
    };
}
