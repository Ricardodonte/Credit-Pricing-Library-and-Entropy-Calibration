
#pragma once
#include <algorithm>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <vector>

#include "Core/Dates.h"
#include "Core/day_counter.h"
#include "Core/interpolator.h"

namespace Curves {
    /// @brief Base classe for curves based on the  CRTP.
    /// Need :
    ///        - Derived : the real curve type
    ///              static double initial_value()           value of the pillar at time t = 0 ;
    ///              static void   validate_pillar(double v) raise std::invalid_argument when a value v is invalid for
    ///              that curve;
    ///              (optionnel) void on_pillar_added()
    ///        - Interp : The interpolation type or class (concept Core::Interpolator).
    ///        - DC : The day count convention
    template <typename Derived, Core::Interpolator Interp, Core::DayCountConvention DC>
    class CurveBase {
    public :
        using interpolator_type = Interp;
        using day_counter_type  = DC;

        explicit CurveBase(Core::Date ref_date, DC day_counter = {})
            : ref_date_(ref_date), day_counter_(day_counter) {
            if (ref_date.is_null())
                throw std::invalid_argument("CurveBase: reference date cannot be null");
            dates_.push_back(ref_date_);
            times_.push_back(0.0);
            values_.push_back(Derived::initial_value());
            nodes_.push_back(Interp::to_node(values_.back()));
        }

        [[nodiscard]] Core::Date reference_date() const noexcept { return ref_date_; }
        [[nodiscard]] const DC& day_counter() const noexcept { return day_counter_; }
        [[nodiscard]] double time_from_reference(Core::Date d) const noexcept {
            return day_counter_.year_fraction(ref_date_, d);
        }

        [[nodiscard]] std::size_t size() const noexcept { return dates_.size(); }
        [[nodiscard]] std::span<const Core::Date> pillar_dates()  const noexcept { return dates_; }
        [[nodiscard]] std::span<const double>     pillar_times()  const noexcept { return times_; }
        [[nodiscard]] std::span<const double>     pillar_values() const noexcept { return values_; }

        /// @brief Add a pillar at the end of the current final pillar
        void add_pillar(Core::Date d, double value) {
            if (d <= dates_.back())
                throw std::invalid_argument(
                    "CurveBase: pillar dates must be strictly increasing and after the reference date");
            const double t = time_from_reference(d);
            if (t <= times_.back())
                throw std::invalid_argument("CurveBase: pillar times must be strictly increasing");
            Derived::validate_pillar(value);

            if (dates_.size() == dates_.capacity()) {
                const std::size_t capacity = 2 * dates_.size();
                dates_.reserve(capacity);
                times_.reserve(capacity);
                values_.reserve(capacity);
                nodes_.reserve(capacity);
            }
            dates_.push_back(d);
            times_.push_back(t);
            values_.push_back(value);
            nodes_.push_back(Interp::to_node(value));
            if constexpr (requires(Derived& c) { c.on_pillar_added(); }) {
                derived().on_pillar_added();
            }
        }
        /// @brief Values at each given time t.
        [[nodiscard]] double value(double t) const noexcept {
            if (t <= times_.front()) return Interp::extrapolate_left(times_, nodes_, t);
            if (t >  times_.back())  return Interp::extrapolate_right(times_, nodes_, t);
            return Interp::interpolate(times_, nodes_, upper_index(t), t);
        }
        /// @brief Interpolated value at a given date d-type Date
        [[nodiscard]] double value(Core::Date d) const noexcept {
            return value(time_from_reference(d));
        }

        [[nodiscard]] double value_with_candidate(double t, double t_new, double v_new) const noexcept {
            if (t <= times_.back()) return value(t);
            const double xs[2] = {times_.back(), t_new};
            const double ys[2] = {nodes_.back(), Interp::to_node(v_new)};
            if (t <= t_new) return Interp::interpolate(xs, ys, 1, t);
            return Interp::extrapolate_right(xs, ys, t);
        }
    protected :
        /// @brief Index i such that times[i-1] < t <= times[i] with the condition front < t <= back
        [[nodiscard]] std::size_t upper_index(double t) const noexcept {
            return static_cast<std::size_t>(
                std::ranges::lower_bound(times_, t) - times_.begin());
        }
        [[nodiscard]] Derived& derived() noexcept { return static_cast<Derived&>(*this); }
        [[nodiscard]] const Derived& derived() const noexcept { return static_cast<const Derived&>(*this); }

    private :
        Core::Date ref_date_;
        DC day_counter_;
        std::vector<Core::Date> dates_;
        std::vector<double> times_;
        std::vector<double> values_;
        std::vector<double> nodes_;
    };

}
