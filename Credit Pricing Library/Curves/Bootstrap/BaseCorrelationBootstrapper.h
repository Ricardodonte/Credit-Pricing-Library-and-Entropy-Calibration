// Sequential bootstrap of a base correlation curve from quoted index tranches.
// Same scheme as SequentialBootstrapper (sort, solve each pillar on a frozen curve, append once, reprice every
// input on the final curve), but on the detachment axis instead of dates, hence a dedicated engine rather than a
// modification of the generic one (open-closed).
// Root search. The tranche price is not always monotonic in the base correlation (typically for senior
// tranches, where two roots or none can exist): instead of widening a bracket, the admissible range is scanned
// on a regular grid and Brent is run on the FIRST sign change, i.e. the lowest correlation matching the quote.
// A tranche detaching above the maximal pool loss does not depend on its base correlation: see
// undetermined_correlation().

#pragma once
#include <algorithm>
#include <cmath>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "Core/interpolator.h"
#include "Core/numericals.h"
#include "Curves/BaseCorrelationCurve.h"
#include "Curves/Bootstrap/BootstrapTypes.h"
#include "Curves/Bootstrap/TrancheHelper.h"

namespace Curves::Bootstrap {

    struct BaseCorrelationSettings {
        double accuracy = 1e-10;        ///< Brent tolerance on the correlation
        int max_iterations = 100;
        double min_correlation = 0.0;
        double max_correlation = 0.999;
        int scan_points = 10;           ///< regular scan of [min, max] to bracket the first root
        double insensitive_tolerance = 1e-8;  ///< quote error accepted when the price ignores the correlation
    };

    /// @brief Repricing diagnostics of one quoted tranche on the final curve.
    struct TrancheReport {
        std::string instrument;
        double detachment;
        double base_correlation;
        double quote;
        double implied_quote;
        [[nodiscard]] double error() const noexcept { return implied_quote - quote; }
    };

    template <class Interp = Core::LinearInterpolator>
    struct BaseCorrelationResult {
        BaseCorrelationCurve<Interp> curve;
        std::vector<TrancheReport> report;

        [[nodiscard]] double max_abs_error() const noexcept {
            double m = 0.0;
            for (const auto& r : report) m = std::max(m, std::abs(r.error()));
            return m;
        }
    };

    template <Models::Credit::TrancheLossModel LossModel, Core::Interpolator Interp = Core::LinearInterpolator>
    class BaseCorrelationBootstrapper {
    public:
        using helper_type = TrancheHelper<LossModel>;
        using curve_type = BaseCorrelationCurve<Interp>;

        explicit BaseCorrelationBootstrapper(BaseCorrelationSettings settings = {}) : settings_(settings) {
            if (!(0.0 <= settings_.min_correlation && settings_.min_correlation < settings_.max_correlation
                  && settings_.max_correlation < 1.0) || settings_.scan_points < 1)
                throw std::invalid_argument("BaseCorrelationBootstrapper: invalid correlation range or scan");
        }

        /// @param helpers quoted tranches of ONE index series and maturity, contiguous from 0, in any order.
        [[nodiscard]] BaseCorrelationResult<Interp> bootstrap(curve_type curve, std::vector<helper_type> helpers) const {
            if (helpers.empty())
                throw std::invalid_argument("BaseCorrelationBootstrapper: no tranche to bootstrap");
            std::ranges::stable_sort(helpers, {}, &helper_type::detachment);
            validate_capital_structure(curve, helpers);

            const Core::Brent solver(settings_.accuracy, settings_.max_iterations);
            for (const auto& h : helpers) solve_pillar(curve, h, solver);

            BaseCorrelationResult<Interp> result{std::move(curve), {}};
            result.report.reserve(helpers.size());
            for (const auto& h : helpers)
                result.report.push_back({h.description(), h.detachment(), result.curve.correlation(h.detachment()),
                                         h.quote(), h.implied_quote(result.curve)});
            return result;
        }

        [[nodiscard]] const BaseCorrelationSettings& settings() const noexcept { return settings_; }

    private:
        BaseCorrelationSettings settings_;

        /// @brief Each tranche must attach where the previous one detaches (or at 0 / the last known pillar).
        static void validate_capital_structure(const curve_type& curve, const std::vector<helper_type>& helpers) {
            double previous = curve.empty() ? 0.0 : curve.detachments().back();
            for (const auto& h : helpers) {
                if (h.attachment() != previous)
                    throw std::invalid_argument("BaseCorrelationBootstrapper: " + h.description()
                                                + " does not attach at the previous detachment ("
                                                + std::to_string(previous) + "): tranches must be contiguous");
                previous = h.detachment();
            }
        }

        [[nodiscard]] static std::string context(const helper_type& h) {
            return "Base correlation bootstrap failed for " + h.description();
        }

        void solve_pillar(curve_type& curve, const helper_type& helper, const Core::Brent& solver) const {
            const auto objective = helper.objective(std::as_const(curve));  // curve frozen from here
            const auto bracket = first_bracket(objective);
            if (!bracket) {
                curve.add_pillar(helper.detachment(), undetermined_correlation(curve, objective, helper));
                return;
            }
            double rho = 0.0;
            try {
                rho = solver.solve(objective, bracket->first, bracket->second);
            } catch (const std::exception& e) {
                throw BootstrapError(context(helper) + ": " + e.what());
            }
            curve.add_pillar(helper.detachment(), rho);
        }

        /// @brief First sub-interval of the scan on which the objective changes sign, if any.
        template <class F>
        [[nodiscard]] std::optional<std::pair<double, double>> first_bracket(const F& f) const {
            const double lo = settings_.min_correlation;
            const double step = (settings_.max_correlation - lo) / settings_.scan_points;
            double x_prev = lo;
            double f_prev = f(x_prev);
            if (f_prev == 0.0) return std::pair{x_prev, x_prev + step};
            for (int k = 1; k <= settings_.scan_points; ++k) {
                const double x = k == settings_.scan_points ? settings_.max_correlation : lo + k * step;
                const double fx = f(x);
                if (!std::isfinite(fx)) return std::nullopt;
                if (f_prev * fx <= 0.0) return std::pair{x_prev, x};
                x_prev = x;
                f_prev = fx;
            }
            return std::nullopt;
        }

        /// @brief No sign change. When the detachment is above the maximal pool loss (e.g. 100% with 40% recovery),
        ///        E[min(L, K)] = E[L] for every rho: the price does not depend on the base correlation. If the quote
        ///        is matched anyway, the correlation is extended flat from the previous pillar (market convention);
        ///        otherwise the quote cannot be reached.
        template <class F>
        [[nodiscard]] double undetermined_correlation(const curve_type& curve, const F& f, const helper_type& helper) const {
            const double f_lo = f(settings_.min_correlation);
            const double f_hi = f(settings_.max_correlation);
            const bool insensitive = std::abs(f_hi - f_lo) <= 1e-12;
            if (insensitive && std::abs(f_lo) <= settings_.insensitive_tolerance)
                return curve.empty() ? settings_.min_correlation : curve.correlations().back();
            throw BootstrapError(context(helper) + (insensitive
                ? ": the price does not depend on the correlation (detachment above the maximal pool loss) "
                  "and does not match the quote"
                : ": no base correlation in [" + std::to_string(settings_.min_correlation) + ", "
                  + std::to_string(settings_.max_correlation)
                  + "] reprices the quote (arbitrage in the quotes, or loss model too coarse)"));
        }
    };

} // namespace Curves::Bootstrap
