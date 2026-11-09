// Common types of the bootstrap framework: settings, helper concept, report and errors.

#pragma once
#include <algorithm>
#include <cmath>
#include <concepts>
#include <stdexcept>
#include <string>
#include <vector>

#include "Core/Dates.h"

namespace Curves::Bootstrap {

    struct BootstrapSettings {
        double accuracy = 1e-12;
        int max_iterations = 100;         // Brent iterations per pillar
        int max_bracket_expansions = 60;  /// Number of attempts to widen the initial bracket
    };

    /// @brief A bootstrap helper that turns one market quote into one curve pillar.

    template <class H, class Curve>
    concept BootstrapHelper = requires(const H& h, const Curve& curve, double candidate) {
        { h.pillar_date() } -> std::same_as<Core::Date>;
        { h.quote() } -> std::convertible_to<double>;
        { h.implied_quote(curve) } -> std::convertible_to<double>;
        { h.description() } -> std::convertible_to<std::string>;
        { h.objective(curve)(candidate) } -> std::convertible_to<double>;
    };

    /// @brief Repricing diagnostics of one input instrument on the final curve.
    struct PillarReport {
        std::string instrument;
        Core::Date pillar;
        double pillar_value;   
        double quote;
        double implied_quote;
        [[nodiscard]] double error() const noexcept { return implied_quote - quote; }
    };

    template <class Curve>
    struct BootstrapResult {
        Curve curve;
        std::vector<PillarReport> report;

        /// @brief Largest absolute repricing error, in quote units.
        [[nodiscard]] double max_abs_error() const noexcept {
            double m = 0.0;
            for (const auto& r : report) m = std::max(m, std::abs(r.error()));
            return m;
        }
    };

    /// @brief Raised when a pillar cannot be solved (no root, no convergence, invalid value).
    class BootstrapError : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };

} // namespace Curves::Bootstrap
