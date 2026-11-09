// Generic sequential (pillar-by-pillar) bootstrapper.
// Design:
//  - one generic engine for every curve type; curve-specific knowledge lives in BootstrapTraits<Curve>
//    and instrument-specific knowledge in the helpers;
//  - heterogeneous helpers are held in a std::variant and dispatched with std::visit: static dispatch,
//    no virtual call. The helper list is a template parameter, so new instruments are added without
//    modifying this file (open-closed);
//  - principle 3 of the guide: the curve is never modified while solving. Each helper builds a pure
//    objective (known part of the price precomputed once), Brent solves it, and the pillar is appended
//    once with the solved value.

#pragma once
#include <algorithm>
#include <cmath>
#include <exception>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "Core/Dates.h"
#include "Core/numericals.h"
#include "Curves/Bootstrap/BootstrapTraits.h"
#include "Curves/Bootstrap/BootstrapTypes.h"

namespace Curves::Bootstrap {

    template <class Curve, class... Helpers>
        requires (sizeof...(Helpers) > 0) && (BootstrapHelper<Helpers, Curve> && ...)
    class SequentialBootstrapper {
    public:
        using curve_type = Curve;
        using helper_type = std::variant<Helpers...>;
        using traits = BootstrapTraits<Curve>;

        explicit SequentialBootstrapper(BootstrapSettings settings = {}) noexcept : settings_(settings) {}

        /// @brief Bootstraps `curve` (typically empty: reference pillar only) on the given instruments.
        ///        Helpers are sorted by pillar date; pillars must be distinct and after the last curve pillar.
        ///        The report reprices every input on the FINAL curve.
        /// @param curve   starting curve, taken by value: its reference date and day counter are kept
        /// @param helpers one helper per market quote, in any order
        [[nodiscard]] BootstrapResult<Curve> bootstrap(Curve curve, std::vector<helper_type> helpers) const {
            if (helpers.empty())
                throw std::invalid_argument("SequentialBootstrapper: no instrument to bootstrap");

            std::ranges::stable_sort(helpers, {}, [](const helper_type& h) { return pillar_of(h); });
            validate_pillars(curve, helpers);

            const Core::Brent solver(settings_.accuracy, settings_.max_iterations);
            for (const auto& h : helpers)
                std::visit([&](const auto& helper) { solve_pillar(curve, helper, solver); }, h);

            BootstrapResult<Curve> result{std::move(curve), {}};
            result.report.reserve(helpers.size());
            for (const auto& h : helpers) {
                std::visit([&](const auto& helper) {
                    const Core::Date pillar = helper.pillar_date();
                    result.report.push_back({helper.description(), pillar, result.curve.value(pillar),
                                             helper.quote(), helper.implied_quote(result.curve)});
                }, h);
            }
            return result;
        }

        [[nodiscard]] const BootstrapSettings& settings() const noexcept { return settings_; }

    private:
        BootstrapSettings settings_;

        [[nodiscard]] static Core::Date pillar_of(const helper_type& h) {
            return std::visit([](const auto& x) { return x.pillar_date(); }, h);
        }
        [[nodiscard]] static std::string description_of(const helper_type& h) {
            return std::visit([](const auto& x) { return std::string(x.description()); }, h);
        }
        template <class H>
        [[nodiscard]] static std::string context(const H& helper) {
            return "Bootstrap failed for " + std::string(helper.description()) + " (pillar "
                 + helper.pillar_date().to_string() + ")";
        }

        static void validate_pillars(const Curve& curve, const std::vector<helper_type>& helpers) {
            Core::Date previous = curve.pillar_dates().back();
            for (const auto& h : helpers) {
                const Core::Date p = pillar_of(h);
                if (!(previous < p))
                    throw std::invalid_argument("SequentialBootstrapper: " + description_of(h) + " has pillar "
                                                + p.to_string() + ", not after the previous pillar "
                                                + previous.to_string() + " (duplicate pillar or pillar before the "
                                                "curve reference date)");
                previous = p;
            }
        }

        template <class H>
        void solve_pillar(Curve& curve, const H& helper, const Core::Brent& solver) const {
            const Core::Date pillar = helper.pillar_date();
            const auto objective = helper.objective(std::as_const(curve));  // curve frozen from here

            auto [lo, hi] = traits::initial_bracket(curve, curve.time_from_reference(pillar));
            bracket_root(objective, lo, hi, helper);

            double value = 0.0;
            try {
                value = solver.solve(objective, lo, hi);
            } catch (const std::exception& e) {
                throw BootstrapError(context(helper) + ": " + e.what());
            }
            try {
                curve.add_pillar(pillar, value);
            } catch (const std::exception& e) {
                throw BootstrapError(context(helper) + ": solved value rejected by the curve: " + e.what());
            }
        }

        /// @brief Widens [lo, hi] (towards the side with the smaller |f|, within the traits limits)
        ///        until f changes sign.
        template <class F, class H>
        void bracket_root(const F& f, double& lo, double& hi, const H& helper) const {
            double f_lo = f(lo);
            double f_hi = f(hi);
            for (int k = 0; f_lo * f_hi > 0.0 || !std::isfinite(f_lo) || !std::isfinite(f_hi); ++k) {
                const bool can_lower = lo > traits::lower_limit;
                const bool can_raise = hi < traits::upper_limit;
                if (k >= settings_.max_bracket_expansions || (!can_lower && !can_raise)
                    || !std::isfinite(f_lo) || !std::isfinite(f_hi)) {
                    throw BootstrapError(context(helper) + ": no root in [" + std::to_string(lo) + ", "
                                         + std::to_string(hi) + "], objective = [" + std::to_string(f_lo) + ", "
                                         + std::to_string(f_hi) + "]. Check the quote.");
                }
                const double width = hi - lo;
                const bool widen_low = can_lower && (!can_raise || std::abs(f_lo) < std::abs(f_hi));
                if (widen_low) {
                    lo = std::max(traits::lower_limit, lo - width);
                    f_lo = f(lo);
                } else {
                    hi = std::min(traits::upper_limit, hi + width);
                    f_hi = f(hi);
                }
            }
        }
    };

} // namespace Curves::Bootstrap
