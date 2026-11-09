// Single-name credit default swap with the generic ISDA standard contract, post-2009 , modulable.

#pragma once
#include <stdexcept>
#include <vector>

#include "Core/Dates.h"
#include "Core/ImmDates.h"
#include "Core/Period.h"
#include "Core/calendar.h"

namespace Instruments::Credit {

    /// @brief Standard CDS conventions. Premium accrual is always Act/360 (ISDA).
    struct CdsConventions {
        Core::Period coupon_tenor{3, Core::TimeUnit::Months};
        Core::BusinessDayConvention convention = Core::BusinessDayConvention::Following;
        int step_in_days = 1;          /// calendar days after trade date on which the buyer is exposed to
                                       /// credit risk
        int cash_settlement_days = 3;  /// business days after trade date where the upfront is paid
    };

    /// @brief One premium period. The last period accrues up to and including the maturity date.
    struct CdsPeriod {
        Core::Date accrual_start;
        Core::Date accrual_end;  /// exclusive; maturity + 1 day for the last period
        Core::Date payment;
        double accrual;        // from start to the end
    };

    /// @brief CDS contract class with the premium schedule resolved at construction
    struct Cds {
        Core::Date trade_date;
        Core::Date step_in_date;
        Core::Date cash_settlement_date;
        Core::Date protection_start;  ///< start of the default-leg integration (trade date)
        Core::Date maturity;          ///< unadjusted maturity (an IMM date for standard contracts)
        std::vector<CdsPeriod> periods;
        double running_coupon = 0.0;
        double recovery_rate = 0.4;
        double notional = 1.0;

        /// @brief Premium accrued from the first accrual date to the step-in date (Act/360),
        ///        paid back to the protection buyer at cash settlement.
        [[nodiscard]] double accrued_at_step_in() const noexcept {
            return static_cast<double>(step_in_date - periods.front().accrual_start) / 360.0;
        }
    };

    /// @brief Build a CDS with an explicit maturity. The first accrual date is the latest adjusted IMM date on or previous coupon
    ///        payment date before the step-in date (full first coupon); then quarterly IMM dates, adjusted, up to maturity.
    ///        Weekends-only calendar by default
    /// @param maturity       unadjusted protection end date (an IMM date for standard contracts)
    /// @param running_coupon fixed annual coupon paid quarterly, e.g. 0.01 for 100bp
    /// @param recovery_rate  fraction of notional recovered at default, in [0, 1[
    template <class Cal = Core::WeekendsOnly>
    [[nodiscard]] Cds make_cds(Core::Date trade_date, Core::Date maturity, double running_coupon, double recovery_rate,
                               const Cal& calendar = {}, const CdsConventions& conventions = {},
                               double notional = 1.0) {
        if (!(recovery_rate >= 0.0 && recovery_rate < 1.0))
            throw std::invalid_argument("make_cds: recovery rate must lie in [0, 1[");
        if (!(running_coupon >= 0.0))
            throw std::invalid_argument("make_cds: running coupon cannot be negative");

        Cds cds;
        cds.trade_date = trade_date;
        cds.step_in_date = trade_date + conventions.step_in_days;
        cds.cash_settlement_date = calendar.advance(trade_date, conventions.cash_settlement_days);
        cds.protection_start = trade_date;
        cds.maturity = maturity;
        cds.running_coupon = running_coupon;
        cds.recovery_rate = recovery_rate;
        cds.notional = notional;
        if (!(cds.step_in_date < maturity))
            throw std::invalid_argument("make_cds: maturity must be after the step-in date");

        // Previous adjusted IMM date on or before the step-in date (first coupon beginning period coverage)
        Core::Date anchor = Core::previous_imm_date(cds.step_in_date);
        if (calendar.adjust(anchor, conventions.convention) > cds.step_in_date)
            anchor = Core::previous_imm_date(anchor - 1);

        Core::Date accrual_start = calendar.adjust(anchor, conventions.convention);
        for (int k = 1;; ++k) {
            const Core::Date unadjusted_end = Core::add_period(anchor, conventions.coupon_tenor * k);
            if (unadjusted_end >= maturity) {
                const Core::Date accrual_end = maturity + 1;
                cds.periods.push_back({accrual_start, accrual_end, calendar.adjust(maturity, conventions.convention),
                                       static_cast<double>(accrual_end - accrual_start) / 360.0});
                break;
            }
            const Core::Date accrual_end = calendar.adjust(unadjusted_end, conventions.convention);
            cds.periods.push_back({accrual_start, accrual_end, accrual_end,
                                   static_cast<double>(accrual_end - accrual_start) / 360.0});
            accrual_start = accrual_end;
        }
        return cds;
    }

    /// @brief Standard CDS according to the ISDA conventions : maturity from the ISDA 2015 roll rule
    template <class Cal = Core::WeekendsOnly>
    [[nodiscard]] Cds make_standard_cds(Core::Date trade_date, Core::Period tenor, double running_coupon,
                                        double recovery_rate, const Cal& calendar = {},
                                        const CdsConventions& conventions = {}, double notional = 1.0) {
        return make_cds(trade_date, Core::cds_standard_maturity(trade_date, tenor), running_coupon, recovery_rate,
                        calendar, conventions, notional);
    }

} // namespace Instruments::Credit
