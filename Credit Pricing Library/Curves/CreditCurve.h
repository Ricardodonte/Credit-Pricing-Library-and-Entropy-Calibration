
#pragma once
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "Curves/BaseCurves.h"


namespace Curves {
    /// @brief Credit curve : use by defautl the piecewise constant interpolation scheme
    ///  On t[i-1] - t[i]], the hazard rate is equal to lambda[i], beyond the last pilar, the hazard rate is prolonged
    template <Core::DayCountConvention DC = Core::Actual365Fixed>
    class CreditCurve : public CurveBase<CreditCurve<DC>, Core::PiecewiseConstant, DC> {
        using Base = CurveBase<CreditCurve, Core::PiecewiseConstant, DC>;
        friend Base;
        public:
            explicit CreditCurve(std::string name, Core::Date ref_date, DC day_counter = {})
                : Base(ref_date, day_counter), m_name(std::move(name)) {}

            [[nodiscard]] const std::string& name() const noexcept { return m_name; }

            [[nodiscard]] double hazard_rate(double t) const noexcept { return this->value(t); }
            [[nodiscard]] double hazard_rate(Core::Date d) const noexcept { return this->value(d); }

            /// @brief Cumulative hazard rate H(t) = ∫_0^t lambda(s) ds.
            [[nodiscard]] double cumulative_hazard(double t) const noexcept {
                const auto times   = this->pillar_times();
                const auto lambdas = this->pillar_values();
                if (t <= times.front()) return 0.0;
                if (t > times.back())
                    return m_cumulative_hazard.back() + lambdas.back() * (t - times.back());
                const std::size_t i = this->upper_index(t);
                return m_cumulative_hazard[i - 1] + lambdas[i] * (t - times[i - 1]);
            }

            [[nodiscard]] double survival_probability(double t) const noexcept {
                return std::exp(-cumulative_hazard(t));
            }
            [[nodiscard]] double survival_probability(Core::Date d) const noexcept {
                return survival_probability(this->time_from_reference(d));
            }
            [[nodiscard]] double default_probability(double t) const noexcept {
                return 1.0 - survival_probability(t);
            }
            [[nodiscard]] double default_probability(Core::Date d) const noexcept {
                return 1.0 - survival_probability(d);
            }

            [[nodiscard]] double survival_probability_with_candidate(double t, double lambda_new) const noexcept {
                const auto times = this->pillar_times();
                if (t <= times.back()) return survival_probability(t);
                return std::exp(-(m_cumulative_hazard.back() + lambda_new * (t - times.back())));
            }

            [[nodiscard]] static constexpr double initial_value() noexcept { return 0.0; }

            static void validate_pillar(double lambda) {
                if (!(lambda >= 0.0))
                    throw std::invalid_argument("CreditCurve: Hazard rate cannot have negative value");
            }

        private:
            void on_pillar_added() {
                const auto times   = this->pillar_times();
                const auto lambdas = this->pillar_values();
                const std::size_t n = times.size();
                m_cumulative_hazard.push_back(
                    m_cumulative_hazard.back() + lambdas[n - 1] * (times[n - 1] - times[n - 2]));
            }

            std::string m_name;
            std::vector<double> m_cumulative_hazard{0.0};
        };

}
