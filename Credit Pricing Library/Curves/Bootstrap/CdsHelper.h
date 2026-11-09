// Bootstrap helper for the credit curve: one standard CDS quote (par spread or points upfront).
// The unknown is the hazard rate on the segment ending at the CDS maturity.

#pragma once
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "Core/Dates.h"
#include "Core/calendar.h"
#include "Instruments/Credit/Cds.h"
#include "Instruments/Credit/CdsQuote.h"
#include "Pricing/Credit/CdsIntegrationGrid.h"
#include "Pricing/Credit/IsdaCdsEngine.h"

namespace Curves::Bootstrap {

    /// @brief Holds a non-owning pointer to the discount curve, which must outlive the helper.
    template <class YieldCurveT>
    class CdsHelper {
    public:
        CdsHelper(Instruments::Credit::Cds cds, Instruments::Credit::CdsQuote quote, const YieldCurveT& yield_curve)
            : cds_(std::move(cds)), quote_(quote), yield_curve_(&yield_curve) {
            if (cds_.running_coupon != quote_.running_coupon || cds_.recovery_rate != quote_.recovery_rate)
                throw std::invalid_argument("CdsHelper: CDS coupon and recovery must match the quote");
        }

        /// @brief Standard CDS (ISDA 2015 maturity) traded at `trade_date`, built from the quote's tenor.
        template <class Cal = Core::WeekendsOnly>
        [[nodiscard]] static CdsHelper standard(Core::Date trade_date, const Instruments::Credit::CdsQuote& quote,
                                                const YieldCurveT& yield_curve, const Cal& calendar = {},
                                                const Instruments::Credit::CdsConventions& conventions = {}) {
            return CdsHelper(Instruments::Credit::make_standard_cds(trade_date, quote.tenor, quote.running_coupon,
                                                                    quote.recovery_rate, calendar, conventions),
                             quote, yield_curve);
        }

        [[nodiscard]] Core::Date pillar_date() const noexcept { return cds_.maturity; }
        [[nodiscard]] double quote() const noexcept { return quote_.value; }
        [[nodiscard]] const Instruments::Credit::Cds& instrument() const noexcept { return cds_; }
        [[nodiscard]] std::string description() const {
            return "CDS " + quote_.tenor.to_string()
                 + (quote_.type == Instruments::Credit::CdsQuoteType::ParSpread ? " par spread" : " upfront");
        }

        /// @brief Par spread or points upfront implied by the curves.
        template <class CreditCurveT>
        [[nodiscard]] double implied_quote(const CreditCurveT& credit_curve) const {
            const auto v = Pricing::Credit::IsdaCdsEngine(*yield_curve_, credit_curve).value(cds_);
            return quote_.type == Instruments::Credit::CdsQuoteType::ParSpread ? v.par_spread() : v.points_upfront();
        }

        /// @brief Residual(lambda) = clean points upfront(lambda) - quoted points (0 for a par quote).
        ///        The legs on the nodes up to the last known pillar are integrated once, before the solver;
        ///        each evaluation only integrates the new segment, in O(nodes of the new segment).
        template <class CreditCurveT>
        [[nodiscard]] auto objective(const CreditCurveT& credit_curve) const {
            if (yield_curve_->reference_date() != cds_.trade_date || credit_curve.reference_date() != cds_.trade_date)
                throw std::invalid_argument("CdsHelper: both curves must be built as of the CDS trade date");

            Pricing::Credit::CdsIntegrationGrid grid(cds_, *yield_curve_, credit_curve.pillar_dates());
            const auto nodes = grid.nodes();
            const std::size_t split = grid.last_node_on_or_before(credit_curve.pillar_dates().back());

            const Pricing::Credit::CdsLegPv known = grid.accumulate(
                [&](std::size_t j) { return credit_curve.survival_probability(nodes[j]); }, 0, split);

            std::vector<double> times(nodes.size());
            for (std::size_t j = 0; j < nodes.size(); ++j) times[j] = credit_curve.time_from_reference(nodes[j]);

            const double df_cash_settlement = yield_curve_->discount(cds_.cash_settlement_date);
            const double target = quote_.target_points_upfront();

            return [&credit_curve, grid = std::move(grid), times = std::move(times), known, split,
                    df_cash_settlement, target, cds = &cds_](double lambda) {
                const Pricing::Credit::CdsLegPv tail = grid.accumulate(
                    [&](std::size_t j) { return credit_curve.survival_probability_with_candidate(times[j], lambda); },
                    split, grid.size() - 1);
                const Pricing::Credit::CdsLegPv legs{known.protection + tail.protection, known.premium + tail.premium};
                return Pricing::Credit::CdsValuation::from_legs(*cds, legs, df_cash_settlement).points_upfront() - target;
            };
        }

    private:
        Instruments::Credit::Cds cds_;
        Instruments::Credit::CdsQuote quote_;
        const YieldCurveT* yield_curve_;
    };

} // namespace Curves::Bootstrap
