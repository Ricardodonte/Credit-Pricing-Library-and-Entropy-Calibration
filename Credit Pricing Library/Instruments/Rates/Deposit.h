// Cash deposit.

#pragma once
#include <stdexcept>

#include "Core/Dates.h"
#include "Core/Period.h"
#include "Core/calendar.h"
#include "Core/day_counter.h"

namespace Instruments::Rates {

    /// @brief Cash deposit: 1 lent at `start`, 1 + rate * accrual received at `maturity`.
    ///        Conventions (spot lag, day count, adjustment) are resolved at construction.
    struct Deposit {
        Core::Date start;
        Core::Date maturity;
        double accrual;  ///< year fraction start -> maturity under the deposit day count
        double rate;     ///< simple rate quote
    };

    /// @brief Deposit starting at spot (trade + settlement_days business days) for the given tenor.
    /// @param settlement_days business days between trade date and start date (2 for EUR, 0 for overnight)
    template <class Cal, Core::DayCountConvention DC = Core::Actual360>
    [[nodiscard]] Deposit make_deposit(Core::Date trade_date, Core::Period tenor, double rate, const Cal& calendar,
                                       DC day_counter = {}, int settlement_days = 2,
                                       Core::BusinessDayConvention convention =
                                           Core::BusinessDayConvention::ModifiedFollowing) {
        const Core::Date start = calendar.advance(trade_date, settlement_days);
        const Core::Date maturity = calendar.adjust(Core::add_period(start, tenor), convention);
        if (!(start < maturity))
            throw std::invalid_argument("make_deposit: maturity must be after the start date");
        return {start, maturity, day_counter.year_fraction(start, maturity), rate};
    }

} // namespace Instruments::Rates
