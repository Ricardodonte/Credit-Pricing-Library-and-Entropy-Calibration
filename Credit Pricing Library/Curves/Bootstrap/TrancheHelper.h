// Bootstrap helper for the base correlation curve: one quoted index tranche [K1, K2] (par spread or upfront).
// The unknown is the base correlation rho(K2); rho(K1) is read on the curve built so far.
// The tranche is prepared once, at construction (grid, leg weights, loss model state at every node). The
// objective precomputes the legs of the base tranche [0, K1], which do not depend on the unknown: each
// evaluation only prices the base tranche [0, K2]. The curve is never modified (principle 3).

#pragma once
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include "Curves/BaseCorrelationCurve.h"
#include "Instruments/Credit/Tranche.h"
#include "Instruments/Credit/TrancheQuote.h"
#include "Models/Credit/LossModel.h"
#include "Pricing/Credit/PreparedTranche.h"

namespace Curves::Bootstrap {

    template <Models::Credit::TrancheLossModel LossModel>
    class TrancheHelper {
    public:
        using prepared_type = Pricing::Credit::PreparedTranche<LossModel>;

        TrancheHelper(prepared_type prepared, Instruments::Credit::TrancheQuote quote)
            : prepared_(std::move(prepared)), quote_(quote) {
            const auto& t = prepared_.contract();
            if (t.attachment != quote_.attachment || t.detachment != quote_.detachment)
                throw std::invalid_argument("TrancheHelper: tranche strikes must match the quote");
            if (t.running_coupon != quote_.running_coupon)
                throw std::invalid_argument("TrancheHelper: tranche coupon must match the quote");
        }

        /// @brief Standard tranche (ISDA 2015 maturity) built from the quote, prepared on the given market.
        template <class YieldCurveT, class CreditCurveT, class Cal = Core::WeekendsOnly>
        [[nodiscard]] static TrancheHelper standard(Core::Date trade_date, const Instruments::Credit::TrancheQuote& quote,
                                                    const YieldCurveT& yield_curve,
                                                    const Pricing::Credit::ReferencePool<CreditCurveT>& pool,
                                                    LossModel model = {},
                                                    const Pricing::Credit::TrancheGridSettings& settings = {},
                                                    const Cal& calendar = {}) {
            auto tranche = Instruments::Credit::make_standard_tranche(trade_date, quote.tenor, quote.attachment,
                                                                      quote.detachment, quote.running_coupon, calendar);
            return {prepared_type(std::move(tranche), yield_curve, pool, std::move(model), settings), quote};
        }

        [[nodiscard]] double attachment() const noexcept { return quote_.attachment; }
        [[nodiscard]] double detachment() const noexcept { return quote_.detachment; }
        [[nodiscard]] double quote() const noexcept { return quote_.value; }
        [[nodiscard]] const prepared_type& prepared() const noexcept { return prepared_; }

        [[nodiscard]] std::string description() const {
            std::ostringstream s;
            s << "Tranche " << quote_.tenor.to_string() << ' ' << 100.0 * quote_.attachment << "-"
              << 100.0 * quote_.detachment << "%"
              << (quote_.type == Instruments::Credit::TrancheQuoteType::ParSpread ? " par spread" : " upfront");
            return s.str();
        }

        /// @brief Par spread or points upfront implied by a base correlation curve.
        template <class Interp>
        [[nodiscard]] double implied_quote(const BaseCorrelationCurve<Interp>& curve) const {
            const auto v = prepared_.value(curve);
            return quote_.type == Instruments::Credit::TrancheQuoteType::ParSpread ? v.par_spread() : v.points_upfront();
        }

        /// @brief Residual(rho2) = clean points upfront(rho(K1), rho2) - quoted points (0 for a par quote).
        template <class Interp>
        [[nodiscard]] auto objective(const BaseCorrelationCurve<Interp>& curve) const {
            if (!prepared_.contract().is_equity()) {
                if (curve.empty() || curve.detachments().back() < attachment())
                    throw std::invalid_argument("TrancheHelper: the base correlation at the attachment point "
                                                "must be known (bootstrap the tranches below first)");
            }
            const double rho_attachment = prepared_.contract().is_equity() ? 0.0 : curve.correlation(attachment());
            const Pricing::Credit::BaseTrancheLegs known = prepared_.base_legs(attachment(), rho_attachment);
            const double target = quote_.target_points_upfront();
            return [this, known, target](double rho_detachment) {
                const auto legs = prepared_.base_legs(detachment(), rho_detachment);
                return prepared_.valuation(known, legs).points_upfront() - target;
            };
        }

    private:
        prepared_type prepared_;
        Instruments::Credit::TrancheQuote quote_;
    };

} // namespace Curves::Bootstrap
