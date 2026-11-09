// Valuation results of a CDS, shared by IsdaCdsEngine and PreparedCds.

#pragma once
#include "Instruments/Credit/Cds.h"
#include "Pricing/Credit/CdsIntegrationGrid.h"
#include "Pricing/Credit/UpfrontValuation.h"

namespace Pricing::Credit {


    /// @brief Valuation of a CDS as of its trade date
    ///        Leg values are per unit notional; sign convention: protection buyer.
    struct CdsValuation : UpfrontValuation {
        [[nodiscard]] static CdsValuation from_legs(const Instruments::Credit::Cds& cds, const CdsLegPv& legs,
                                                    double df_cash_settlement) noexcept {
            return {{(1.0 - cds.recovery_rate) * legs.protection, legs.premium, cds.accrued_at_step_in(),
                     df_cash_settlement, cds.running_coupon, cds.notional}};
        }
    };

} // namespace Pricing::Credit
