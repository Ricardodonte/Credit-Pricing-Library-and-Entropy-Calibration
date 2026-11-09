// Market quote of a standard index tranche: par spread (mezzanine tranches, historically) or points upfront
// against a fixed running coupon (equity, and every tranche since the 2009 standardisation).

#pragma once
#include "Core/Period.h"

namespace Instruments::Credit {

    enum class TrancheQuoteType {
        ParSpread,  /// running spread making the clean upfront zero
        Upfront     /// clean points upfront for a fixed running coupon
    };

    struct TrancheQuote {
        Core::Period tenor;
        double attachment = 0.0;
        double detachment = 0.0;
        TrancheQuoteType type = TrancheQuoteType::Upfront;
        double value = 0.0;           /// par spread or points upfront
        double running_coupon = 0.0;  /// contract coupon

        [[nodiscard]] static TrancheQuote par_spread(Core::Period tenor, double attachment, double detachment,
                                                     double spread) {
            return {tenor, attachment, detachment, TrancheQuoteType::ParSpread, spread, spread};
        }
        [[nodiscard]] static TrancheQuote upfront(Core::Period tenor, double attachment, double detachment,
                                                  double points, double running_coupon) {
            return {tenor, attachment, detachment, TrancheQuoteType::Upfront, points, running_coupon};
        }

        /// @brief Clean points upfront targeted by the quote (zero for a par quote).
        [[nodiscard]] double target_points_upfront() const noexcept {
            return type == TrancheQuoteType::ParSpread ? 0.0 : value;
        }
    };

} // namespace Instruments::Credit
