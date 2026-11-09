// Base correlation curve of one index series and maturity: rho(K) for base tranches [0, K].
// Its axis is the detachment point, not time, so it does not derive from CurveBase (which is built on dates and
// a day counter); it reuses the same Interpolator concept instead. Linear interpolation in K with flat
// extrapolation by default: any other Core::Interpolator can be plugged in.
// Pillars are appended in increasing detachment, which is what the sequential bootstrap produces.

#pragma once
#include <algorithm>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "Core/interpolator.h"

namespace Curves {

    template <Core::Interpolator Interp = Core::LinearInterpolator>
    class BaseCorrelationCurve {
    public:
        using interpolator_type = Interp;

        explicit BaseCorrelationCurve(std::string name = {}) : name_(std::move(name)) {}

        [[nodiscard]] const std::string& name() const noexcept { return name_; }
        [[nodiscard]] bool empty() const noexcept { return detachments_.empty(); }
        [[nodiscard]] std::size_t size() const noexcept { return detachments_.size(); }
        [[nodiscard]] std::span<const double> detachments() const noexcept { return detachments_; }
        [[nodiscard]] std::span<const double> correlations() const noexcept { return correlations_; }

        /// @brief Appends the base correlation of the base tranche [0, detachment].
        void add_pillar(double detachment, double correlation) {
            if (!(detachment > 0.0 && detachment <= 1.0))
                throw std::invalid_argument("BaseCorrelationCurve: detachment must lie in ]0, 1]");
            if (!detachments_.empty() && detachment <= detachments_.back())
                throw std::invalid_argument("BaseCorrelationCurve: detachments must be strictly increasing");
            validate_pillar(correlation);
            detachments_.push_back(detachment);
            correlations_.push_back(correlation);
            nodes_.push_back(Interp::to_node(correlation));
        }

        /// @brief Base correlation for the base tranche [0, K].
        [[nodiscard]] double correlation(double detachment) const {
            if (detachments_.empty())
                throw std::invalid_argument("BaseCorrelationCurve: curve " + name_ + " has no pillar");
            if (detachment <= detachments_.front()) return Interp::extrapolate_left(detachments_, nodes_, detachment);
            if (detachment > detachments_.back()) return Interp::extrapolate_right(detachments_, nodes_, detachment);
            const auto i = static_cast<std::size_t>(std::ranges::lower_bound(detachments_, detachment) - detachments_.begin());
            return Interp::interpolate(detachments_, nodes_, i, detachment);
        }

        static void validate_pillar(double correlation) {
            if (!(correlation >= 0.0 && correlation <= 1.0))
                throw std::invalid_argument("BaseCorrelationCurve: correlation must lie in [0, 1]");
        }

    private:
        std::string name_;
        std::vector<double> detachments_;
        std::vector<double> correlations_;
        std::vector<double> nodes_;  // Interp::to_node(correlation)
    };

} // namespace Curves
