// Quoting arithmetic shared by every standard credit contract with a fixed running coupon and an upfront
// (single-name CDS, index tranches, indices): par spread, clean points upfront, cash settlement and NPV
// from the protection leg and the risky annuity.

#pragma once

namespace Pricing::Credit {

    /// @brief Leg values per unit notional as of the trade date; sign convention: protection buyer.
    struct UpfrontValuation {
        double protection_leg;      /// PV of the expected losses paid by the protection seller
        double rpv01_dirty;         /// risky annuity: full coupons (incl. accrual before step-in) + accrued on default
        double accrued;             /// Act/360 accrual from the first accrual date to step-in
        double df_cash_settlement;  /// discount factor to the cash-settlement date
        double running_coupon;
        double notional;

        /// @brief Risky annuity net of the accrued premium refunded at cash settlement.
        [[nodiscard]] double rpv01_clean() const noexcept { return rpv01_dirty - accrued * df_cash_settlement; }

        /// @brief Running spread for which the clean upfront is zero.
        [[nodiscard]] double par_spread() const noexcept { return protection_leg / rpv01_clean(); }

        /// @brief Clean points upfront for the contract coupon, as of cash settlement, per unit notional.
        ///        Positive: paid by the protection buyer. This is the market quote.
        [[nodiscard]] double points_upfront() const noexcept {
            return (protection_leg - running_coupon * rpv01_clean()) / df_cash_settlement;
        }

        /// @brief Cash exchanged at settlement (dirty: points upfront minus accrued premium), scaled by notional.
        [[nodiscard]] double cash_settlement_amount() const noexcept {
            return notional * (protection_leg - running_coupon * rpv01_dirty) / df_cash_settlement;
        }

        /// @brief Mark-to-market for the protection buyer as of trade date, scaled by notional.
        [[nodiscard]] double npv() const noexcept {
            return notional * (protection_leg - running_coupon * rpv01_dirty);
        }
    };

} // namespace Pricing::Credit
