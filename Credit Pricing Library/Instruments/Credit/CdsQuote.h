// Market quote of a standard CDS.

#pragma once
#include "Core/Period.h"

namespace Instruments::Credit {

    enum class CdsQuoteType {
        ParSpread,  /// running spread making the clean upfront zero
        Upfront     /// clean points upfront for a fixed running coupon
    };

    struct CdsQuote {
        Core::Period tenor;
        CdsQuoteType type = CdsQuoteType::ParSpread;
        double value = 0.0;           /// par spread or points upfront
        double running_coupon = 0.0;  /// contract coupon
        double recovery_rate = 0.4;

        [[nodiscard]] static CdsQuote par_spread(Core::Period tenor, double spread, double recovery_rate = 0.4) {
            return {tenor, CdsQuoteType::ParSpread, spread, spread, recovery_rate};
        }
        [[nodiscard]] static CdsQuote upfront(Core::Period tenor, double points, double running_coupon,
                                              double recovery_rate = 0.4) {
            return {tenor, CdsQuoteType::Upfront, points, running_coupon, recovery_rate};
        }

        /// @brief Clean points upfront targeted by the quote (zero for a par quote).
        [[nodiscard]] double target_points_upfront() const noexcept {
            return type == CdsQuoteType::ParSpread ? 0.0 : value;
        }
    };

} // namespace Instruments::Credit
