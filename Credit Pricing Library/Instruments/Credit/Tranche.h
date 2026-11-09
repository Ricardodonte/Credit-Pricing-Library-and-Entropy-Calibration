// Synthetic CDO tranche on a credit index: protection on the portfolio losses between the attachment
// and the detachment points, against a running coupon (plus an upfront for standard tranches).
// Standard index tranches follow the CDS conventions: quarterly premiums on IMM dates, Act/360, full first
// coupon, step-in at T+1 and cash settlement at T+3. The schedule is therefore built with make_cds.
// Attachment and detachment are fractions of the index notional (e.g. 0.03 and 0.06).

#pragma once
#include <stdexcept>
#include <vector>

#include "Core/Dates.h"
#include "Core/ImmDates.h"
#include "Core/Period.h"
#include "Core/calendar.h"
#include "Instruments/Credit/Cds.h"

namespace Instruments::Credit {

    struct Tranche {
        Core::Date trade_date;
        Core::Date step_in_date;
        Core::Date cash_settlement_date;
        Core::Date protection_start;  ///< start of the default-leg integration (trade date)
        Core::Date maturity;          ///< unadjusted maturity
        std::vector<CdsPeriod> periods;
        double attachment = 0.0;      ///< lower strike K1, fraction of the index notional
        double detachment = 0.0;      ///< upper strike K2, fraction of the index notional
        double running_coupon = 0.0;
        double notional = 1.0;        ///< tranche notional (not the index notional)

        [[nodiscard]] double width() const noexcept { return detachment - attachment; }
        [[nodiscard]] bool is_equity() const noexcept { return attachment == 0.0; }

        /// @brief Premium accrued from the first accrual date to the step-in date (Act/360),
        ///        paid back to the protection buyer at cash settlement.
        [[nodiscard]] double accrued_at_step_in() const noexcept {
            return static_cast<double>(step_in_date - periods.front().accrual_start) / 360.0;
        }
    };

    /// @brief Tranche with an explicit maturity, on the CDS premium schedule.
    template <class Cal = Core::WeekendsOnly>
    [[nodiscard]] Tranche make_tranche(Core::Date trade_date, Core::Date maturity, double attachment, double detachment,
                                       double running_coupon, const Cal& calendar = {},
                                       const CdsConventions& conventions = {}, double notional = 1.0) {
        if (!(attachment >= 0.0 && attachment < detachment && detachment <= 1.0))
            throw std::invalid_argument("make_tranche: strikes must satisfy 0 <= attachment < detachment <= 1");
        // The recovery is a property of the names, not of the tranche: 0 is only a placeholder for make_cds.
        const Cds schedule = make_cds(trade_date, maturity, running_coupon, 0.0, calendar, conventions, notional);
        return {schedule.trade_date, schedule.step_in_date, schedule.cash_settlement_date, schedule.protection_start,
                schedule.maturity, schedule.periods, attachment, detachment, running_coupon, notional};
    }

    /// @brief Standard index tranche: maturity from the ISDA 2015 roll rule (e.g. 5Y -> next 20 June / 20 December).
    template <class Cal = Core::WeekendsOnly>
    [[nodiscard]] Tranche make_standard_tranche(Core::Date trade_date, Core::Period tenor, double attachment,
                                                double detachment, double running_coupon, const Cal& calendar = {},
                                                const CdsConventions& conventions = {}, double notional = 1.0) {
        return make_tranche(trade_date, Core::cds_standard_maturity(trade_date, tenor), attachment, detachment,
                            running_coupon, calendar, conventions, notional);
    }

} // namespace Instruments::Credit
